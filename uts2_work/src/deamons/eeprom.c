/**
 *  @file       eeprom_mgr.c
 *  @headerfile eeprom_mgr.h
 *
 *  @date       2026.10.28
 *  @author     Dymov Igor (Refactored)
 *
 *  @brief      Менеджер энергонезависимой памяти EEPROM с поддержкой k_msgq и квитирования
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>

#include "system_bus_model.h"
#include "app_worker.h"
#include "system_define.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(eeprom_mgr, LOG_LEVEL_INF);

#define EEPROM_UID_OFFSET         0xFC
#define EEPROM_PAGE_SIZE          16U    // Размер страницы 24AA025UID
#define EEPROM_DEBOUNCE_DELAY_MS  50U   // Окно агрегации изменений
#define EEPROM_PAGE_WRITE_TIME_MS 15U    // Пауза между страницами для t_WC EEPROM
#define MAX_PENDING_SYNCS         4      // Максимум одновременно ждущих потоков квитирования

#define EEPROM_PAYLOAD_DATA_SIZE  (sizeof(eeprom_disk_payload_t) - sizeof(uint16_t))

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

struct __packed eeprom_device_msg 
{
    uint32_t factory_uid;
    uint32_t serial_number;
    uint32_t product_code;
    uint32_t device_class;
    uint32_t device_num;
    uint32_t device_major_ver;
    uint32_t device_minor_ver;
    uint32_t usage_counter;
};
typedef struct eeprom_device_msg eeprom_device_msg_t;

struct __packed eeprom_disk_payload 
{
    uint32_t serial_number;
    uint32_t product_code;
    uint32_t device_class;
    uint32_t device_num;
    uint32_t device_major_ver;
    uint32_t device_minor_ver;
    uint32_t usage_counter;    
    uint16_t crc16;
};
typedef struct eeprom_disk_payload eeprom_disk_payload_t;

/**
 *  @brief Структура для хранения информации о ждущих клиентах квитирования
 */
typedef struct {
    struct k_sem *sync;
    uint8_t      *response;
} pending_sync_t;

/**
 *  @brief Дескриптор EEPROM
 */
typedef struct eeprom_entry
{
    const struct device *const dev;
    const struct zbus_channel *chan;
    struct k_msgq             *rx_queue;
    const char                *name;
    uint32_t                   error_bit;
    uint32_t                   set_crc_error_bit;
    struct k_work_delayable    debounce_task; 
    struct k_work_delayable    write_task;    
    off_t                      offset;
    size_t                     remaining;
    eeprom_disk_payload_t      disk_cache;
    eeprom_disk_payload_t      staging_cache; 
    bool                       is_busy;       

    pending_sync_t             pending_syncs[MAX_PENDING_SYNCS];
    uint8_t                    sync_count;
} eeprom_entry_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void     _eeprom_work_poll_handler(struct k_work *_work);
static void     _eeprom_debounce_handler(struct k_work *_work);
static void     _eeprom_write_step_handler(struct k_work *_work);
static void     _start_write_transaction(eeprom_entry_t *_entry);
static void     _write_page_step(eeprom_entry_t *_entry);
static void     _notify_all_pending(eeprom_entry_t *_entry, int err);
static uint16_t _calc_disk_crc(const eeprom_disk_payload_t *_disk_data);
static void     _read_and_publish_eeprom_chip(eeprom_entry_t *_entry, uint32_t *_uid_out);
static int      _eeprom_mgr_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/

K_MSGQ_DEFINE(eeprom1_rx_queue, sizeof(server_request), 5, 4);
K_MSGQ_DEFINE(eeprom2_rx_queue, sizeof(server_request), 5, 4);


ZBUS_CHAN_DEFINE(eeprom1_chan,
                 eeprom_device_msg_t,
                 NULL, 
                 &eeprom1_rx_queue,      /* <--- ВОТ ЗДЕСЬ БЫЛ ПРОПУСК! */
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

ZBUS_CHAN_DEFINE(eeprom2_chan,
                 eeprom_device_msg_t,
                 NULL, 
                 &eeprom2_rx_queue,      /* <--- И ЗДЕСЬ ТОЖЕ! */
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);



static eeprom_entry_t eeprom_entries[] = {
    {
        .dev                = DEVICE_DT_GET(DT_NODELABEL(eeprom_1)),
        .chan               = &eeprom1_chan,
        .rx_queue           = &eeprom1_rx_queue,
        .name               = "IU",
        .error_bit          = EEPORM_IU_ERROR,
        .set_crc_error_bit  = EVENT_EEPROM_IU_CRC_ERROR,
        .is_busy            = false,
        .sync_count         = 0,
    },
    {
        .dev                = DEVICE_DT_GET(DT_NODELABEL(eeprom_2)),
        .chan               = &eeprom2_chan,
        .rx_queue           = &eeprom2_rx_queue,
        .name               = "CU",
        .error_bit          = EEPORM_CU_ERROR,
        .set_crc_error_bit  = EVENT_EEPROM_CU_CRC_ERROR,
        .is_busy            = false,
        .sync_count         = 0,
    }
};

static struct k_work_poll  eeprom_task;
static struct k_poll_event eeprom_poll_events[2];

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

static inline uint16_t _calc_disk_crc(const eeprom_disk_payload_t *_disk_data)
{
    return crc16_itu_t(0xFFFF, (const uint8_t *)_disk_data, EEPROM_PAYLOAD_DATA_SIZE);
}

static void _notify_all_pending(eeprom_entry_t *_entry, int err)
{
    for (int i = 0; i < _entry->sync_count; i++) 
    {
        if (_entry->pending_syncs[i].response != NULL) 
        {
            *_entry->pending_syncs[i].response = err;
        }
        if (_entry->pending_syncs[i].sync != NULL) 
        {
            k_sem_give(_entry->pending_syncs[i].sync);
        }
    }
    _entry->sync_count = 0;
}

/**
 *  @brief      Обработчик опроса очередей (Контекст: COMMON_WORKER)
 */
static void _eeprom_work_poll_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);

    for (size_t i = 0; i < ARRAY_SIZE(eeprom_entries); i++) 
    {
        eeprom_entry_t *entry = &eeprom_entries[i];
        server_request req;

        while (k_msgq_get(entry->rx_queue, &req, K_NO_WAIT) == 0) 
        {
            
            switch(req.id)
            {
                case SETTING_IU_SN:
                case SETTING_CU_SN:            
                    entry->staging_cache.serial_number = req.value.sd_uint;
                    break;
                case SETTING_IU_HW_ID:
                case SETTING_CU_HW_ID:             
                    entry->staging_cache.device_major_ver = req.value.sd_uint;
                    break;
                case SETTING_IU_HW_VER:
                case SETTING_CU_HW_VER:            
                    entry->staging_cache.device_minor_ver = req.value.sd_uint;
                    break;
                case SETTING_IU_USER_DATA:
                case SETTING_CU_USER_DATA:            
                    entry->staging_cache.usage_counter = req.value.sd_uint;
                default:
                    break;
            }                
        }

        if (memcmp(&entry->staging_cache, &entry->disk_cache, sizeof(eeprom_disk_payload_t)) != 0) 
        {
            if (req.sync != NULL && entry->sync_count < MAX_PENDING_SYNCS) 
            {                
                entry->pending_syncs[entry->sync_count].sync = req.sync;
                entry->pending_syncs[entry->sync_count].response = req.response;
                entry->sync_count++;
            }
            app_worker_reschedule_submit(&entry->debounce_task, 
                                         K_MSEC(EEPROM_DEBOUNCE_DELAY_MS), 
                                         COMMON_WORKER);
        }
        else 
        {
            if (req.sync != NULL)
            {
                if (req.response != NULL)
                {
                    *req.response = 0;
                }
                k_sem_give(req.sync);                
            }
        }
    }

    eeprom_poll_events[0].state = K_POLL_STATE_NOT_READY;
    eeprom_poll_events[1].state = K_POLL_STATE_NOT_READY;
  
    app_worker_poll_submit(&eeprom_task, eeprom_poll_events,ARRAY_SIZE(eeprom_poll_events), COMMON_WORKER);
}

/**
 *  @brief      Таймер дебаунса истёк
 */
static void _eeprom_debounce_handler(struct k_work *_work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(_work);
    eeprom_entry_t *entry = CONTAINER_OF(dwork, eeprom_entry_t, debounce_task);

    entry->staging_cache.crc16 = _calc_disk_crc(&entry->staging_cache);
    entry->disk_cache = entry->staging_cache;
    
    entry->offset    = 0;
    entry->remaining = sizeof(eeprom_disk_payload_t);
    entry->is_busy   = true;

    _write_page_step(entry);
}


static void _write_page_step(eeprom_entry_t *entry)
{
    size_t bytes_to_page_end = EEPROM_PAGE_SIZE - (entry->offset % EEPROM_PAGE_SIZE);
    size_t chunk = MIN(entry->remaining, bytes_to_page_end);

    const uint8_t *src_buf = ((const uint8_t *)&entry->disk_cache) + entry->offset;

    int err = eeprom_write(entry->dev, entry->offset, src_buf, chunk);
    if (err != 0) 
    {
        LOG_ERR("Failed to write %s at offset 0x%02X (err %d)", 
                entry->name, (uint32_t)entry->offset, err);
        entry->is_busy = false;
        _notify_all_pending(entry, err);
        return;
    }

    entry->offset    += chunk;
    entry->remaining -= chunk;

    if (entry->remaining > 0) 
    {
        app_worker_reschedule_submit(&entry->write_task, 
                                     K_MSEC(EEPROM_PAGE_WRITE_TIME_MS), 
                                     COMMON_WORKER);
    } 
    else 
    {
        LOG_INF("Data successfully persisted to %s", entry->name);
       
        SERVER_CH_CLEAR_BIT(EEPROM_EVENT_CH, entry->set_crc_error_bit);
        entry->is_busy = false;

        _notify_all_pending(entry, 0);

        eeprom_device_msg_t msg;
        msg.serial_number = entry->disk_cache.serial_number;
        msg.product_code = entry->disk_cache.product_code;
        msg.device_class = entry->disk_cache.device_class;
        msg.device_num = entry->disk_cache.device_num;
        msg.device_major_ver = entry->disk_cache.device_major_ver;
        msg.device_minor_ver = entry->disk_cache.device_minor_ver;
        msg.usage_counter = entry->disk_cache.usage_counter;
        zbus_chan_pub(entry->chan, &msg, K_MSEC(2));    
                
    }
}

static void _eeprom_write_step_handler(struct k_work *_work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(_work);
    eeprom_entry_t *entry = CONTAINER_OF(dwork, eeprom_entry_t, write_task);
    _write_page_step(entry);
}

static void _read_and_publish_eeprom_chip(eeprom_entry_t *_entry, uint32_t *_uid_out)
{
    eeprom_device_msg_t msg = {0};

    SERVER_CH_CLEAR_BIT(EEPROM_ERROR_CH, _entry->error_bit);
    SERVER_CH_CLEAR_BIT(EEPROM_EVENT_CH, _entry->set_crc_error_bit);

    int err = eeprom_read(_entry->dev, EEPROM_UID_OFFSET, &msg.factory_uid, sizeof(uint32_t));
    if ((err != 0) || (msg.factory_uid == 0x00)) 
    {
        LOG_ERR("Failed to read Factory UID from %s (err %d)", _entry->name, err);  
        err = 1;          
    }
    else 
    {   
        if (_uid_out != NULL) {
            *_uid_out = msg.factory_uid;
        }
    
        eeprom_disk_payload_t disk_data;
        int read_err = eeprom_read(_entry->dev, 0x00, &disk_data, sizeof(disk_data));
        if (read_err == 0) 
        {
            if (_calc_disk_crc(&disk_data) == disk_data.crc16) 
            {
                msg.serial_number = disk_data.serial_number;
                msg.product_code = disk_data.product_code;
                msg.device_class = disk_data.device_class;
                msg.device_num = disk_data.device_num;
                msg.device_major_ver = disk_data.device_major_ver;
                msg.device_minor_ver = disk_data.device_minor_ver;
                msg.usage_counter = disk_data.usage_counter;

                memcpy(&_entry->disk_cache, &disk_data, sizeof(disk_data));
                _entry->staging_cache = _entry->disk_cache;
                LOG_INF("%s EEPROM User Data CRC OK (0x%04X)", _entry->name, disk_data.crc16);
            } 
            else 
            {
                LOG_WRN("CRC mismatch on %s user data!", _entry->name);
                memset(&_entry->disk_cache, 0x00, sizeof(_entry->disk_cache));
                _entry->staging_cache = _entry->disk_cache;
                SERVER_CH_SET_BIT(EEPROM_EVENT_CH, _entry->set_crc_error_bit);
            }
        }
    }
              
    zbus_chan_pub(_entry->chan, &msg, K_MSEC(2));    
               
    if (err != 0) 
    {
        SERVER_CH_SET_BIT(EEPROM_ERROR_CH, _entry->error_bit);
    }
}

static int _eeprom_mgr_init(void)
{
    uint32_t uids[ARRAY_SIZE(eeprom_entries)] = {0};

    for (size_t i = 0; i < ARRAY_SIZE(eeprom_entries); i++) 
    {
        eeprom_entry_t *entry = &eeprom_entries[i];

        k_work_init_delayable(&entry->debounce_task, _eeprom_debounce_handler);
        k_work_init_delayable(&entry->write_task, _eeprom_write_step_handler);
        
        _read_and_publish_eeprom_chip(entry, &uids[i]);
    }

    k_poll_event_init(&eeprom_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY,
                      eeprom_entries[0].rx_queue);

    k_poll_event_init(&eeprom_poll_events[1],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY,
                      eeprom_entries[1].rx_queue);
   

    k_work_poll_init(&eeprom_task, _eeprom_work_poll_handler);
    app_worker_poll_submit(&eeprom_task, eeprom_poll_events, ARRAY_SIZE(eeprom_poll_events),COMMON_WORKER);

    LOG_INF("EEPROM Manager initialized with k_work_poll & Sync support.");
    return 0;
}

SYS_INIT(_eeprom_mgr_init, EEPROM_DEAMON_INIT_LEVEL, EEPROM_DEAMON_INIT_PRIOR);

/***************************************************************************************************
 *                                     PARAMETER ROUTING TABLE
 **************************************************************************************************/
SERVER_CHANNEL_DEFINE(SETTING_IU_ID,        &eeprom1_chan, 0, UINT_DATA);
SERVER_CHANNEL_DEFINE(SETTING_IU_SN,        &eeprom1_chan, 1, UINT_DATA);
SERVER_CHANNEL_DEFINE(SETTING_IU_HW_ID,     &eeprom1_chan, 2, UINT_DATA);
SERVER_CHANNEL_DEFINE(SETTING_IU_HW_VER,    &eeprom1_chan, 3, UINT_DATA);
SERVER_CHANNEL_DEFINE(SETTING_IU_USER_DATA, &eeprom1_chan, 4, UINT_DATA);

SERVER_CHANNEL_DEFINE(SETTING_CU_ID,        &eeprom2_chan, 0, UINT_DATA);
SERVER_CHANNEL_DEFINE(SETTING_CU_SN,        &eeprom2_chan, 1, UINT_DATA);
SERVER_CHANNEL_DEFINE(SETTING_CU_HW_ID,     &eeprom2_chan, 2, UINT_DATA);
SERVER_CHANNEL_DEFINE(SETTING_CU_HW_VER,    &eeprom2_chan, 3, UINT_DATA);
SERVER_CHANNEL_DEFINE(SETTING_CU_USER_DATA, &eeprom2_chan, 4, UINT_DATA);


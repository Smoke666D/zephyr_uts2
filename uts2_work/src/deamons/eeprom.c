/**
 *  @file       eeprom_mgr.c
 *  @headerfile eeprom_mgr.h
 *
 *  @brief      Менеджер энергонезависимой памяти EEPROM (Microchip 24AA025UID)
 *              с выровненными индексами Zbus, корректной нарезкой страниц и расчетом CRC16.
 */

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

LOG_MODULE_REGISTER(eeprom_mgr, LOG_LEVEL_INF);

#define EEPROM_UID_OFFSET         0xFC
#define EEPROM_PAGE_SIZE          16U    // Аппаратный размер страницы 24AA025UID
#define EEPROM_DEBOUNCE_DELAY_MS  50U    // Окно накопления изменений перед сбросом на чип
#define EEPROM_PAGE_WRITE_TIME_MS 10U    // Аппаратное время цикла записи t_WC (5-10 мс)
#define MAX_PENDING_SYNCS         8      // Очередь ожидающих квитирования

#define EEPROM_PAYLOAD_DATA_SIZE  (sizeof(eeprom_disk_payload_t) - sizeof(uint16_t))

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

/**
 *  @brief Сообщение Zbus: ровно 5 полей по 32 бита, строго совпадающих с индексами 0, 1, 2, 3, 4!
 */
struct __packed eeprom_device_msg 
{
    uint32_t factory_uid;   // Индекс 0: SETTING_IU_ID / SETTING_CU_ID
    uint32_t serial_number; // Индекс 1: SETTING_IU_SN / SETTING_CU_SN
    uint32_t hw_id;         // Индекс 2: SETTING_IU_HW_ID / SETTING_CU_HW_ID (Слово 1 HW)
    uint32_t hw_ver;        // Индекс 3: SETTING_IU_HW_VER / SETTING_CU_HW_VER (Слово 2 HW)
    uint32_t user_data;     // Индекс 4: SETTING_IU_USER_DATA / SETTING_CU_USER_DATA
};
typedef struct eeprom_device_msg eeprom_device_msg_t;

/**
 *  @brief Структура на диске EEPROM: 16 байт данных (1 страница) + 2 байта CRC16 = 18 байт
 */
struct __packed eeprom_disk_payload 
{
    uint32_t serial_number; // 4 байта (offset 0..3)
    uint32_t hw_id;         // 4 байта (offset 4..7)
    uint32_t hw_ver;        // 4 байта (offset 8..11)
    uint32_t user_data;     // 4 байта (offset 12..15)
    uint16_t crc16;         // 2 байта (offset 16..17)
};
typedef struct eeprom_disk_payload eeprom_disk_payload_t;

typedef struct {
    struct k_sem *sync;
    uint8_t      *response;
} pending_sync_t;

typedef struct eeprom_entry
{
    const struct device *const dev;
    const struct zbus_channel *chan;
    struct k_msgq             *rx_queue;
    const char                *name;
    uint32_t                   error_bit;
    uint32_t                   set_crc_error_bit;
    struct k_work_delayable    write_task; 
    
    eeprom_disk_payload_t      disk_cache;
    eeprom_disk_payload_t      staging_cache;

    pending_sync_t             pending_syncs[MAX_PENDING_SYNCS];
    uint8_t                    sync_count;
    struct k_mutex             lock;
} eeprom_entry_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void     _eeprom_work_poll_handler(struct k_work *_work);
static void     _eeprom_write_flush_handler(struct k_work *_work);
static int      _eeprom_write_pages_sync(const struct device *dev, off_t offset, const void *data, size_t len);
static uint16_t _calc_disk_crc(const eeprom_disk_payload_t *_disk_data);
static void     _notify_pending_locked(eeprom_entry_t *_entry, int err);
static void     _read_and_publish_eeprom_chip(eeprom_entry_t *_entry, uint32_t *_uid_out);
static int      _eeprom_mgr_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/

K_MSGQ_DEFINE(eeprom1_rx_queue, sizeof(server_request), 10, 4);
K_MSGQ_DEFINE(eeprom2_rx_queue, sizeof(server_request), 10, 4);

ZBUS_CHAN_DEFINE(eeprom1_chan,
                 eeprom_device_msg_t,
                 NULL, 
                 &eeprom1_rx_queue,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

ZBUS_CHAN_DEFINE(eeprom2_chan,
                 eeprom_device_msg_t,
                 NULL, 
                 &eeprom2_rx_queue,
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
        .sync_count         = 0,
    },
    {
        .dev                = DEVICE_DT_GET(DT_NODELABEL(eeprom_2)),
        .chan               = &eeprom2_chan,
        .rx_queue           = &eeprom2_rx_queue,
        .name               = "CU",
        .error_bit          = EEPORM_CU_ERROR,
        .set_crc_error_bit  = EVENT_EEPROM_CU_CRC_ERROR,
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

static void _notify_pending_locked(eeprom_entry_t *_entry, int err)
{
    for (uint8_t i = 0; i < _entry->sync_count; i++) 
    {
        if (_entry->pending_syncs[i].response != NULL) 
        {
            *_entry->pending_syncs[i].response = (uint8_t)err;
        }
        if (_entry->pending_syncs[i].sync != NULL) 
        {
            k_sem_give(_entry->pending_syncs[i].sync);
        }
    }
    _entry->sync_count = 0;
}

/**
 *  @brief  Постраничная запись с соблюдением аппаратных границ 16 байт и пауз t_WC
 */
static int _eeprom_write_pages_sync(const struct device *dev, 
                                    off_t offset, 
                                    const void *data, 
                                    size_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    size_t remaining = len;
    off_t cur_offset = offset;

    while (remaining > 0) 
    {
        size_t page_offset = cur_offset % EEPROM_PAGE_SIZE;
        size_t chunk = MIN(remaining, EEPROM_PAGE_SIZE - page_offset);

        int err = eeprom_write(dev, cur_offset, src, chunk);
        if (err != 0) 
        {
            LOG_ERR("eeprom_write failed (err %d) at offset 0x%02X", err, (uint32_t)cur_offset);
            return err;
        }

        src += chunk;
        cur_offset += chunk;
        remaining -= chunk;

        if (remaining > 0) 
        {
            k_msleep(EEPROM_PAGE_WRITE_TIME_MS);
        }
    }

    k_msleep(EEPROM_PAGE_WRITE_TIME_MS);
    return 0;
}

/**
 *  @brief Обработчик входящих сообщений (Запись в staging_cache)
 */
static void _eeprom_work_poll_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);

    for (size_t i = 0; i < ARRAY_SIZE(eeprom_entries); i++) 
    {
        eeprom_entry_t *entry = &eeprom_entries[i];
        server_request req;
        bool has_new_data = false;

        k_mutex_lock(&entry->lock, K_FOREVER);

        while (k_msgq_get(entry->rx_queue, &req, K_NO_WAIT) == 0) 
        {
            has_new_data = true;

            // Теперь каждое поле попадает строго в свой адрес!
            switch (req.id)
            {
                case SETTING_IU_SN:
                case SETTING_CU_SN:            
                    entry->staging_cache.serial_number = req.value.sd_uint;
                    LOG_INF("%s: new SN = %u", entry->name, req.value.sd_uint);
                    break;

                case SETTING_IU_HW_ID:
                case SETTING_CU_HW_ID:             
                    entry->staging_cache.hw_id = req.value.sd_uint;
                    LOG_INF("%s: new HW_ID = 0x%08X", entry->name, req.value.sd_uint);
                    break;

                case SETTING_IU_HW_VER:
                case SETTING_CU_HW_VER:            
                    entry->staging_cache.hw_ver = req.value.sd_uint;
                    LOG_INF("%s: new HW_VER = 0x%08X", entry->name, req.value.sd_uint);
                    break;

                case SETTING_IU_USER_DATA:
                case SETTING_CU_USER_DATA:            
                    entry->staging_cache.user_data = req.value.sd_uint;
                    LOG_INF("%s: new USER_DATA = %u", entry->name, req.value.sd_uint);
                    break;

                default:
                    LOG_WRN("%s: unknown setting id %d", entry->name, req.id);
                    break;
            }

            if (req.sync != NULL) 
            {
                if (entry->sync_count < MAX_PENDING_SYNCS) 
                {
                    entry->pending_syncs[entry->sync_count].sync = req.sync;
                    entry->pending_syncs[entry->sync_count].response = req.response;
                    entry->sync_count++;
                } 
                else 
                {
                    if (req.response != NULL) {
                        *req.response = (uint8_t)-ENOMEM;
                    }
                    k_sem_give(req.sync);
                }
            }
        }

        if (has_new_data) 
        {
            if (memcmp(&entry->staging_cache, &entry->disk_cache, sizeof(eeprom_disk_payload_t)) != 0) 
            {
                app_worker_reschedule_submit(&entry->write_task, 
                                             K_MSEC(EEPROM_DEBOUNCE_DELAY_MS), 
                                             COMMON_WORKER);
            } 
            else 
            {
                _notify_pending_locked(entry, 0);
            }
        }

        k_mutex_unlock(&entry->lock);
    }

    eeprom_poll_events[0].state = K_POLL_STATE_NOT_READY;
    eeprom_poll_events[1].state = K_POLL_STATE_NOT_READY;
    app_worker_poll_submit(&eeprom_task, eeprom_poll_events, ARRAY_SIZE(eeprom_poll_events), COMMON_WORKER);
}

/**
 *  @brief Запись и контрольное чтение с проверкой CRC
 */
static void _eeprom_write_flush_handler(struct k_work *_work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(_work);
    eeprom_entry_t *entry = CONTAINER_OF(dwork, eeprom_entry_t, write_task);

    k_mutex_lock(&entry->lock, K_FOREVER);

    // 1. Вычисляем CRC16 для всех 16 байт данных
    entry->staging_cache.crc16 = _calc_disk_crc(&entry->staging_cache);
    entry->disk_cache = entry->staging_cache;

    LOG_INF("Writing %s EEPROM (18 bytes), CRC: 0x%04X", entry->name, entry->disk_cache.crc16);

    // 2. Пишем 18 байт (страница 0: 16 байт -> пауза 10 мс -> страница 1: 2 байта CRC)
    int err = _eeprom_write_pages_sync(entry->dev, 0, &entry->disk_cache, sizeof(eeprom_disk_payload_t));
    if (err != 0) 
    {
        LOG_ERR("Failed to write %s EEPROM (err %d)", entry->name, err);
        SERVER_CH_SET_BIT(EEPROM_ERROR_CH, entry->error_bit);
    } 
    else 
    {
        // 3. Контрольная проверка вычиткой (Readback verification)
        eeprom_disk_payload_t verify_buf;
        int read_err = eeprom_read(entry->dev, 0, &verify_buf, sizeof(verify_buf));

        if ((read_err == 0) && (_calc_disk_crc(&verify_buf) == verify_buf.crc16)) 
        {
            LOG_INF("%s EEPROM successfully written & verified! CRC: 0x%04X", 
                    entry->name, verify_buf.crc16);
            SERVER_CH_CLEAR_BIT(EEPROM_ERROR_CH, entry->error_bit);
            SERVER_CH_CLEAR_BIT(EEPROM_EVENT_CH, entry->set_crc_error_bit);

            // Публикуем в Zbus строго по тем же полям 0, 1, 2, 3, 4!
            eeprom_device_msg_t msg;
            msg.factory_uid   = 0; // заводской UID не перезаписывается
            eeprom_read(entry->dev, EEPROM_UID_OFFSET, &msg.factory_uid, sizeof(uint32_t));

            msg.serial_number = verify_buf.serial_number;
            msg.hw_id         = verify_buf.hw_id;
            msg.hw_ver        = verify_buf.hw_ver;
            msg.user_data     = verify_buf.user_data;

            zbus_chan_pub(entry->chan, &msg, K_MSEC(10));
        } 
        else 
        {
            LOG_ERR("%s EEPROM readback CRC mismatch! Read: 0x%04X, Expected: 0x%04X", 
                    entry->name, verify_buf.crc16, entry->disk_cache.crc16);
            SERVER_CH_SET_BIT(EEPROM_EVENT_CH, entry->set_crc_error_bit);
            err = -EIO;
        }
    }

    _notify_pending_locked(entry, err);

    k_mutex_unlock(&entry->lock);
}

static void _read_and_publish_eeprom_chip(eeprom_entry_t *_entry, uint32_t *_uid_out)
{
    eeprom_device_msg_t msg = {0};

    SERVER_CH_CLEAR_BIT(EEPROM_ERROR_CH, _entry->error_bit);
    SERVER_CH_CLEAR_BIT(EEPROM_EVENT_CH, _entry->set_crc_error_bit);

    // Чтение аппаратного UID (0xFC)
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
                msg.hw_id         = disk_data.hw_id;
                msg.hw_ver        = disk_data.hw_ver;
                msg.user_data     = disk_data.user_data;

                memcpy(&_entry->disk_cache, &disk_data, sizeof(disk_data));
                _entry->staging_cache = _entry->disk_cache;
                LOG_INF("%s EEPROM Data CRC OK (0x%04X). SN=%u, HW_ID=0x%08X, HW_VER=0x%08X, USER=%u", 
                        _entry->name, disk_data.crc16, msg.serial_number, msg.hw_id, msg.hw_ver, msg.user_data);
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
              
    zbus_chan_pub(_entry->chan, &msg, K_MSEC(10));    
               
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

        k_mutex_init(&entry->lock);
        k_work_init_delayable(&entry->write_task, _eeprom_write_flush_handler);
        
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
    app_worker_poll_submit(&eeprom_task, eeprom_poll_events, ARRAY_SIZE(eeprom_poll_events), COMMON_WORKER);

    LOG_INF("EEPROM Manager initialized reliably.");
    return 0;
}

SYS_INIT(_eeprom_mgr_init, EEPROM_DEAMON_INIT_LEVEL, EEPROM_DEAMON_INIT_PRIOR);

/***************************************************************************************************
 *                 ПАРАМЕТРЫ МАРШРУТИЗАЦИИ (СТРОГО СОВПАДАЮТ С ИНДЕКСАМИ 0..4)
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
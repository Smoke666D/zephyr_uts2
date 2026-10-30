/**
 *  @file       eeprom_mgr.c
 *  @headerfile eeprom_mgr.h
 *
 *  @date       2026.10.28
 *  @author     Dymov Igor (Refactored)
 *
 *  @brief      Менеджер энергонезависимой памяти EEPROM
 *  @details    Обеспечивает чтение данных при старте, проверку CRC16,
 *              публикацию в ZBUS и неблокирующую запись с полной
 *              изоляцией состояния внутри потока COMMON_WORKER.
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
#define EEPROM_DEBOUNCE_DELAY_MS  10U    // Окно агрегации изменений ZBUS
#define EEPROM_PAGE_WRITE_TIME_MS 15U    // Пауза между страницами для t_WC EEPROM

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
 *  @brief Дескриптор EEPROM
 */
typedef struct eeprom_entry
{
    /* Неизменяемые параметры конфигурации */
    const struct device *const dev;
    const struct zbus_channel *chan;
    const char                *name;
    uint32_t                   error_bit;
    uint32_t                   set_ok_bit;
    uint32_t                   set_error_bit;
    uint32_t                   set_crc_error_bit;

    /* Задачи очередей (управление потокобезопасно через API Zephyr) */
    struct k_work_delayable    debounce_task; // Отвечает за дебаунс ZBUS (10 мс)
    struct k_work_delayable    write_task;    // Отвечает за постраничную запись (15 мс)

    /* ========================================================================= */
    /* ПРИВАТНОЕ СОСТОЯНИЕ АВТОМАТА (ДОСТУПНО ТОЛЬКО ИЗ ПОТОКА COMMON_WORKER!)   */
    /* ========================================================================= */
    off_t                      offset;
    size_t                     remaining;
    eeprom_disk_payload_t      disk_cache;
    bool                       is_busy;       // Идет ли сейчас физическая запись
    bool                       is_dirty;      // Пришли ли новые данные, пока шла запись
} eeprom_entry_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void     _eeprom_zbus_listener_callback(const struct zbus_channel *_chan);
static void     _eeprom_debounce_work_handler(struct k_work *_work);
static void     _eeprom_write_step_handler(struct k_work *_work);
static void     _start_write_transaction(eeprom_entry_t *_entry);
static void     _write_page_step(eeprom_entry_t *_entry);
static uint16_t _calc_disk_crc(const eeprom_disk_payload_t *_disk_data);
static void     _read_and_publish_eeprom_chip(eeprom_entry_t *_entry, uint32_t *_uid_out);
static int      _eeprom_mgr_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/

ZBUS_LISTENER_DEFINE(eeprom_lis, _eeprom_zbus_listener_callback);

ZBUS_CHAN_DEFINE(eeprom1_chan,
                 eeprom_device_msg_t,
                 NULL, NULL,
                 ZBUS_OBSERVERS(eeprom_lis),
                 {0}
);

ZBUS_CHAN_DEFINE(eeprom2_chan,
                 eeprom_device_msg_t,
                 NULL, NULL,
                 ZBUS_OBSERVERS(eeprom_lis),
                 {0}
);

static eeprom_entry_t eeprom_entries[] = {
    {
        .dev                = DEVICE_DT_GET(DT_NODELABEL(eeprom_1)),
        .chan               = &eeprom1_chan,
        .name               = "IU",
        .error_bit          = EEPORM_IU_ERROR,
        .set_ok_bit         = EVENT_EEPROM_IU_WRITE_OK,
        .set_error_bit      = EVENT_EEPROM_IU_WRITE_ERROR,
        .set_crc_error_bit  = EVENT_EEPROM_IU_CRC_ERROR,
        .is_busy            = false,
        .is_dirty           = false,
    },
    {
        .dev                = DEVICE_DT_GET(DT_NODELABEL(eeprom_2)),
        .chan               = &eeprom2_chan,
        .name               = "CU",
        .error_bit          = EEPORM_CU_ERROR,
        .set_ok_bit         = EVENT_EEPROM_CU_WRITE_OK,
        .set_error_bit      = EVENT_EEPROM_CU_WRITE_ERROR,
        .set_crc_error_bit  = EVENT_EEPROM_CU_CRC_ERROR,
        .is_busy            = false,
        .is_dirty           = false,
    }
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

static inline uint16_t _calc_disk_crc(const eeprom_disk_payload_t *_disk_data)
{
    return crc16_itu_t(0xFFFF, (const uint8_t *)_disk_data, EEPROM_PAYLOAD_DATA_SIZE);
}

/**
 *  @brief      Листенер ZBUS (Выполняется в контексте ПОТОКА ИЗДАТЕЛЯ)
 *  @note       НЕ читает и НЕ изменяет переменные состояния! Полная изоляция.
 */
static void _eeprom_zbus_listener_callback(const struct zbus_channel *_chan)
{
    for (size_t i = 0; i < ARRAY_SIZE(eeprom_entries); i++) 
    {
        eeprom_entry_t *entry = &eeprom_entries[i];

        if (entry->chan == _chan) 
        {
            // Перезапуск дебаунс-таймера. Функция ядра Zephyr потокобезопасна из коробки.
            app_worker_reschedule_submit(&entry->debounce_task, 
                                         K_MSEC(EEPROM_DEBOUNCE_DELAY_MS), 
                                         COMMON_WORKER);
            break;
        }
    }
}

/**
 *  @brief      Обработчик окончания дебаунса (Контекст: ТОЛЬКО COMMON_WORKER)
 */
static void _eeprom_debounce_work_handler(struct k_work *_work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(_work);
    eeprom_entry_t *entry = CONTAINER_OF(dwork, eeprom_entry_t, debounce_task);

    // Если прямо сейчас идет постраничная запись — просто выставляем флаг
    if (entry->is_busy) 
    {
        entry->is_dirty = true;
        return;
    }

    _start_write_transaction(entry);
}

/**
 *  @brief      Старт новой транзакции записи (Контекст: ТОЛЬКО COMMON_WORKER)
 */
static void _start_write_transaction(eeprom_entry_t *entry)
{
    eeprom_device_msg_t msg;
    if (zbus_chan_read(entry->chan, &msg, K_MSEC(10)) != 0) 
    {
        return;
    }

    eeprom_disk_payload_t new_payload;
    memcpy(&new_payload, &msg.serial_number, EEPROM_PAYLOAD_DATA_SIZE);
    new_payload.crc16 = _calc_disk_crc(&new_payload);

    // Если данные на диске идентичны новым — экономим ресурс EEPROM
    if (memcmp(&new_payload, &entry->disk_cache, sizeof(eeprom_disk_payload_t)) == 0) 
    {
        entry->is_dirty = false;
        SERVER_CH_SET_BIT_IF_NEEDED(EEPROM_EVENT_CH, entry->set_ok_bit);
        return;
    }

    entry->disk_cache = new_payload;
    entry->offset     = 0;
    entry->remaining  = sizeof(eeprom_disk_payload_t);
    entry->is_busy    = true;
    entry->is_dirty   = false;

    // Сразу пишем первую страницу
    _write_page_step(entry);
}

/**
 *  @brief      Один шаг постраничной записи (Контекст: ТОЛЬКО COMMON_WORKER)
 */
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
        entry->remaining = 0;
        entry->is_busy   = false;
        entry->is_dirty  = false;        
        SERVER_CH_SET_BIT_IF_NEEDED(EEPROM_EVENT_CH, entry->set_error_bit);
        return;
    }

    entry->offset    += chunk;
    entry->remaining -= chunk;

    if (entry->remaining > 0) 
    {
        // Планируем следующую страницу через 15 мс
        app_worker_reschedule_submit(&entry->write_task, 
                                     K_MSEC(EEPROM_PAGE_WRITE_TIME_MS), 
                                     COMMON_WORKER);
    } 
    else 
    {
        LOG_INF("Data successfully persisted to %s", entry->name);
        SERVER_CH_SET_BIT(EEPROM_EVENT_CH, entry->set_ok_bit);
        SERVER_CH_CLEAR_BIT(EEPROM_EVENT_CH, entry->set_crc_error_bit);
        uint32_t flags = 0;
         SYSTEM_BUS_GET(EEPROM_EVENT_CH,&flags);
         LOG_INF("flalg stat %d", flags);
        // Если пока шла запись, поступили новые данные — запускаем новый цикл
        if (entry->is_dirty) 
        {
            entry->is_dirty = false;
            _start_write_transaction(entry);
        } 
        else 
        {
            entry->is_busy = false;
        }
    }
}

/**
 *  @brief      Хэндлер шага постраничной записи (Контекст: ТОЛЬКО COMMON_WORKER)
 */
static void _eeprom_write_step_handler(struct k_work *_work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(_work);
    eeprom_entry_t *entry = CONTAINER_OF(dwork, eeprom_entry_t, write_task);

    _write_page_step(entry);
}

/**
 *  @brief      Чтение физического чипа EEPROM при инициализации
 */
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
        err = eeprom_read(_entry->dev, 0x00, &disk_data, sizeof(disk_data));
        if (err == 0) 
        {
            if (_calc_disk_crc(&disk_data) == disk_data.crc16) 
            {
                memcpy(&msg.serial_number, &disk_data, EEPROM_PAYLOAD_DATA_SIZE);
                memcpy(&_entry->disk_cache, &disk_data, sizeof(disk_data));
                LOG_INF("%s EEPROM User Data CRC OK (0x%04X)", _entry->name, disk_data.crc16);
            } 
            else 
            {
                LOG_WRN("CRC mismatch on %s user data!", _entry->name);
                memset(&_entry->disk_cache, 0xFF, sizeof(_entry->disk_cache));
                SERVER_CH_SET_BIT(EEPROM_EVENT_CH, _entry->set_crc_error_bit);
            }
        }
        else 
        {
            LOG_ERR("Failed to read user data from %s (err %d)", _entry->name, err);            
        }
    }

    if (zbus_chan_claim(_entry->chan, K_FOREVER) == 0)
    {
        eeprom_device_msg_t *chan_msg = (eeprom_device_msg_t *)zbus_chan_msg(_entry->chan);
        memcpy(chan_msg, &msg, sizeof(msg));
        zbus_chan_finish(_entry->chan);
    }
    
    if (err != 0) 
    {
        SERVER_CH_SET_BIT(EEPROM_ERROR_CH, _entry->error_bit);
    }
    SERVER_CH_SET_MASK(EEPROM_EVENT_CH, _entry->set_ok_bit | _entry->set_error_bit);    
}

/**
 *  @brief      Инициализация менеджера EEPROM
 */
static int _eeprom_mgr_init(void)
{
    uint32_t uids[ARRAY_SIZE(eeprom_entries)] = {0};

    for (size_t i = 0; i < ARRAY_SIZE(eeprom_entries); i++) 
    {
        eeprom_entry_t *entry = &eeprom_entries[i];

        k_work_init_delayable(&entry->debounce_task, _eeprom_debounce_work_handler);
        k_work_init_delayable(&entry->write_task, _eeprom_write_step_handler);

        _read_and_publish_eeprom_chip(entry, &uids[i]);
    }

    LOG_INF("EEPROM Manager initialized: IU UID: 0x%08X, CU UID: 0x%08X", uids[0], uids[1]);
    return 0;
}

SYS_INIT(_eeprom_mgr_init, EEPROM_DEAMON_INIT_LEVEL, EEPROM_DEAMON_INIT_PRIOR);

/***************************************************************************************************
 *                                     PARAMETER ROUTING TABLE
 **************************************************************************************************/
SERVER_CHANNEL_DEFINE(SETTING_IU_ID,        &eeprom1_chan, 0, UINT_DATA, 0);
SERVER_CHANNEL_DEFINE(SETTING_IU_SN,        &eeprom1_chan, 1, UINT_DATA, 100);
SERVER_CHANNEL_DEFINE(SETTING_IU_HW_ID,     &eeprom1_chan, 2, UINT_DATA, 100);
SERVER_CHANNEL_DEFINE(SETTING_IU_HW_VER,    &eeprom1_chan, 3, UINT_DATA, 100);
SERVER_CHANNEL_DEFINE(SETTING_IU_USER_DATA, &eeprom1_chan, 4, UINT_DATA, 100);

SERVER_CHANNEL_DEFINE(SETTING_CU_ID,        &eeprom2_chan, 0, UINT_DATA, 0);
SERVER_CHANNEL_DEFINE(SETTING_CU_SN,        &eeprom2_chan, 1, UINT_DATA, 100);
SERVER_CHANNEL_DEFINE(SETTING_CU_HW_ID,     &eeprom2_chan, 2, UINT_DATA, 100);
SERVER_CHANNEL_DEFINE(SETTING_CU_HW_VER,    &eeprom2_chan, 3, UINT_DATA, 100);
SERVER_CHANNEL_DEFINE(SETTING_CU_USER_DATA, &eeprom2_chan, 4, UINT_DATA, 100);
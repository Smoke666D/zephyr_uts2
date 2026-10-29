/**
 *  @file       eeprom_mgr.c
 *  @headerfile eeprom_mgr.h
 *
 *  @date       2026.10.28
 *  @author     Dymov Igor (Refactored)
 *
 *  @brief      Менеджер энергонезависимой памяти EEPROM
 *  @details    Обеспечивает чтение пользовательских данных и уникальных UID при старте,
 *              проверку целостности по CRC16, публикацию в ZBUS и неблокирующую
 *              постраничную запись в EEPROM с дедупликацией неизменных данных.
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
#define EEPROM_DEBOUNCE_DELAY_MS  10U    // Задержка дебаунсинга записи
#define EEPROM_PAGE_WRITE_TIME_MS 15U    // Интервал между постраничной записью

#define EEPROM_PAYLOAD_DATA_SIZE  (sizeof(eeprom_disk_payload_t) - sizeof(uint16_t))

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

/**
 *  @brief Упакованная структура данных ZBUS-сообщения для каждого устройства EEPROM
 */
struct __packed eeprom_device_msg 
{
    uint32_t factory_uid;                 // Заводской 32-битный UID (адрес 0xFC)
    uint32_t serial_number;               // Пользовательский серийный номер
    uint32_t product_code;
    uint32_t device_class;
    uint32_t device_num;
    uint32_t device_major_ver;
    uint32_t device_minor_ver;
    uint32_t usage_counter;               // Счетчик записей/включений
};
typedef struct eeprom_device_msg eeprom_device_msg_t;

/**
 *  @brief Физическая структура кадра в памяти EEPROM
 */
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
 *  @brief Полный дескриптор устройства EEPROM и контекст асинхронной записи
 */
typedef struct eeprom_entry
{
    const struct device *const dev;        // Устройство EEPROM в DeviceTree
    const struct zbus_channel *chan;       // Связанный канал ZBUS
    struct k_work_delayable    save_task;  // Задача отложенной записи
    off_t                      offset;     // Текущее смещение постраничной записи
    size_t                     remaining;  // Оставшееся количество байт для записи
    eeprom_disk_payload_t      disk_cache; // Локальный кэш данных (для защиты от лишних перезаписей)
    const char                *name;       // Имя устройства ("IU" / "CU")
    uint32_t                   error_bit;  // Бит ошибки инициализации
    uint32_t                   warn_bit;   // Бит предупреждения при сбое записи
    bool                       is_dirty;   // Флаг поступивших новых данных
} eeprom_entry_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void     _eeprom_zbus_listener_callback(const struct zbus_channel *_chan);
static void     _eeprom_save_work_handler(struct k_work *_work);
static uint16_t _calc_disk_crc(const eeprom_disk_payload_t *_disk_data);
static void     _read_and_publish_eeprom_chip(eeprom_entry_t *_entry, uint32_t *_uid_out);
static int      _eeprom_mgr_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/

// Единый листенер Zbus для всех каналов EEPROM
ZBUS_LISTENER_DEFINE(eeprom_lis, _eeprom_zbus_listener_callback);

// Каналы ZBUS
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

// Таблица дескрипторов устройств (устраняет дублирование логики)
static eeprom_entry_t eeprom_entries[] = {
    {
        .dev       = DEVICE_DT_GET(DT_NODELABEL(eeprom_1)),
        .chan      = &eeprom1_chan,
        .name      = "IU",
        .error_bit = EEPORM_IU_ERROR,
        .warn_bit  = EEPORM_IU_WARNING,
        .is_dirty  = false,
    },
    {
        .dev       = DEVICE_DT_GET(DT_NODELABEL(eeprom_2)),
        .chan      = &eeprom2_chan,
        .name      = "CU",
        .error_bit = EEPORM_CU_ERROR,
        .warn_bit  = EEPORM_CU_WARNING,
        .is_dirty  = false,
    }
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Расчет контрольной суммы CRC16 полезной нагрузки
 */
static inline uint16_t _calc_disk_crc(const eeprom_disk_payload_t *_disk_data)
{
    return crc16_itu_t(0xFFFF, (const uint8_t *)_disk_data, EEPROM_PAYLOAD_DATA_SIZE);
}

/**
 *  @brief      Чтение физического чипа EEPROM, инициализация кэша и публикация в Zbus
 */
static void _read_and_publish_eeprom_chip(eeprom_entry_t *_entry, uint32_t *_uid_out)
{
    eeprom_device_msg_t msg = {0};

    if (_uid_out != NULL) 
    {
        *_uid_out = 0;
    }

    if (!device_is_ready(_entry->dev)) 
    {
        LOG_ERR("%s EEPROM device not ready!", _entry->name);
        SYSTEM_BUS_SET_BIT(HARDWARE_ERROR_REGISTER1, _entry->error_bit);
        return;
    }

    int err = eeprom_read(_entry->dev, EEPROM_UID_OFFSET, &msg.factory_uid, sizeof(uint32_t));
    if ((err != 0) || (msg.factory_uid == 0x00)) 
    {
        LOG_ERR("Failed to read Factory UID from %s (err %d)", _entry->name, err);
        SYSTEM_BUS_SET_BIT(HARDWARE_ERROR_REGISTER1, _entry->error_bit);
        return;
    }

    if (_uid_out != NULL) 
    {
        *_uid_out = msg.factory_uid;
    }

    eeprom_disk_payload_t disk_data;
    err = eeprom_read(_entry->dev, 0x00, &disk_data, sizeof(disk_data));
    if (err == 0) 
    {
        if (_calc_disk_crc(&disk_data) == disk_data.crc16) 
        {
            memcpy(&msg.serial_number, &disk_data, EEPROM_PAYLOAD_DATA_SIZE);
            // Сохраняем в кэш: если данные не изменятся, повторной записи не будет
            memcpy(&_entry->disk_cache, &disk_data, sizeof(disk_data));
            LOG_INF("%s EEPROM User Data CRC OK (0x%04X)", _entry->name, disk_data.crc16);
        } 
        else 
        {
            LOG_WRN("CRC mismatch on %s user data!", _entry->name);
            memset(&_entry->disk_cache, 0xFF, sizeof(_entry->disk_cache));
        }
    }
    else 
    {
        LOG_ERR("Failed to read user data from %s (err %d)", _entry->name, err);
        SYSTEM_BUS_SET_BIT(HARDWARE_ERROR_REGISTER1, _entry->error_bit);
    }

    // Публикуем напрямую в Zbus канал без вызова observer notify
    if (zbus_chan_claim(_entry->chan, K_FOREVER) == 0)
    {
        eeprom_device_msg_t *chan_msg = (eeprom_device_msg_t *)zbus_chan_msg(_entry->chan);
        memcpy(chan_msg, &msg, sizeof(msg));
        zbus_chan_finish(_entry->chan);
    }
}

/**
 *  @brief      Листенер с дебаунсингом (задержкой вызова на 10 мс)
 */
static void _eeprom_zbus_listener_callback(const struct zbus_channel *_chan)
{
    for (size_t i = 0; i < ARRAY_SIZE(eeprom_entries); i++) 
    {
        eeprom_entry_t *entry = &eeprom_entries[i];

        if (entry->chan == _chan) 
        {
            entry->is_dirty = true;

            // Если прямо сейчас запись не идет — запускаем дебаунс-таймер.
            // Если запись идет — текущий цикл сам проверит is_dirty по завершении.
            if (entry->remaining == 0) 
            {
                app_worker_reschedule_submit(&entry->save_task, 
                                             K_MSEC(EEPROM_DEBOUNCE_DELAY_MS), 
                                             COMMON_WORKER);
            }
            break;
        }
    }
}

/**
 *  @brief      Универсальный неблокирующий автомат постраничной записи EEPROM
 */
static void _eeprom_save_work_handler(struct k_work *_work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(_work);
    eeprom_entry_t *entry = CONTAINER_OF(dwork, eeprom_entry_t, save_task);

    // Шаг 1: Если начинаем новую транзакцию записи
    if (entry->remaining == 0) 
    {
        eeprom_device_msg_t msg;
        if (zbus_chan_read(entry->chan, &msg, K_NO_WAIT) != 0) 
        {
            return;
        }

        eeprom_disk_payload_t new_payload;
        memcpy(&new_payload, &msg.serial_number, EEPROM_PAYLOAD_DATA_SIZE);
        new_payload.crc16 = _calc_disk_crc(&new_payload);

        // Оптимизация: данные совпадают с текущими на диске — экономим ресурс памяти
        if (memcmp(&new_payload, &entry->disk_cache, sizeof(eeprom_disk_payload_t)) == 0) 
        {
            entry->is_dirty = false;
            return;
        }

        entry->disk_cache = new_payload;
        entry->offset     = 0;
        entry->remaining  = sizeof(eeprom_disk_payload_t);
        entry->is_dirty   = false;
    }

    // Шаг 2: Пишем ровно одну страницу (до 16 байт с учетом границы страницы)
    size_t bytes_to_page_end = EEPROM_PAGE_SIZE - (entry->offset % EEPROM_PAGE_SIZE);
    size_t chunk = MIN(entry->remaining, bytes_to_page_end);

    const uint8_t *src_buf = ((const uint8_t *)&entry->disk_cache) + entry->offset;

    int err = eeprom_write(entry->dev, entry->offset, src_buf, chunk);
    if (err != 0) 
    {
        LOG_ERR("Failed to write %s at offset 0x%02X (err %d)", 
                entry->name, (uint32_t)entry->offset, err);
        entry->remaining = 0;
        entry->is_dirty  = false;
        SYSTEM_BUS_SET_BIT(HARDWARE_WARNING_REG1, entry->warn_bit);
        return;
    }
    SYSTEM_BUS_CLEAR_BIT_IF_NEEDED(HARDWARE_WARNING_REG1, entry->warn_bit);

    entry->offset    += chunk;
    entry->remaining -= chunk;

    // Шаг 3: Проверка остатка данных или новых запросов на запись
    if (entry->remaining > 0) 
    {
        app_worker_reschedule_submit(dwork, K_MSEC(EEPROM_PAGE_WRITE_TIME_MS), COMMON_WORKER);
    } 
    else 
    {
        LOG_INF("Data successfully persisted to %s", entry->name);

        // Если пока шла запись, пришли новые данные — сразу планируем следующий цикл
        if (entry->is_dirty) 
        {
            app_worker_reschedule_submit(dwork, K_MSEC(EEPROM_DEBOUNCE_DELAY_MS), COMMON_WORKER);
        }
    }
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

        k_work_init_delayable(&entry->save_task, _eeprom_save_work_handler);
        _read_and_publish_eeprom_chip(entry, &uids[i]);
    }

    LOG_INF("EEPROM Manager initialized: IU UID: 0x%08X, CU UID: 0x%08X", uids[0], uids[1]);

    return 0;
}

SYS_INIT(_eeprom_mgr_init, EEPROM_DEAMON_INIT_LEVEL, EEPROM_DEAMON_INIT_PRIOR);

/***************************************************************************************************
 *                                     PARAMETER ROUTING TABLE
 **************************************************************************************************/
PARAM_ROUTE_DEFINE(SETTING_IU_ID,        &eeprom1_chan, 0, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_IU_SN,        &eeprom1_chan, 1, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_IU_HW_ID,     &eeprom1_chan, 2, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_IU_HW_VER,    &eeprom1_chan, 3, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_IU_USER_DATA, &eeprom1_chan, 4, UINT_DATA);

PARAM_ROUTE_DEFINE(SETTING_CU_ID,        &eeprom2_chan, 0, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_CU_SN,        &eeprom2_chan, 1, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_CU_HW_ID,     &eeprom2_chan, 2, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_CU_HW_VER,    &eeprom2_chan, 3, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_CU_USER_DATA, &eeprom2_chan, 4, UINT_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
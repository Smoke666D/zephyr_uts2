/**
 *  @file       fram_settings.c
 *  @brief      Менеджер энергонезависимых настроек FRAM на выделенном потоке.
 *              Обеспечивает чтение заводского UID STM32, сохранение настроек MU (SN, HW),
 *              контроль CRC16 и публикацию в Zbus.
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
#include <zephyr/drivers/hwinfo.h>

#include "system_bus_model.h"
#include "system_define.h"

LOG_MODULE_REGISTER(fram_settings, LOG_LEVEL_INF);

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
#define FRAM_DEBOUNCE_DELAY_MS   50U    // Окно агрегации изменений (HW_ID + HW_VER)
#define MAX_PENDING_SYNCS        8      // Буфер одновременных потоков квитирования
#define FRAM_THREAD_STACK_SIZE   2048   // Безопасный размер стека
#define FRAM_THREAD_PRIORITY     10     // Низкий приоритет: не блокирует системные задачи

typedef enum {
    MU_ID_1 = 0,
    MU_ID_2 = 1,
    MU_ID_3 = 2,
    MU_SN   = 3,
    MU_HW_ID = 4,
    MU_HW_VER = 5,
    MU_USER_DATA   = 6,
} uint32_t_setting_t;

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
#pragma pack(push, 1)

typedef struct app_uint32_setting {
    uint32_t value[4]; // 0: SN, 1: HW_ID, 2: HW_VER, 3: резерв
} app_uint32_setting;

typedef struct app_setting_uint_t {
    uint32_t value[7]; // 0..2: STM32 UID, 3: SN, 4: HW_ID, 5: HW_VER, 6: резерв
} app_setting_uint_t;

struct app_settings_frame {
    app_uint32_setting uint32_payload;
    uint16_t           crc16;
};

#pragma pack(pop)

typedef struct {
    struct k_sem *sync;
    uint8_t      *response;
} pending_sync_t;

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const struct device *const fram_dev = DEVICE_DT_GET(DT_NODELABEL(fram_chip));

/* Кэш настроек в оперативной памяти */
static struct app_settings_frame cache_disk_frame;
static struct app_settings_frame cache_staging_frame;

/* Буфер ожидания квитирования */
static pending_sync_t pending_syncs[MAX_PENDING_SYNCS];
static uint8_t        pending_sync_count = 0;

/* Очередь входящих запросов на запись */
K_MSGQ_DEFINE(fram_rx_queue, sizeof(server_request), 10, 4);

/* Канал Zbus */
ZBUS_CHAN_DEFINE(settings_uint32_t_chan,
                 app_setting_uint_t,
                 NULL, 
                 &fram_rx_queue,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

/* Прототип функции потока */
static void _fram_thread_entry(void *p1, void *p2, void *p3);

/* Автономный поток управления FRAM */
K_THREAD_DEFINE(fram_thread_id, 
                FRAM_THREAD_STACK_SIZE,
                _fram_thread_entry, 
                NULL, NULL, NULL,
                FRAM_THREAD_PRIORITY, 
                0, 
                0);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

static inline uint16_t calc_frame_crc(const struct app_settings_frame *frame)
{
    size_t payload_bytes = sizeof(struct app_settings_frame) - sizeof(uint16_t);
    return crc16_itu_t(0xFFFF, (const uint8_t *)frame, payload_bytes);
}

static void _notify_pending(int err)
{
    for (uint8_t i = 0; i < pending_sync_count; i++) 
    {
        if (pending_syncs[i].response != NULL) 
        {
            *pending_syncs[i].response = (uint8_t)err;
        }
        if (pending_syncs[i].sync != NULL) 
        {
            k_sem_give(pending_syncs[i].sync);
        }
    }
    pending_sync_count = 0;
}

static void get_stm32_uid(struct app_setting_uint_t *setting)
{
    uint8_t uid[12];
    ssize_t length = hwinfo_get_device_id(uid, sizeof(uid));

    if (length == 12) {
        setting->value[MU_ID_1] = UNALIGNED_GET((uint32_t *)&uid[0]);
        setting->value[MU_ID_2] = UNALIGNED_GET((uint32_t *)&uid[4]);
        setting->value[MU_ID_3] = UNALIGNED_GET((uint32_t *)&uid[8]);
    } else {
        LOG_ERR("Failed to read STM32 UID, error: %d", (int)length);
    }
}

/**
 *  @brief Публикация полного слепка настроек и UID в канал Zbus
 */
static void _publish_zbus_msg(const struct app_settings_frame *frame)
{
    app_setting_uint_t msg = {0};

    // 1. Аппаратный 96-битный заводской номер чипа STM32H7 (поля 0, 1, 2)
    get_stm32_uid(&msg);

    // 2. Настройки из FRAM (поля 3, 4, 5, 6)
    for (int i = 0; i < 4; i++) {
        msg.value[i + 3] = frame->uint32_payload.value[i];
    }

    zbus_chan_pub(&settings_uint32_t_chan, &msg, K_MSEC(10));
}

/**
 *  @brief Применение одного запроса к staging-кэшу
 */
static void _process_single_request(const server_request *req)
{
    switch (req->id) 
    {
        case SETTING_MU_SN:
            cache_staging_frame.uint32_payload.value[0] = req->value.sd_uint;            
            break;
        case SETTING_MU_HW_ID:
            cache_staging_frame.uint32_payload.value[1] = req->value.sd_uint;            
            break;
        case SETTING_MU_HW_VER:
            cache_staging_frame.uint32_payload.value[2] = req->value.sd_uint;            
            break;
        case SETTING_MU_USER_DATA:
            cache_staging_frame.uint32_payload.value[3] = req->value.sd_uint;            
        default:            
            break;
    }

    // Сохраняем семафор квитирования
    if (req->sync != NULL) 
    {
        if (pending_sync_count < MAX_PENDING_SYNCS) 
        {
            pending_syncs[pending_sync_count].sync = req->sync;
            pending_syncs[pending_sync_count].response = req->response;
            pending_sync_count++;
        } 
        else 
        {
            // Защита от переполнения
            if (req->response != NULL) {
                *req->response = (uint8_t)-ENOMEM;
            }
            k_sem_give(req->sync);
        }
    }
}

/**
 *  @brief Вычитка всех оставшихся сообщений из очереди (неблокирующая)
 */
static void _drain_remaining_queue(void)
{
    server_request req;
    while (k_msgq_get(&fram_rx_queue, &req, K_NO_WAIT) == 0) 
    {
        _process_single_request(&req);
    }
}

/**
 *  @brief Физический сброс данных во FRAM, проверка CRC и обновление Zbus
 */
static void _flush_fram_chip(void)
{
    // Если данные не изменились — подтверждаем успех без лишней записи
    if (memcmp(&cache_staging_frame, &cache_disk_frame, sizeof(struct app_settings_frame)) == 0) 
    {
        _notify_pending(0);
        return;
    }

    // 1. Подсчитываем CRC16
    cache_staging_frame.crc16 = calc_frame_crc(&cache_staging_frame);
    cache_disk_frame = cache_staging_frame;

    LOG_INF("Writing FRAM (size %u), CRC: 0x%04X", 
            (uint32_t)sizeof(cache_disk_frame), cache_disk_frame.crc16);

    int err = 0;
    if (device_is_ready(fram_dev)) 
    {
        err = eeprom_write(fram_dev, 0x0000, &cache_disk_frame, sizeof(cache_disk_frame));
        if (err == 0) 
        {
            // 2. Readback-верификация: контрольная вычитка и проверка CRC
            struct app_settings_frame verify_frame;
            int read_err = eeprom_read(fram_dev, 0x0000, &verify_frame, sizeof(verify_frame));

            if ((read_err == 0) && (calc_frame_crc(&verify_frame) == verify_frame.crc16)) 
            {
                LOG_INF("FRAM written & verified successfully! CRC: 0x%04X", verify_frame.crc16);
                SERVER_CH_CLEAR_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1, FRAM_MU_ERROR);

                // Обновляем Zbus, чтобы SYSTEM_BUS_GET сразу возвращал новые данные
                _publish_zbus_msg(&cache_disk_frame);
            } 
            else 
            {
                LOG_ERR("FRAM readback CRC mismatch! Read: 0x%04X, Expected: 0x%04X", 
                        verify_frame.crc16, cache_disk_frame.crc16);
                SERVER_CH_SET_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1, FRAM_MU_ERROR);
                err = -EIO;
            }
        } 
        else 
        {
            LOG_ERR("FRAM write failed (err %d)", err);
            SERVER_CH_SET_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1, FRAM_MU_ERROR);
        }
    } 
    else 
    {
        LOG_ERR("FRAM device is not ready!");
        err = -ENODEV;
    }

    _notify_pending(err);
}

/**
 *  @brief Основной поток FRAM: старт, инициализация и гарантированный сон без spin-lock
 */
static void _fram_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("FRAM Settings Thread started. Initializing...");

    // 1. ИНИЦИАЛИЗАЦИЯ И ВЫЧИТКА FRAM ПРИ СТАРТЕ ПОТОКА
    if (device_is_ready(fram_dev)) 
    {
        if (eeprom_read(fram_dev, 0x0000, &cache_disk_frame, sizeof(cache_disk_frame)) == 0) 
        {        
            if (calc_frame_crc(&cache_disk_frame) == cache_disk_frame.crc16) 
            {
                cache_staging_frame = cache_disk_frame;
                LOG_INF("FRAM CRC OK (0x%04X). SN=%u, HW_ID=0x%08X, HW_VER=0x%08X",
                        cache_disk_frame.crc16,
                        cache_disk_frame.uint32_payload.value[0],
                        cache_disk_frame.uint32_payload.value[1],
                        cache_disk_frame.uint32_payload.value[2]);
            }
            else 
            {
                LOG_WRN("FRAM CRC mismatch on boot! Clearing cache...");
                memset(&cache_disk_frame, 0x00, sizeof(cache_disk_frame));
                cache_staging_frame = cache_disk_frame;
                SERVER_CH_SET_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1, FRAM_MU_ERROR);
            }       
        }
        else 
        {
            LOG_ERR("Failed to read FRAM on boot!");
            SERVER_CH_SET_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1, FRAM_MU_ERROR);
        }
    } 
    else 
    {
        LOG_ERR("FRAM device is not ready on boot!");
        SERVER_CH_SET_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1, FRAM_MU_ERROR);
    }

    // Публикация стартового слепка в Zbus (UID процессора + уставки)
    _publish_zbus_msg(&cache_disk_frame);

    // 2. БЕСКОНЕЧНЫЙ ЦИКЛ ОБРАБОТКИ
    while (1) 
    {
        server_request first_req;

        // Поток СПИТ НАМЕРТВО здесь, не потребляя такты процессора,
        // пока в очереди действительно не появится хотя бы одно сообщение
        if (k_msgq_get(&fram_rx_queue, &first_req, K_FOREVER) != 0) 
        {
            continue;
        }

        // Обрабатываем первое пришедшее сообщение
        _process_single_request(&first_req);

        // Ждем окно дебаунса 50 мс для объединения HW_ID и HW_VER
        k_msleep(FRAM_DEBOUNCE_DELAY_MS);

        // Выгребаем все сообщения, пришедшие за время дебаунса
        _drain_remaining_queue();

        // Сбрасываем изменения во FRAM
        _flush_fram_chip();
    }
}

/***************************************************************************************************
 *                 МАРШРУТЫ СЕРВЕРА ПАРАМЕТРОВ (ИНДЕКСЫ 0..5)
 **************************************************************************************************/
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_ID_1,      &settings_uint32_t_chan,    MU_ID_1,   UINT_DATA); // 0
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_ID_2,      &settings_uint32_t_chan,    MU_ID_2,   UINT_DATA); // 1
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_ID_3,      &settings_uint32_t_chan,    MU_ID_3,   UINT_DATA); // 2
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_SN,        &settings_uint32_t_chan,    MU_SN,     UINT_DATA); // 3
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_HW_ID,     &settings_uint32_t_chan,    MU_HW_ID,  UINT_DATA); // 4
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_HW_VER,    &settings_uint32_t_chan,    MU_HW_VER, UINT_DATA); // 5
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_USER_DATA, &settings_uint32_t_chan, MU_USER_DATA, UINT_DATA); // 6

typedef struct 
{
    uint32_t  value[3];    
} error_channel_t;

typedef struct 
{
    uint32_t  value[3];    
} warning_channel_t;


/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/


// Предварительная декларация канала ZBU


ZBUS_CHAN_DEFINE(
    error_channel,
    error_channel_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(
    warning_channel,
    warning_channel_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

SERVER_IN_CHANNEL_DEFINE(HARDWARE_ERROR_REGISTER1, &error_channel, 0, UINT_DATA);
SERVER_IN_CHANNEL_DEFINE(HARDWARE_ERROR_REGISTER2, &error_channel, 1, UINT_DATA);
SERVER_IN_CHANNEL_DEFINE(HARDWARE_ERROR_REGISTER3, &error_channel, 2, UINT_DATA);

SERVER_IN_CHANNEL_DEFINE(EVENT_1_REG, &warning_channel, 0, UINT_DATA);
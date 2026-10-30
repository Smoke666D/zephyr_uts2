#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/sys/crc.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <zephyr/drivers/hwinfo.h>
#include "system_bus_model.h"


LOG_MODULE_REGISTER(fram_settings, LOG_LEVEL_INF);

#define FRAM_MAGIC_HEADER   0x53545453 /* Маркер "STTS" */
#define FRAM_VERSION_1      2

typedef enum {
    MU_ID_1 = 0,
    MU_ID_2 = 1,
    MU_ID_3 = 2,
    MU_SN   = 3,
} uint32_t_setting_t;

/* ------------------------------------------------------------------ */
/* ВАЖНО: Упаковываем структуры (#pragma pack), чтобы убрать мусорные байты */
/* ------------------------------------------------------------------ */
#pragma pack(push, 1)

typedef struct app_uint32_setting {
    uint32_t value[4];
} app_uint32_setting;

typedef struct app_float_settings {
    float fault_low_threshold_mv;
    float fault_high_threshold_mv;
    float fault_current_limit_a;        
} app_float_settings;

struct app_settings_frame {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    app_uint32_setting uint32_payload;
    app_float_settings float_payload;
    uint16_t crc16;
};

#pragma pack(pop)

/* Глобальный мастер-кадр уставок в RAM */
static struct app_settings_frame g_settings_frame;

/* Доступ к физической микросхеме FRAM из Devicetree */
static const struct device *const fram_dev = DEVICE_DT_GET(DT_NODELABEL(fram_chip));

/* 1. Объявляем листенер сохранения уставок */
static void settings_listener_fn(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(settings_lis, settings_listener_fn);

/* 2. Объявляем Zbus каналы (каждый под свой строго определенный тип данных) */
ZBUS_CHAN_DEFINE(settings_uint32_t_chan,
                 app_uint32_setting,
                 NULL, NULL,
                 ZBUS_OBSERVERS(settings_lis),
                 {0}
);

ZBUS_CHAN_DEFINE(settings_float_chan,
                 app_float_settings,
                 NULL, NULL,
                 ZBUS_OBSERVERS(settings_lis),
                 {0}
);

/* ------------------------------------------------------------------ */
/* РАСЧЕТ CRC16                                                       */
/* ------------------------------------------------------------------ */
static uint16_t calc_frame_crc(const struct app_settings_frame *frame)
{
    size_t payload_bytes = sizeof(struct app_settings_frame) - sizeof(uint16_t);
    return crc16_itu_t(0xFFFF, (const uint8_t *)frame, payload_bytes);
}

/* ------------------------------------------------------------------ */
/* ЛИСТЕНЕР: СРАБАТЫВАЕТ ПРИ ИЗМЕНЕНИИ ЛЮБОГО ИЗ КАНАЛОВ              */
/* ------------------------------------------------------------------ */
static void settings_listener_fn(const struct zbus_channel *chan)
{
    bool updated = false;

    if (chan == &settings_uint32_t_chan) 
    {    
        /* 
         * ПРАВИЛЬНО: Получаем прямой константный указатель на данные канала.
         * Функция работает мгновенно и без конфликтов мьютексов.
         */
        const struct app_uint32_setting *msg = zbus_chan_const_msg(chan);
        
        if (msg != NULL) {
            /* Копируем данные из безопасного указателя в наш мастер-кадр */
            memcpy(&g_settings_frame.uint32_payload, msg, sizeof(struct app_uint32_setting));
            updated = true;
        }
    } 
    else if (chan == &settings_float_chan) 
    {
        const struct app_float_settings *msg = zbus_chan_const_msg(chan);
        
        if (msg != NULL) {
            memcpy(&g_settings_frame.float_payload, msg, sizeof(struct app_float_settings));
            updated = true;
        }
    }

    if (updated) 
    {
        /* Обновляем CRC16 мастера-кадра */
        g_settings_frame.crc16 = calc_frame_crc(&g_settings_frame);

        /* Физически сохраняем во FRAM и обязательно проверяем статус ошибки */
        if (device_is_ready(fram_dev)) {
            int err = eeprom_write(fram_dev, 0x0000, &g_settings_frame, sizeof(g_settings_frame));
            if (err == 0) 
            {
                LOG_INF("Settings successfully written to FRAM.");
                SERVER_CH_CLEAR_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1,FRAM_MU_ERROR);
            } 
            else 
            {
                LOG_ERR("EEPROM WRITE FAILED! Error code: %d", err);
                SERVER_CH_SET_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1,FRAM_MU_ERROR);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* ВНОМОГАТЕЛЬНЫЕ ФУНКЦИИ                                             */
/* ------------------------------------------------------------------ */
static void get_stm32_uid(app_uint32_setting *setting)
{
    uint8_t uid[12];
    ssize_t length = hwinfo_get_device_id(uid, sizeof(uid));

    if (length == 12) {
        setting->value[MU_ID_1] = UNALIGNED_GET((uint32_t *)&uid[0]);
        setting->value[MU_ID_2] = UNALIGNED_GET((uint32_t *)&uid[4]);
        setting->value[MU_ID_3] = UNALIGNED_GET((uint32_t *)&uid[8]);
    } else {
        LOG_ERR("Failed to read UID, error: %d", (int)length);
    }
}

/* ------------------------------------------------------------------ */
/* ИНИЦИАЛИЗАЦИЯ И ЗАГРУЗКА ИЗ FRAM ПРИ СТАРТЕ СИСТЕМЫ                 */
/* ------------------------------------------------------------------ */
static int fram_settings_init(void)
{
    if (!device_is_ready(fram_dev)) {
        LOG_ERR("FRAM device not ready during boot!");
        return -ENODEV;
    }

    bool valid_data_loaded = false;

    /* Читаем полный мастер-кадр из FRAM */
    if (eeprom_read(fram_dev, 0x0000, &g_settings_frame, sizeof(g_settings_frame)) == 0) 
    {
        if (g_settings_frame.magic == FRAM_MAGIC_HEADER) 
        {
            if (calc_frame_crc(&g_settings_frame) == g_settings_frame.crc16) 
            {
                valid_data_loaded = true;
                LOG_INF("Valid settings loaded from FRAM successfully!");
            }
        }
        SERVER_CH_CLEAR_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1,FRAM_MU_ERROR);
    }
    else 
    {
        SERVER_CH_SET_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1,FRAM_MU_ERROR);
        return -ENODEV;
    }

    /* Если FRAM чист или CRC не совпал — инициализируем заводские дефолты */
    if (!valid_data_loaded) {
        LOG_WRN("FRAM uninitialized or CRC invalid! Loading factory defaults...");
        
        g_settings_frame.magic = FRAM_MAGIC_HEADER;
        g_settings_frame.version = FRAM_VERSION_1;
        g_settings_frame.length = sizeof(g_settings_frame.uint32_payload) + sizeof(g_settings_frame.float_payload);
        
        g_settings_frame.uint32_payload.value[MU_SN] = 0x01;
        get_stm32_uid(&g_settings_frame.uint32_payload);

        g_settings_frame.float_payload.fault_low_threshold_mv = 1500.0f;
        g_settings_frame.float_payload.fault_high_threshold_mv = 10000.0f;
        g_settings_frame.float_payload.fault_current_limit_a = 2.5f;

        /* Считаем CRC16 для дефолтов */
        g_settings_frame.crc16 = calc_frame_crc(&g_settings_frame);

        /* ИНИЦИАЛИЗИРУЕМ FRAM И ОБЯЗАТЕЛЬНО ПРОВЕРЯЕМ ОШИБКУ */
        int write_err = eeprom_write(fram_dev, 0x0000, &g_settings_frame, sizeof(g_settings_frame));
        if (write_err != 0) 
        {
            LOG_ERR("CRITICAL: Failed to write initial defaults to FRAM! Code: %d", write_err);
            SERVER_CH_SET_BIT_IF_NEEDED(HARDWARE_ERROR_REGISTER1,FRAM_MU_ERROR);
        } 
        else 
        {
            LOG_INF("Initial factory defaults written to FRAM.");
        }
    }

    /* 
     * Копируем прочитанные валидные данные в соответствующие Zbus каналы БЕЗ УВЕДОМЛЕНИЯ (без notify).
     * Каждому каналу передаем СТРОГО ЕГО ТИП ДАННЫХ (без выходов за границы буферов!)
     */
    if (zbus_chan_claim(&settings_uint32_t_chan, K_FOREVER) == 0)
    {
        app_uint32_setting *chan_msg = (app_uint32_setting *)zbus_chan_msg(&settings_uint32_t_chan);
        memcpy(chan_msg, &g_settings_frame.uint32_payload, sizeof(app_uint32_setting));
        zbus_chan_finish(&settings_uint32_t_chan);
    }

    if (zbus_chan_claim(&settings_float_chan, K_FOREVER) == 0) {
        app_float_settings *chan_msg = (app_float_settings *)zbus_chan_msg(&settings_float_chan);
        memcpy(chan_msg, &g_settings_frame.float_payload, sizeof(app_float_settings));
        zbus_chan_finish(&settings_float_chan);
    }

    LOG_INF("Settings Zbus channels initialized successfully.");
    return 0;
}

SYS_INIT(fram_settings_init, POST_KERNEL, 80);


/* ------------------------------------------------------------------ */
/* МАРШРУТЫ СЕРВЕРА ПАРАМЕТРОВ                                        */
/* ------------------------------------------------------------------ */
PARAM_ROUTE_DEFINE(SETTING_MU_ID_1, &settings_uint32_t_chan, 0, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_MU_ID_2, &settings_uint32_t_chan, 1, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_MU_ID_3, &settings_uint32_t_chan, 2, UINT_DATA);
PARAM_ROUTE_DEFINE(SETTING_MU_SN,   &settings_uint32_t_chan, 3, UINT_DATA);


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

PARAM_ROUTE_DEFINE(HARDWARE_ERROR_REGISTER1, &error_channel, 0, UINT_DATA);
PARAM_ROUTE_DEFINE(HARDWARE_ERROR_REGISTER2, &error_channel, 1, UINT_DATA);
PARAM_ROUTE_DEFINE(HARDWARE_ERROR_REGISTER3, &error_channel, 2, UINT_DATA);

PARAM_ROUTE_DEFINE(EVENT_1_REG, &warning_channel, 0, UINT_DATA);
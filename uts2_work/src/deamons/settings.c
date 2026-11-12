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
#include "app_worker.h"

LOG_MODULE_REGISTER(fram_settings, LOG_LEVEL_INF);


typedef enum {
    MU_ID_1 = 0,
    MU_ID_2 = 1,
    MU_ID_3 = 2,
    MU_SN   = 3,
    MU_HW_ID = 4,
    MU_HW_VER = 5,
} uint32_t_setting_t;

/* ------------------------------------------------------------------ */
/* ВАЖНО: Упаковываем структуры (#pragma pack), чтобы убрать мусорные байты */
/* ------------------------------------------------------------------ */
#pragma pack(push, 1)

typedef struct app_uint32_setting {
    uint32_t value[4];
} app_uint32_setting;

typedef struct app_setting_uint_t
{
    uint32_t value[7];
} app_setting_uint_t;

struct app_settings_frame 
{
    app_uint32_setting uint32_payload;
    uint16_t crc16;
};

#pragma pack(pop)

/* Глобальный мастер-кадр уставок в RAM */
static struct app_settings_frame cashe_settings_frame;

/* Доступ к физической микросхеме FRAM из Devicetree */
static const struct device *const fram_dev = DEVICE_DT_GET(DT_NODELABEL(fram_chip));


static struct k_work_poll  fram_ctl_task                         __attribute__((section("DTCM")));
static struct k_poll_event fram_ctl_poll_events[1]               __attribute__((section("DTCM")));

K_MSGQ_DEFINE(fram_rx_queue, sizeof(server_request), 10, 4);


/* 2. Объявляем Zbus каналы (каждый под свой строго определенный тип данных) */
ZBUS_CHAN_DEFINE(settings_uint32_t_chan,
                 app_setting_uint_t,
                 NULL, 
                 &fram_rx_queue,
                 ZBUS_OBSERVERS_EMPTY,
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
static void  fram_ctl_update_handler(struct k_work *_work)
{
     ARG_UNUSED(_work);
     server_request req;
    int err = 0;

     while (k_msgq_get(&fram_rx_queue, &req, K_NO_WAIT) == 0) 
     {
            switch (req.id) 
            {
                case SETTING_MU_SN:
                    cashe_settings_frame.uint32_payload.value[0] = req.value.sd_uint;
                    break;
                case SETTING_MU_HW_ID:
                    cashe_settings_frame.uint32_payload.value[1] = req.value.sd_uint;
                    break;
                case SETTING_MU_HW_VER:
                    cashe_settings_frame.uint32_payload.value[2] = req.value.sd_uint;
                    break;
                default:
                    return;                
            }
     }
    
    
        /* Обновляем CRC16 мастера-кадра */
        cashe_settings_frame.crc16 = calc_frame_crc(&cashe_settings_frame);

        /* Физически сохраняем во FRAM и обязательно проверяем статус ошибки */
        if (device_is_ready(fram_dev)) {
            err = eeprom_write(fram_dev, 0x0000, &cashe_settings_frame, sizeof(cashe_settings_frame));
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
        
    
    if (req.sync != NULL)
    {
       if (req.response != NULL)
        {
            *req.response = err;
        }
        k_sem_give(req.sync);
    }
    fram_ctl_poll_events[0].state = K_POLL_STATE_NOT_READY;
     app_worker_poll_submit(&fram_ctl_task, fram_ctl_poll_events,ARRAY_SIZE(fram_ctl_poll_events), COMMON_WORKER);
}

/* ------------------------------------------------------------------ */
/* ВНОМОГАТЕЛЬНЫЕ ФУНКЦИИ                                             */
/* ------------------------------------------------------------------ */
static void get_stm32_uid(struct app_setting_uint_t *setting)
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

    k_poll_event_init(&fram_ctl_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &fram_rx_queue); 

    k_work_poll_init(&fram_ctl_task, fram_ctl_update_handler); 
    app_worker_poll_submit(&fram_ctl_task, fram_ctl_poll_events,ARRAY_SIZE(fram_ctl_poll_events), COMMON_WORKER);

    app_setting_uint_t msg = {0};
    get_stm32_uid(&msg);

    /* Читаем полный мастер-кадр из FRAM */
    if (eeprom_read(fram_dev, 0x0000, &cashe_settings_frame, sizeof(cashe_settings_frame)) == 0) 
    {        
        if (calc_frame_crc(&cashe_settings_frame) == cashe_settings_frame.crc16) 
        {
            for (int i = 0; i < 4; i++)
            {
                msg.value[i + 3] =  cashe_settings_frame.uint32_payload.value[i];
            }
        }
        else 
        {
            for (int i = 0; i < 4; i++)
            {
                cashe_settings_frame.uint32_payload.value[i] = 0x00;
                msg.value[i + 3] =  0x00;
            }
        }       
    }
    zbus_chan_pub(&settings_uint32_t_chan, &msg, K_MSEC(10)); 

    LOG_INF("Settings Zbus channels initialized successfully.");
    return 0;
}

SYS_INIT(fram_settings_init, POST_KERNEL, 80);


/* ------------------------------------------------------------------ */
/* МАРШРУТЫ СЕРВЕРА ПАРАМЕТРОВ                                        */
/* ------------------------------------------------------------------ */

    
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_ID_1,   &settings_uint32_t_chan, MU_ID_1, UINT_DATA);
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_ID_2,   &settings_uint32_t_chan, MU_ID_2, UINT_DATA);
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_ID_3,   &settings_uint32_t_chan, MU_ID_3, UINT_DATA);
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_SN,     &settings_uint32_t_chan, MU_SN, UINT_DATA);
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_HW_ID,  &settings_uint32_t_chan, MU_HW_ID, UINT_DATA);
SERVER_IN_CHANNEL_DEFINE(SETTING_MU_HW_VER, &settings_uint32_t_chan, MU_HW_VER, UINT_DATA);

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
#include <stdio.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include "system_bus_model.h"
#include "app_worker.h"
#include "scpi/scpi.h"

#define MONITOR_MIN_INTERVAL_MS 10U    // Защита: не чаще 10 мс (100 Гц), чтобы не перегружать UART
#define MONITOR_MAX_INTERVAL_MS 60000U // Максимум 60 секунд

/* Словарь доступных каналов АЦП стенда */
static const scpi_choice_def_t adc_channel_choices[] = {
    {"AIN_AO1",        AIN_AO1},
    {"AIN_AO2",        AIN_AO2},
    {"AIN_AO3",        AIN_AO3},
    {"AIN_AO4",        AIN_AO4},
    {"AIN_AO5",        AIN_AO5},
    {"AIN_AO6",        AIN_AO6},
    {"AIN_AO7",        AIN_AO7},
    {"AIN_AO8",        AIN_AO8},
    {"AIN_AO9",        AIN_AO9},
    {"AIN_AO10",       AIN_AO10},
    {"AIN_AO11",       AIN_AO11},
    {"AIN_AO12",       AIN_AO12},
    {"AIN_AO13",       AIN_AO13},
    {"AIN_AO14",       AIN_AO14},
    {"AIN_AO15",       AIN_AO15},
    {"AIN_AO16",       AIN_AO16},
    {"AIN_AO17",       AIN_AO17},
    {"AIN_AO18",       AIN_AO18},

    {"AIN_AVsense1",   AIN_AVsense1},
    {"AIN_AVsense2",   AIN_AVsense2},
    {"AIN_AVsense3",   AIN_AVsense3},
    {"AIN_AVsense4",   AIN_AVsense4},
    {"AIN_AVsense5",   AIN_AVsense5},
    {"AIN_AVsense6",   AIN_AVsense6},

    {"LIN_AO1",        LIN_AO1},
    {"LIN_AO2",        LIN_AO2},
    {"LIN_AO3",        LIN_AO3},
    {"LIN_AO4",        LIN_AO4},

    {"ENV_P3V3",       ENV_P3V3},
    {"ENV_P5V0",       ENV_P5V0},
    {"ENV_VIN",        ENV_VIN},
    {"ENV_VDOUT1",     ENV_VDOUT1},
    {"ENV_P40V",       ENV_P40V},
    {"ENV_USB",        ENV_USB},
    {"ENV_VDOUT2",     ENV_VDOUT2},
    {"ENV_VDOUT3",     ENV_VDOUT3},

    {"BRD_LOW_CUR", SENS_BRD_LOW_CURRENT},
    {"BRD_LOW_VOL", SENS_BRD_LOW_VOLTAGE},
    {"BRD_LOW_PWR", SENS_I2C_POWER_BRD_LOW },
    {"BRD_HIGH_CUR", SENS_BRD_HIGH_CURRENT},
    {"BRD_HIGH_VOL", SENS_BRD_HIGH_VOLTAGE},
    {"BRD_HIGH_PWR", SENS_I2C_POWER_BRD_HIGH },
    {"VDUT2_CUR",SENS_VDUT2_CURRENT},
    {"VDUT2_VOL",SENS_VDUT2_VOLTAGE}, 
    {"VDUT2_PWR", SENS_I2C_POWER_VDUT2},
    {"VDUT3_CUR",SENS_VDUT3_CURRENT},
    {"VDUT3_VOL",SENS_VDUT3_VOLTAGE},
    {"VDUT3_PWR",SENS_I2C_POWER_VDUT3},
    {"VIN_CUR", SENS_VIN_CURRENT},
    {"VIN_VOL", SENS_VIN_VOLTAGE}, 
    {"VIN_PWR", SENS_I2C_POWER_VIN },
    {"DCDC_3_3_CUR",  SENS_DCDC_3_3_CURRENT},
    {"DCDC_3_3_VOL",  SENS_DCDC_3_3_VOLTAGE},
    {"DCDC_3_3_PWR",  SENS_I2C_POWER_DCDC_3_3 },
    {"VDOUT_PWR_CUR", SENS_VDOUT_PWR_CURRENT},
    {"VDOUT_PWR_VOL", SENS_VDOUT_PWR_VOLTAGE},
    {"VDOUT_PWR_PWR", SENS_I2C_POWER_VDOUT_PWR},
    {"VD5_CUR", SENS_VD5_CURRENT},
    {"VD5_VOL", SENS_VD5_VOLTAGE}, 
    {"VD5_PWR",SENS_I2C_POWER_VD5 },
    SCPI_CHOICE_LIST_END
};

#define MONITOR_MAX_CHANNELS    ARRAY_SIZE(adc_channel_choices)

/* Внешняя функция отправки строки из spci_uts2.c */
extern void scpi_async_send_text(const char *text);

/* Состояние службы фонового мониторинга */
static struct {
    bool                    is_running;
    uint32_t                interval_ms;
    int32_t                 channels[MONITOR_MAX_CHANNELS];
    size_t                  channel_count;
    struct k_work_delayable work;
} monitor_ctx;

/* ========================================================================= */
/* ОБРАБОТЧИК ВОРКЕРА (Выполняется в контексте APPLICATION_WORKER)           */
/* ========================================================================= */

static void _monitor_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!monitor_ctx.is_running) {
        return;
    }

    char stream_buf[256];
    int offset = 0;

    // Префикс пакета телеметрии
    offset += snprintf(stream_buf + offset, sizeof(stream_buf) - offset, "#MON: ");

    // Считываем значения всех выбранных каналов через SYSTEM_BUS_GET
    for (size_t i = 0; i < monitor_ctx.channel_count; i++) 
    {
        float volt = 0.0f;
        SYSTEM_BUS_GET(monitor_ctx.channels[i], &volt);

        offset += snprintf(stream_buf + offset, sizeof(stream_buf) - offset,
                           (i == 0) ? "%.3f" : ",%.3f", (double)volt);

        // Защита от переполнения буфера вывода
        if (offset >= (int)sizeof(stream_buf) - 8) {
            break;
        }
    }

    // Завершение строки переводами CRLF
    snprintf(stream_buf + offset, sizeof(stream_buf) - offset, "\r\n");

    // Отправка строки в UART (потокобезопасно, т.к. мы в том же воркере)
    scpi_async_send_text(stream_buf);

    // Планируем следующий замер
    if (monitor_ctx.is_running) {
        app_worker_reschedule_submit(&monitor_ctx.work, 
                                     K_MSEC(monitor_ctx.interval_ms), 
                                     APPLICATION_WORKER);
    }
}

/* Инициализация модуля при старте стенда */
void scpi_monitor_init(void)
{
    monitor_ctx.is_running = false;
    monitor_ctx.channel_count = 0;
    monitor_ctx.interval_ms = 100;
    k_work_init_delayable(&monitor_ctx.work, _monitor_work_handler);
}

/* ========================================================================= */
/* SCPI-КОЛБЭКИ                                                              */
/* ========================================================================= */

// MONITor[:VOLTage]:STARt <interval_ms>, <ch1>[, <ch2>, ...]
scpi_result_t cmd_monitor_start(scpi_t *context)
{
    int32_t interval_ms;
    int32_t ch;

    // 1. Считываем интервал в миллисекундах целым числом (например: 50, 100, 500)
    if (!SCPI_ParamInt32(context, &interval_ms, TRUE)) {
        return SCPI_RES_ERR;
    }

    // Защита: от 10 мс до 60 000 мс (60 с)
    if (interval_ms < (int32_t)MONITOR_MIN_INTERVAL_MS || interval_ms > (int32_t)MONITOR_MAX_INTERVAL_MS) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // -222
        return SCPI_RES_ERR;
    }

    // 2. Считываем список каналов через запятую
    size_t count = 0;
    while (SCPI_ParamChoice(context, adc_channel_choices, &ch, FALSE)) {
        if (count < MONITOR_MAX_CHANNELS) {
            monitor_ctx.channels[count++] = ch;
        }
    }

    // Если ни один канал не указан
    if (count == 0) {
        SCPI_ErrorPush(context, SCPI_ERROR_MISSING_PARAMETER); // -109
        return SCPI_RES_ERR;
    }

    // 3. Сбрасываем предыдущую работу
    monitor_ctx.is_running = false;
    k_work_cancel_delayable(&monitor_ctx.work);

    // 4. Применяем новые параметры и запускаем
    monitor_ctx.interval_ms = (uint32_t)interval_ms;
    monitor_ctx.channel_count = count;
    monitor_ctx.is_running = true;

    // Первый замер запускаем немедленно
    app_worker_reschedule_submit(&monitor_ctx.work, K_NO_WAIT, APPLICATION_WORKER);

    return SCPI_RES_OK;
}

// MONITor[:VOLTage]:STOP
scpi_result_t cmd_monitor_stop(scpi_t *context)
{
    (void)context;

    monitor_ctx.is_running = false;
    k_work_cancel_delayable(&monitor_ctx.work);

    return SCPI_RES_OK;
}

// MONITor[:VOLTage]:STATe?
scpi_result_t cmd_monitor_state_q(scpi_t *context)
{
    SCPI_ResultBool(context, monitor_ctx.is_running);
    return SCPI_RES_OK;
}

// Сброс по *RST: гарантированная остановка фоновой телеметрии
scpi_result_t cmd_monitor_reset(scpi_t *context)
{
    return cmd_monitor_stop(context);
}
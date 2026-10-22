#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "system_bus_model.h"

#define MONITOR_MAX_CHANNELS 32

/* Контекст работы монитора */
struct monitor_work_context {
    const struct shell *sh;
    int scan_time_ms;
    int channel_count;
    char channel_names[MONITOR_MAX_CHANNELS][16];
    struct k_work_delayable work;   
    bool active;                    
};

static struct monitor_work_context current_monitor;
static bool work_is_initialized = false;

/* Универсальная функция получения значения канала по его текстовому имени */
static float get_channel_value_by_name(const char *name)
{
    float val = 0.0f;

    // --- Группа ENV ---
    if      (strcmp(name, "ENV_P3V3") == 0)   { SYSTEM_BUS_GET(ENV_P3V3,   &val); }
    else if (strcmp(name, "ENV_P5V0") == 0)   { SYSTEM_BUS_GET(ENV_P5V0,   &val); }
    else if (strcmp(name, "ENV_VIN") == 0)    { SYSTEM_BUS_GET(ENV_VIN,    &val); }
    else if (strcmp(name, "ENV_VDOUT1") == 0) { SYSTEM_BUS_GET(ENV_VDOUT1, &val); }
    else if (strcmp(name, "ENV_P40V") == 0)   { SYSTEM_BUS_GET(ENV_P40V,   &val); }
    else if (strcmp(name, "ENV_USB") == 0)    { SYSTEM_BUS_GET(ENV_USB,    &val); }
    else if (strcmp(name, "ENV_VDOUT2") == 0) { SYSTEM_BUS_GET(ENV_VDOUT2, &val); }
    else if (strcmp(name, "ENV_VDOUT3") == 0) { SYSTEM_BUS_GET(ENV_VDOUT3, &val); }
    
    // --- Группа AVSense (1-6) ---
    else if (strcmp(name, "AVsense1") == 0 || strcmp(name, "AVsense1") == 0) { SYSTEM_BUS_GET(AIN_AVsense1, &val); }
    else if (strcmp(name, "AVsense2") == 0) { SYSTEM_BUS_GET(AIN_AVsense2, &val); }
    else if (strcmp(name, "AVsense3") == 0) { SYSTEM_BUS_GET(AIN_AVsense3, &val); }
    else if (strcmp(name, "AVsense4") == 0) { SYSTEM_BUS_GET(AIN_AVsense4, &val); }
    else if (strcmp(name, "AVsense5") == 0) { SYSTEM_BUS_GET(AIN_AVsense5, &val); }
    else if (strcmp(name, "AVsense6") == 0) { SYSTEM_BUS_GET(AIN_AVsense6, &val); }

    // --- Группа LIN_AO (1-4) ---
    else if (strcmp(name, "LIN_AO1") == 0)  { SYSTEM_BUS_GET(LIN_AO1, &val); }
    else if (strcmp(name, "LIN_AO2") == 0)  { SYSTEM_BUS_GET(LIN_AO2, &val); }
    else if (strcmp(name, "LIN_AO3") == 0)  { SYSTEM_BUS_GET(LIN_AO3, &val); }
    else if (strcmp(name, "LIN_AO4") == 0)  { SYSTEM_BUS_GET(LIN_AO4, &val); }

    // --- Группа AO (1-18) ---
    else if (strcmp(name, "AO1") == 0)   { SYSTEM_BUS_GET(AIN_AO1,  &val); }
    else if (strcmp(name, "AO2") == 0)   { SYSTEM_BUS_GET(AIN_AO2,  &val); }
    else if (strcmp(name, "AO3") == 0)   { SYSTEM_BUS_GET(AIN_AO3,  &val); }
    else if (strcmp(name, "AO4") == 0)   { SYSTEM_BUS_GET(AIN_AO4,  &val); }
    else if (strcmp(name, "AO5") == 0)   { SYSTEM_BUS_GET(AIN_AO5,  &val); }
    else if (strcmp(name, "AO6") == 0)   { SYSTEM_BUS_GET(AIN_AO6,  &val); }
    else if (strcmp(name, "AO7") == 0)   { SYSTEM_BUS_GET(AIN_AO7,  &val); }
    else if (strcmp(name, "AO8") == 0)   { SYSTEM_BUS_GET(AIN_AO8,  &val); }
    else if (strcmp(name, "AO9") == 0)   { SYSTEM_BUS_GET(AIN_AO9,  &val); }
    else if (strcmp(name, "AO10") == 0)  { SYSTEM_BUS_GET(AIN_AO10, &val); }
    else if (strcmp(name, "AO11") == 0)  { SYSTEM_BUS_GET(AIN_AO11, &val); }
    else if (strcmp(name, "AO12") == 0)  { SYSTEM_BUS_GET(AIN_AO12, &val); }
    else if (strcmp(name, "AO13") == 0)  { SYSTEM_BUS_GET(AIN_AO13, &val); }
    else if (strcmp(name, "AO14") == 0)  { SYSTEM_BUS_GET(AIN_AO14, &val); }
    else if (strcmp(name, "AO15") == 0)  { SYSTEM_BUS_GET(AIN_AO15, &val); }
    else if (strcmp(name, "AO16") == 0)  { SYSTEM_BUS_GET(AIN_AO16, &val); }
    else if (strcmp(name, "AO17") == 0)  { SYSTEM_BUS_GET(AIN_AO17, &val); }
    else if (strcmp(name, "AO18") == 0)  { SYSTEM_BUS_GET(AIN_AO18, &val); }

    else if (strcmp(name, "TEMP1") == 0) { SYSTEM_BUS_GET(I2C1_TEMP1, &val); }
    else if (strcmp(name, "TEMP2") == 0) { SYSTEM_BUS_GET(I2C1_TEMP2, &val); }

    else if (strcmp(name, "LUX1") == 0)  { SYSTEM_BUS_GET(I2C1_LUX1,  &val); }
    else if (strcmp(name, "LUX2") == 0)  { SYSTEM_BUS_GET(I2C1_LUX2,  &val); }

    else if (strcmp(name, "LED1") == 0)  { bool b = false; SYSTEM_BUS_GET(LED1, &b); val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "LED2") == 0)  { bool b = false; SYSTEM_BUS_GET(LED2, &b); val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "LED3") == 0)  { bool b = false; SYSTEM_BUS_GET(LED3, &b); val = b ? 1.0f : 0.0f; }

    else if (strcmp(name, "EN_DUT2_PSU") == 0) { bool b = false; SYSTEM_BUS_GET(EN_DUT2_PSU, &b); val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "EN_DUT3_PSU") == 0) { bool b = false; SYSTEM_BUS_GET(EN_DUT3_PSU, &b); val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "EN_P12V") == 0)     { bool b = false; SYSTEM_BUS_GET(EN_P12V, &b);     val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "EN_P24V") == 0)     { bool b = false; SYSTEM_BUS_GET(EN_P24V, &b);     val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "EN_VA") == 0)       { bool b = false; SYSTEM_BUS_GET(EN_VA, &b);       val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "EN_USB_OUT") == 0)  { bool b = false; SYSTEM_BUS_GET(EN_USB_OUT, &b);  val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "EN_USB_BOOT") == 0) { bool b = false; SYSTEM_BUS_GET(EN_USB_BOOT, &b); val = b ? 1.0f : 0.0f; }
    else if (strcmp(name, "EN_USB_TOP") == 0)  { bool b = false; SYSTEM_BUS_GET(EN_USB_TOP, &b);  val = b ? 1.0f : 0.0f; }

    return val;
}

static void monitor_work_handler(struct k_work *item)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(item);
    struct monitor_work_context *ctx = CONTAINER_OF(dwork, struct monitor_work_context, work);

    if (!ctx->active) {
        return;
    }

    // 1. Получаем время работы системы в миллисекундах
    int64_t uptime_ms = k_uptime_get();
    int ms = uptime_ms % 1000;
    int total_sec = uptime_ms / 1000;
    int sec = total_sec % 60;
    int total_min = total_sec / 60;
    int min = total_min % 60;
    int hr = total_min / 60;

    // 2. Формируем общую строку: сначала метка времени, затем каналы
    char line_buf[512] = {0};
    int offset = snprintf(line_buf, sizeof(line_buf), "[%02d:%02d:%02d.%03d] ", hr, min, sec, ms);

    // Дописываем значения запрошенных каналов в заданном пользователем порядке
    for (int i = 0; i < ctx->channel_count; i++) {
        float val = get_channel_value_by_name(ctx->channel_names[i]);
        int written = 0;

        if (strncmp(ctx->channel_names[i], "TEMP", 4) == 0) {
            // Для температур (TEMP1, TEMP2) используем %f и (double)
            written = snprintf(line_buf + offset, sizeof(line_buf) - offset, 
                                   "%s: %.2f °C   ", ctx->channel_names[i], (double)val);
        } 
        else if (strncmp(ctx->channel_names[i], "LUX", 3) == 0) {
            // Для освещенности (LUX1, LUX2) используем %f и (double)
            written = snprintf(line_buf + offset, sizeof(line_buf) - offset, 
                                   "%s: %.2f lx   ", ctx->channel_names[i], (double)val);
        } 
         else if (strncmp(ctx->channel_names[i], "LED", 3) == 0 || strncmp(ctx->channel_names[i], "EN_", 3) == 0) {
            // Для светодиодов (LED1-3) и силовых ключей (EN_*) выводим ON / OFF
            const char *state_str = (val >= 0.5f) ? "ON" : "OFF";
            written = snprintf(line_buf + offset, sizeof(line_buf) - offset, 
                                   "%s: %s   ", ctx->channel_names[i], state_str);
        } 
        else {
            // Для остальных каналов (ENV, Sense, AO, LIN_AO) — стандартный %f с (double)
            written = snprintf(line_buf + offset, sizeof(line_buf) - offset, 
                                   "%s: %.4f V   ", ctx->channel_names[i], (double)val);
        }

        if (written > 0) {
            offset += written;
        }
    }

    // Выводим готовую строку в шелл
    shell_print(ctx->sh, "%s", line_buf);

    // Перепланируем следующий запуск
    if (ctx->active) {
        k_work_reschedule(&ctx->work, K_MSEC(ctx->scan_time_ms));
    }
}

/* Команда: monitor start <scan_time_ms> <chan_1> [chan_2] ... */
static int cmd_monitor_start(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: monitor start <scan_time_ms> <chan_1> [chan_2] ...");
        return -EINVAL;
    }

    if (current_monitor.active) {
        current_monitor.active = false;
        k_work_cancel_delayable(&current_monitor.work);
    }

    int scan_time = atoi(argv[1]);
    if (scan_time < 100) {
        shell_error(sh, "Error: scan_time must be at least 100 ms.");
        return -EINVAL;
    }

    int chan_count = argc - 2;
    if (chan_count > MONITOR_MAX_CHANNELS) {
        shell_error(sh, "Error: Too many channels (max %d).", MONITOR_MAX_CHANNELS);
        return -EINVAL;
    }

    current_monitor.sh = sh;
    current_monitor.scan_time_ms = scan_time;
    current_monitor.channel_count = chan_count;
    current_monitor.active = true;

    for (int i = 0; i < chan_count; i++) {
        strncpy(current_monitor.channel_names[i], argv[i + 2], sizeof(current_monitor.channel_names[i]) - 1);
        current_monitor.channel_names[i][sizeof(current_monitor.channel_names[i]) - 1] = '\0';
    }

    if (!work_is_initialized) {
        k_work_init_delayable(&current_monitor.work, monitor_work_handler);
        work_is_initialized = true;
    }

    k_work_reschedule(&current_monitor.work, K_MSEC(current_monitor.scan_time_ms));

    shell_print(sh, "Monitor started. Type 'monitor stop' to halt.");
    return 0;
}

/* Команда: monitor stop */
static int cmd_monitor_stop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!current_monitor.active) {
        shell_warn(sh, "Monitor is not running.");
        return 0;
    }

    current_monitor.active = false;
    k_work_cancel_delayable(&current_monitor.work);

    shell_print(sh, "Monitor stopped.");
    return 0;
}

/* Регистрация подкоманд */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_monitor,
    SHELL_CMD_ARG(start, NULL, "Start monitoring channels.\nUsage: monitor start <scan_time_ms> <chan_1> ...", cmd_monitor_start, 3, MONITOR_MAX_CHANNELS),
    SHELL_CMD_ARG(stop,  NULL, "Stop active monitoring.\nUsage: monitor stop", cmd_monitor_stop, 1, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(monitor, &sub_monitor, "Monitor analog channels subsystem", NULL);
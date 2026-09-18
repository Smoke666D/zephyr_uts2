#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fram_settings, LOG_LEVEL_INF);

#define FRAM_NODE DT_NODELABEL(fram_chip)
static const struct device *fram_dev = DEVICE_DT_GET(FRAM_NODE);

/* Пример вашей структуры с уставками */
struct device_config {
    int32_t speed;
    int32_t kp_gain;
};

static struct device_config current_cfg = {
    .speed = 1000,
    .kp_gain = 50,
};

/* 1. Функция чтения конкретного параметра из структуры при загрузке */
static int cfg_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "speed", &next)) {
        if (len != sizeof(current_cfg.speed)) return -EINVAL;
        return (read_cb(cb_arg, &current_cfg.speed, sizeof(current_cfg.speed)) < 0) ? -EINVAL : 0;
    }

    if (settings_name_steq(name, "kp", &next)) {
        if (len != sizeof(current_cfg.kp_gain)) return -EINVAL;
        return (read_cb(cb_arg, &current_cfg.kp_gain, sizeof(current_cfg.kp_gain)) < 0) ? -EINVAL : 0;
    }

    return -ENOENT;
}

/* 2. Функция экспорта (сохраняет текущие RAM-значения в бэкенд при вызове settings_save) */
static int cfg_export(int (*export_func)(const char *name, const void *value, size_t val_len)) {
    int rc;

    rc = export_func("motor/speed", &current_cfg.speed, sizeof(current_cfg.speed));
    if (rc) return rc;

    rc = export_func("motor/kp", &current_cfg.kp_gain, sizeof(current_cfg.kp_gain));
    if (rc) return rc;

    return 0;
}

/* Регистрируем обработчик ветки "motor" */
SETTINGS_STATIC_HANDLER_DEFINE(motor_settings, "motor", NULL, cfg_set, NULL, cfg_export);


/* Бэкенд-драйвер сохранения/загрузки всего файла настроек во FRAM */
int fram_backend_commit(void) {
    // При вызове "settings save" Zephyr сам соберет все параметры через export 
    // и передаст их нам, либо мы можем просто сбросить всю структуру во FRAM.
    // Но правильнее использовать встроенный механизм сохранения в бэкенд.
    return 0;
}

/* Функция инициализации и загрузки настроек при старте */
int settings_fram_init(void) {
    if (!device_is_ready(fram_dev)) {
        LOG_ERR("FRAM device not ready!");
        return -ENODEV;
    }

    // Пример: Читаем всю структуру целиком из FRAM при старте (смещение 0)
    int err = eeprom_read(fram_dev, 0, &current_cfg, sizeof(current_cfg));
    if (err) {
        LOG_ERR("Failed to read config from FRAM (%d)", err);
    }

    // Инициализируем подсистему settings
    settings_subsys_init();

    // Загружаем поверх дефолтных значений те, что были переопределены
    settings_load();

    LOG_INF("Settings loaded. Speed: %d, KP: %d", current_cfg.speed, current_cfg.kp_gain);
    return 0;
}
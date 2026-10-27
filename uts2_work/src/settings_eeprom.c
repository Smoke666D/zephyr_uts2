#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#define EEPROM_USER_MAGIC   0x45455052 /* "EEPR" */
#define EEPROM_STRING_MAX_LEN  32

/* Побайтово упакованная структура данных для каждого устройства EEPROM */
#pragma pack(push, 1)
struct eeprom_device_msg {
    uint32_t factory_uid;                 /* Заводской 32-битный UID (адрес 0xFC) */
    uint32_t serial_number;               /* Пользовательский серийный номер */
    uint32_t formatted_id;                /* Пользовательский идентификатор */
    uint32_t usage_counter;               /* Счетчик записей/включений */
    char     user_string[EEPROM_STRING_MAX_LEN]; /* Пользовательская строка */
    bool     is_valid;                    /* Флаг успешного чтения и валидности CRC16 */
};

/* Физическая структура кадра в памяти EEPROM */
struct eeprom_disk_payload {
    uint32_t serial_number;
    uint32_t formatted_id;
    uint32_t usage_counter;
    char     user_string[EEPROM_STRING_MAX_LEN];
    uint16_t crc16;
};
#pragma pack(pop)

LOG_MODULE_REGISTER(eeprom_mgr, LOG_LEVEL_INF);

/* Заводской адрес расположения 32-битного UID в памяти 24AA025UID */
#define EEPROM_UID_OFFSET  0xFC

static const struct device *const eeprom1_dev = DEVICE_DT_GET(DT_NODELABEL(eeprom_1));
static const struct device *const eeprom2_dev = DEVICE_DT_GET(DT_NODELABEL(eeprom_2));

/* Прототипы листенеров автосохранения */
static void eeprom1_listener_fn(const struct zbus_channel *chan);
static void eeprom2_listener_fn(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(eeprom1_lis, eeprom1_listener_fn);
ZBUS_LISTENER_DEFINE(eeprom2_lis, eeprom2_listener_fn);

/* ------------------------------------------------------------------ */
/* ОБЪЯВЛЕНИЕ ZBUS КАНАЛОВ ДЛЯ ЕЕPROM_1 И EEPROM_2                    */
/* ------------------------------------------------------------------ */

ZBUS_CHAN_DEFINE(eeprom1_chan,
                 struct eeprom_device_msg,
                 NULL, NULL,
                 ZBUS_OBSERVERS(eeprom1_lis),
                 {0}
);

ZBUS_CHAN_DEFINE(eeprom2_chan,
                 struct eeprom_device_msg,
                 NULL, NULL,
                 ZBUS_OBSERVERS(eeprom2_lis),
                 {0}
);

/* ------------------------------------------------------------------ */
/* ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ                                             */
/* ------------------------------------------------------------------ */

static uint16_t calc_disk_crc(const struct eeprom_disk_payload *disk_data)
{
    size_t payload_bytes = sizeof(struct eeprom_disk_payload) - sizeof(uint16_t);
    return crc16_itu_t(0xFFFF, (const uint8_t *)disk_data, payload_bytes);
}

/**
 * @brief Чтение одного устройства EEPROM с диска в Zbus структуру
 */
static void read_eeprom_chip_to_msg(const struct device *dev, struct eeprom_device_msg *msg)
{
    memset(msg, 0, sizeof(struct eeprom_device_msg));

    if (!device_is_ready(dev)) {
        LOG_ERR("EEPROM device %s not ready!", dev->name);
        return;
    }

    /* 1. Читаем 32-битный заводской UID с адреса 0xFC */
    int err = eeprom_read(dev, EEPROM_UID_OFFSET, &msg->factory_uid, sizeof(uint32_t));
    if (err != 0) {
        LOG_ERR("Failed to read Factory UID from %s: %d", dev->name, err);
        return;
    }

    /* 2. Читаем пользовательские данные с адреса 0x00 */
    struct eeprom_disk_payload disk_data;
    err = eeprom_read(dev, 0x00, &disk_data, sizeof(disk_data));
    if (err == 0) {
        /* Проверяем целостность CRC16 */
        if (calc_disk_crc(&disk_data) == disk_data.crc16) {
            msg->serial_number = disk_data.serial_number;
            msg->formatted_id  = disk_data.formatted_id;
            msg->usage_counter = disk_data.usage_counter;
            memcpy(msg->user_string, disk_data.user_string, sizeof(msg->user_string));
            msg->is_valid = true;
        } else {
            LOG_WRN("CRC mismatch on %s user data!", dev->name);
        }
    }
}

/**
 * @brief Запись из Zbus структуры в физическую микросхему EEPROM
 */
static void save_msg_to_eeprom_chip(const struct device *dev, const struct eeprom_device_msg *msg)
{
    if (!device_is_ready(dev)) {
        return;
    }

    struct eeprom_disk_payload disk_data;
    disk_data.serial_number = msg->serial_number;
    disk_data.formatted_id  = msg->formatted_id;
    disk_data.usage_counter = msg->usage_counter;
    memcpy(disk_data.user_string, msg->user_string, sizeof(disk_data.user_string));

    /* Расчет CRC16 перед записью */
    disk_data.crc16 = calc_disk_crc(&disk_data);

    /* Запись на физическую микросхему I2C (atmel,at24 автоматически разобьет на страницы по 16 байт) [1.1.2] */
    int err = eeprom_write(dev, 0x00, &disk_data, sizeof(disk_data));
    if (err == 0) {
        LOG_INF("Data successfully persisted to %s", dev->name);
    } else {
        LOG_ERR("Failed to write EEPROM %s: %d", dev->name, err);
    }
}

/* ------------------------------------------------------------------ */
/* ЛИСТЕНЕРЫ АВТОСОХРАНЕНИЯ (СРАБАТЫВАЮТ ПРИ ИЗМЕНЕНИИ В ZBUS)        */
/* ------------------------------------------------------------------ */

static void eeprom1_listener_fn(const struct zbus_channel *chan)
{
    if (chan == &eeprom1_chan) {
        const struct eeprom_device_msg *msg = zbus_chan_const_msg(&eeprom1_chan);
        if (msg != NULL) {
            save_msg_to_eeprom_chip(eeprom1_dev, msg);
        }
    }
}

static void eeprom2_listener_fn(const struct zbus_channel *chan)
{
    if (chan == &eeprom2_chan) {
        const struct eeprom_device_msg *msg = zbus_chan_const_msg(&eeprom2_chan);
        if (msg != NULL) {
            save_msg_to_eeprom_chip(eeprom2_dev, msg);
        }
    }
}

/* ------------------------------------------------------------------ */
/* СИСТЕМНАЯ ИНИЦИАЛИЗАЦИЯ ПРИ СТАРТЕ (SYS_INIT)                      */
/* ------------------------------------------------------------------ */

static int eeprom_mgr_init(void)
{
    struct eeprom_device_msg msg1;
    struct eeprom_device_msg msg2;

    /* 1. Считываем данные с обеих физических микросхем */
    read_eeprom_chip_to_msg(eeprom1_dev, &msg1);
    read_eeprom_chip_to_msg(eeprom2_dev, &msg2);

    /* 
     * 2. Молча записываем прочитанные данные в каналы Zbus.
     * Используем Claim/Finish БЕЗ zbus_chan_notify, чтобы листенеры 
     * не начали повторно перезаписывать EEPROM при старте [1.2.8]!
     */
    if (zbus_chan_claim(&eeprom1_chan, K_FOREVER) == 0) {
        struct eeprom_device_msg *chan_msg = (struct eeprom_device_msg *)zbus_chan_msg(&eeprom1_chan);
        memcpy(chan_msg, &msg1, sizeof(msg1));
        zbus_chan_finish(&eeprom1_chan);
    }

    if (zbus_chan_claim(&eeprom2_chan, K_FOREVER) == 0) {
        struct eeprom_device_msg *chan_msg = (struct eeprom_device_msg *)zbus_chan_msg(&eeprom2_chan);
        memcpy(chan_msg, &msg2, sizeof(msg2));
        zbus_chan_finish(&eeprom2_chan);
    }

    LOG_INF("EEPROM Manager initialized: eeprom1_chan (UID: 0x%08X), eeprom2_chan (UID: 0x%08X)",
            msg1.factory_uid, msg2.factory_uid);

    return 0;
}

/* Запуск на этапе POST_KERNEL (приоритет 85) [1.1.1] */
SYS_INIT(eeprom_mgr_init, POST_KERNEL, 85);
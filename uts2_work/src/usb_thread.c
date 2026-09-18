
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>
#






USBD_DEVICE_DEFINE(my_usbd, 
                   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 
                   0x2fe9, 0x0100);

USBD_DESC_LANG_DEFINE(lang_desc);
USBD_DESC_MANUFACTURER_DEFINE(mfr_desc, "MyCompany");
USBD_DESC_PRODUCT_DEFINE(prod_desc, "UTS2 Board Shell");


/* 3. Конфигурация устройства (FS = Full-Speed, атрибуты: питание от шины, ток до 100 мА) */
USBD_CONFIGURATION_DEFINE(my_usbd_config,
                          USB_SCD_SELF_POWERED,
                          100, /* 100 mA */
                          NULL);

int init_usb_console(void)
{
    int err;

    /* 1. Дескрипторы */
    err = usbd_add_descriptor(&my_usbd, &lang_desc);
    err |= usbd_add_descriptor(&my_usbd, &mfr_desc);
    err |= usbd_add_descriptor(&my_usbd, &prod_desc);
    if (err) {
        printk("Ошибка дескрипторов: %d\n", err);
        return err;
    }

    /* 2. Конфигурация */
    err = usbd_add_configuration(&my_usbd, USBD_SPEED_FS, &my_usbd_config);
    if (err) {
        printk("Ошибка конфигурации: %d\n", err);
        return err;
    }

    /* 3. Регистрация класса: имя строго "cdc_acm_0" */
    err = usbd_register_class(&my_usbd, "cdc_acm_0", USBD_SPEED_FS, 1);
    if (err) {
        printk("Ошибка регистрации класса: %d\n", err);
        return err;
    }

    /* 4. КРИТИЧНО ДЛЯ WINDOWS: задаём IAD triple (Miscellaneous / 0x02 / 0x01) */
    usbd_device_set_code_triple(&my_usbd, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 0x02, 0x01);

    /* 5. Инициализация и включение */
    err = usbd_init(&my_usbd);
    if (err) {
        printk("Ошибка usbd_init: %d\n", err);
        return err;
    }

    err = usbd_enable(&my_usbd);
    if (err) {
        printk("Ошибка usbd_enable: %d\n", err);
        return err;
    }

    printk("USB успешно запущен!\n");
    return 0;
}
#ifndef LED_H
#define LED_H

#include <zephyr/kernel.h>

/* Структура состояния светодиодов */

/* Обычная структура без typedef */
struct led_state_msg {
    bool led[3];
    uint32_t update_mask;
};
/**
 * @brief Синхронная установка состояния светодиодов через ZBUS и воркер.
 */


#endif /* LED_H */
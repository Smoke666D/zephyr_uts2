#ifndef LED_H
#define LED_H

#include <zephyr/kernel.h>

/* Структура состояния светодиодов */

/* Обычная структура без typedef */
struct led_state_msg {
    bool led1;
    bool led2;
    bool led3;
};
/**
 * @brief Синхронная установка состояния светодиодов через ZBUS и воркер.
 */
int led_manager_set_states_sync(bool l1, bool l2, bool l3, k_timeout_t timeout);

#endif /* LED_H */
#ifndef SENSOR_THREAD_H
#define SENSOR_THREAD_H

#include <zephyr/kernel.h>

#define SENSOR_STACK_SIZE 1024
#define SENSOR_PRIORITY 5
#define SENSOR_PERIOD_MS 1000

/**
 * @brief Запустить поток опроса датчика
 *
 * @return 0 при успехе, отрицательный код при ошибке
 */
int sensor_thread_start(void);

#endif /* SENSOR_THREAD_H */


#ifndef TASK_WORKER_H
#define TASK_WORKER_H

#include <zephyr/kernel.h>

/// @brief Сигнатура функции-обработчика задачи
typedef void (*task_handler_t)(void *arg);

/**
 *  @brief Структура задачи для очереди воркера
 */
typedef struct {
    task_handler_t handler; ///< Функция обработки
    void          *arg;     ///< Аргумент функции
} app_task_t;

/**
 *  @brief      Отправка задачи в очередь воркера
 *  @details    Безопасно для вызова из ISR. Копирует задачу в очередь.
 *
 *  @param      task - Указатель на структуру задачи
 *  @return     int  - 0 при успехе, отрицательный код ошибки при сбое
 */
int task_worker_submit(const app_task_t *task);

#endif /* TASK_WORKER_H */
#include <zephyr/kernel.h>
#include "system_bridge.h"

#define TASK_QUEUE_SIZE   16
#define WORKER_STACK_SIZE 2048
#define WORKER_PRIORITY   5

// Определение очереди сообщений для задач
K_MSGQ_DEFINE(task_msgq, sizeof(app_task_t), TASK_QUEUE_SIZE, 4);

/**
 *  @brief Функция потока воркера
 */
static void worker_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app_task_t task;

    while (1) 
    {
        // Ожидание поступления новой работы (блокирующий вызов)
        if (k_msgq_get(&task_msgq, &task, K_FOREVER) == 0) 
        {
            if (task.handler != NULL) 
            {
                task.handler(task.arg);
            }
        }
    }
}

// Статическое объявление потока воркера
K_THREAD_DEFINE(worker_thread, WORKER_STACK_SIZE,
                worker_thread_fn, NULL, NULL, NULL,
                WORKER_PRIORITY, 0, 0);

int task_worker_submit(const app_task_t *task)
{
    if (task == NULL || task->handler == NULL) 
    {
        return -EINVAL;
    }

    // K_NO_WAIT позволяет безболезненно вызывать функцию отправки из прерываний
    return k_msgq_put(&task_msgq, task, K_NO_WAIT);
}
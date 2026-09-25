#include "app_worker.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Регистрируем модуль логирования для воркера */
LOG_MODULE_REGISTER(app_worker, LOG_LEVEL_INF);

#define WORKER_STACK_SIZE 1024
#define WORKER_PRIORITY   7

static struct k_work_q app_work_q;
K_THREAD_STACK_DEFINE(worker_stack, WORKER_STACK_SIZE);

static int app_worker_system_init(void)
{
   

    k_work_queue_init(&app_work_q);
    k_work_queue_start(&app_work_q, 
                       worker_stack,
                       K_THREAD_STACK_SIZEOF(worker_stack), 
                       WORKER_PRIORITY, 
                       NULL);

    return 0;
}

void app_worker_reschedule_submit(struct k_work_delayable *delayed_work, k_timeout_t delay)
{
    k_work_reschedule_for_queue(&app_work_q, delayed_work, delay);
}

void app_worker_submit(struct k_work *work)
{
    k_work_submit_to_queue(&app_work_q, work);
}

SYS_INIT(app_worker_system_init, POST_KERNEL, 50);
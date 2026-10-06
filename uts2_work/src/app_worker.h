#ifndef APP_WORKER_H
#define APP_WORKER_H

#include <zephyr/kernel.h>


typedef enum
{
    COMMON_WORKER,
    REAL_TIME_WORKER,
} worker_type;
/**
 * @brief Универсальная функция отправки задачи в воркер.
 * @курсор Указатель на структуру k_work, которую нужно поставить в очередь.
 */
void app_worker_submit(struct k_work *work,  worker_type _worker_type);

void app_worker_reschedule_submit(struct k_work_delayable *delayed_work, k_timeout_t delay, worker_type _worker_type);

void app_worker_poll_submit(struct k_work_poll *_work, struct k_poll_event * _poll_event, worker_type _worker_type);



#endif /* APP_WORKER_H */
#ifndef APP_WORKER_H
#define APP_WORKER_H

#include <zephyr/kernel.h>

/**
 * @brief Универсальная функция отправки задачи в воркер.
 * @курсор Указатель на структуру k_work, которую нужно поставить в очередь.
 */
void app_worker_submit(struct k_work *work);

#endif /* APP_WORKER_H */
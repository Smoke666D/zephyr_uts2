/**
 *  @file       app_worker.c
 *  @headerfile app_worker.h
 *
 *  @date       2026.10.06
 *  @author     Dymov Igor
 *
 *  @brief      Системный диспетчер очередей задач (Workqueues)
 *  @details    Реализует инициализацию и запуск выделенных очередей задач 
 *              стандартного выполнения и реального времени для планирования воркеров.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_worker.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
// Регистрация модуля логирования для планировщика воркеров
LOG_MODULE_REGISTER(app_worker, LOG_LEVEL_INF);

#define WORKER_STACK_SIZE           2048
#define COMMON_WORKER_PRIORITY      4
#define REAL_TIME_WORKER_PRIORITY   -1

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static int _app_worker_system_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static struct k_work_q app_work_q;
static struct k_work_q app_real_time_work_q;

K_THREAD_STACK_DEFINE(worker_stack, WORKER_STACK_SIZE);
K_THREAD_STACK_DEFINE(real_time_worker_stack, WORKER_STACK_SIZE);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Системная инициализация очередей задач
 *  @details    Выполняет инициализацию и запуск двух очередей задач:
 *              стандартной (приоритет 4) и реального времени (приоритет -1).
 *
 *  @return     int - Ноль при успешной инициализации
 */
static int _app_worker_system_init(void)
{
    k_work_queue_init(&app_work_q);
    k_work_queue_init(&app_real_time_work_q);

    k_work_queue_start(
        &app_work_q, 
        worker_stack,
        K_THREAD_STACK_SIZEOF(worker_stack), 
        COMMON_WORKER_PRIORITY, 
        NULL
    );

    k_work_queue_start(
        &app_real_time_work_q, 
        real_time_worker_stack,
        K_THREAD_STACK_SIZEOF(real_time_worker_stack), 
        REAL_TIME_WORKER_PRIORITY, 
        NULL
    );

    return 0;
}

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Повторная отправка отложенной задачи в очередь
 *  @details    Перепланирует выполнение отложенной задачи с заданным таймаутом
 *              в выбранную очередь (стандартную или реального времени).
 *
 *  @param      _delayed_work - Указатель на структуру отложенной задачи
 *  @param      _delay        - Таймаут перед запуском задачи
 *  @param      _worker_type  - Тип очереди (стандартная или реального времени)
 */
void app_worker_reschedule_submit(struct k_work_delayable *_delayed_work, 
                                  k_timeout_t _delay, 
                                  worker_type _worker_type)
{
    struct k_work_q *q = (_worker_type == REAL_TIME_WORKER) 
                         ? &app_real_time_work_q 
                         : &app_work_q;

    k_work_reschedule_for_queue(q, _delayed_work, _delay);    
}

/**
 *  @brief      Отправка задачи в очередь
 *  @details    Помещает задачу в выбранную очередь (стандартную или реального времени)
 *              для немедленного выполнения.
 *
 *  @param      _work        - Указатель на структуру задачи
 *  @param      _worker_type - Тип очереди (стандартная или реального времени)
 */
void app_worker_submit(struct k_work *_work, worker_type _worker_type)
{
    struct k_work_q *q = (_worker_type == REAL_TIME_WORKER) 
                         ? &app_real_time_work_q 
                         : &app_work_q;
    
    k_work_submit_to_queue(q, _work);    
}

/**
 *  @brief      Отправка поллинг-задачи в очередь
 *  @details    Помещает поллинг-задачу со структурой событий в выбранную очередь.
 *
 *  @param      _work        - Указатель на структуру поллинг-задачи
 *  @param      _poll_event  - Указатель на структуру событий поллинга
 *  @param      _worker_type - Тип очереди (стандартная или реального времени)
 */
void app_worker_poll_submit(struct k_work_poll *_work, 
                            struct k_poll_event *_poll_event, 
                            worker_type _worker_type)
{
    struct k_work_q *q = (_worker_type == REAL_TIME_WORKER) 
                         ? &app_real_time_work_q 
                         : &app_work_q;

    k_work_poll_submit_to_queue(q, _work, _poll_event, 1, K_FOREVER);
}

SYS_INIT(_app_worker_system_init, POST_KERNEL, 50);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
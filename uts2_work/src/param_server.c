#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include "param_server.h"

/* Автогенерация этих символов гарантирована линкером GNU ld */
extern const struct param_handler __start_param_routes[];
extern const struct param_handler __stop_param_routes[];

/* RAM-кэш указателей на структуры обработчиков в ROM */
static const struct   param_handler *cache[PARAM_TOTAL_COUNT] __dtcm_bss_section;

/**
 * @brief Функция автоинициализации кэша (вызывается до старта планировщика)
 */
static int param_cache_init(void)
{
    for (const struct param_handler *handler = __start_param_routes;
         handler < __stop_param_routes;
         handler++) {
         
        if (handler->param_id < PARAM_TOTAL_COUNT) {
            cache[handler->param_id] = handler;
        }
    }
    return 0;
}

SYS_INIT(param_cache_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);


int param_set(PARAM_ID id, const PARAM_VAL *val)
{
    if (id >= PARAM_TOTAL_COUNT) {
        return -EINVAL;
    }

    /* 
     * Прямой вызов обработчика за O(1) без блокировок.
     * Потокобезопасность обеспечивается внутри самого коллбэка set()
     */
    const struct param_handler *handler = cache[id];
    if (handler != NULL) {
        if (handler->set != NULL) {
            return handler->set(id, val);
        }
        return -EACCES; /* Запись в параметр Read-Only запрещена */
    }

    return -ENODEV; /* Модуль обслуживания параметра не скомпилирован */
}

int param_get(PARAM_ID id, PARAM_VAL *val)
{
    if (id >= PARAM_TOTAL_COUNT) {
        return -EINVAL;
    }

    /* 
     * Прямой вызов обработчика за O(1) без блокировок.
     * Потокобезопасность обеспечивается внутри самого коллбэка get() (через Zbus)
     */
    const struct param_handler *handler = cache[id];
    if (handler != NULL) {
        if (handler->get != NULL) {
            return handler->get(id, val);
        }
        return -EACCES; /* Чтение параметра Write-Only запрещено */
    }

    return -ENODEV; /* Модуль обслуживания параметра не скомпилирован */
}
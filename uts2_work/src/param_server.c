#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include "param_server.h"

/* Автогенерация этих символов гарантирована линкером GNU ld */
extern const struct param_handler __start_param_routes[];
extern const struct param_handler __stop_param_routes[];

int param_set(PARAM_ID id, const PARAM_VAL *val)
{
    for (const struct param_handler *handler = __start_param_routes;
         handler < __stop_param_routes;
         handler++) {
         
        if (handler->param_id == id) {
            if (handler->set != NULL) {
                return handler->set(id, val);
            }
            return -EACCES;
        }
    }
    return -ENODEV;
}

int param_get(PARAM_ID id, PARAM_VAL *val)
{
    for (const struct param_handler *handler = __start_param_routes;
         handler < __stop_param_routes;
         handler++) {
         
        if (handler->param_id == id) {
            if (handler->get != NULL) {
                return handler->get(id, val);
            }
            return -EACCES;
        }
    }
    return -ENODEV;
}
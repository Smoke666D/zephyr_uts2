#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include "param_server.h"

int param_set(PARAM_ID id, const PARAM_VAL *val)
{
    STRUCT_SECTION_FOREACH(param_handler, handler) {
        if (handler->param_id == id) {
            if (handler->set != NULL) {
                return handler->set(id, val); /* Передаем id! */
            }
            return -EACCES;
        }
    }
    return -ENODEV;
}

int param_get(PARAM_ID id, PARAM_VAL *val)
{
    STRUCT_SECTION_FOREACH(param_handler, handler) {
        if (handler->param_id == id) {
            if (handler->get != NULL) {
                return handler->get(id, val); /* Передаем id! */
            }
            return -EACCES;
        }
    }
    return -ENODEV;
}
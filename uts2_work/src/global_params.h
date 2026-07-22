
#ifndef GLOBAL_PARAMS_H_
#define GLOBAL_PARAMS_H_

#include <stdint.h>
#include <stdbool.h>

/* Логические параметры всей системы */


typedef enum
{
    AIN_AO1,
    AIN_AO7,
    AIN_AO13,
    AIN_AVsense1,
    AIN_AO2,
    AIN_AO8,
    AIN_AO14,
    AIN_AVsense2,    
    AIN_AO3,
    AIN_AO9,
    AIN_AO15,
    AIN_AVsense3,        
    AIN_AO4,
    AIN_AO10,
    AIN_AO16,
    AIN_AVsense4,
    AIN_AO5,
    AIN_AO11,
    AIN_AO17,
    AIN_AVsense5,
    AIN_AO6,                    
    AIN_AO12,                
    AIN_AO18,
    AIN_AVsense6,
    AIN_DA11_test1,
    AIN_DA20_test1,
    AIN_DA33_test1,
    AIN_DA44_test1,
    AIN_DA11_test2,
    AIN_DA20_test2,
    AIN_DA33_test2,
    AIN_DA44_test2,    
    /* Сюда в будущем можно добавлять любые другие параметры других модулей */
    PARAM_TOTAL_COUNT
} PARAM_ID;

/* Универсальный контейнер обмена данными */
typedef struct {
    union {
        int32_t  integer;
        double   real;
        bool     boolean;
        uint8_t  raw[8];
    } value;
} PARAM_VAL;

/* Определение структуры обработчика маршрута */
struct param_handler {
    PARAM_ID param_id;
    int (*set)(PARAM_ID id, const PARAM_VAL *val);
    int (*get)(PARAM_ID id, PARAM_VAL *val);
};

/* 
 * Макросы автоматической генерации структур.
 * Препроцессор сам соберет уникальное имя структуры с помощью оператора слияния (##) [1.1.1].
 */
#define PARAM_ROUTE_RW(id, set_fn, get_fn) \
    const STRUCT_SECTION_ITERABLE(param_handler, route_##id) = { \
        .param_id = id, \
        .set = set_fn, \
        .get = get_fn, \
    }

/* Макрос для параметров "Только на запись" (Write Only) */
#define PARAM_ROUTE_WO(id, set_fn) \
    PARAM_ROUTE_RW(id, set_fn, NULL)

/* Макрос для параметров "Только на чтение" (Read Only) */
#define PARAM_ROUTE_RO(id, get_fn) \
    PARAM_ROUTE_RW(id, NULL, get_fn)

#endif /* GLOBAL_PARAMS_H_ */
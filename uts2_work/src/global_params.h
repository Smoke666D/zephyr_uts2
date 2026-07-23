
#ifndef GLOBAL_PARAMS_H_
#define GLOBAL_PARAMS_H_

#include <stdint.h>
#include <stdbool.h>





/* Логические параметры всей системы */
/* WARNING!!!!
Парамерты обязательно должны быть сгруппированы по одноитмпогму set/get методу
что бы внутри метода можно было проверить валидность параметра на больше/меньше 
в рамках группы.
*/

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
    SENS_BRD_LOW_VOLTAGE,
    SENS_BRD_HIGH_VOLTAGE,
    SENS_VDUT2_VOLTAGE,
    SENS_VIN_VOLTAGE,
    SENS_DCDC_3_3_VOLTAGE,
    SENS_VDOUT_PWR_VOLTAGE,
    SENS_VD5_VOLTAGE, 
    SENS_BRD_LOW_CURRENT,    
    SENS_BRD_HIGH_CURRENT,    
    SENS_VDUT2_CURRENT,    
    SENS_VDUT3_CURRENT,
    SENS_VDUT3_VOLTAGE,
    SENS_VIN_CURRENT,    
    SENS_DCDC_3_3_CURRENT,    
    SENS_VDOUT_PWR_CURRENT,    
    SENS_VD5_CURRENT,    
    SENS_I2C_POWER_BRD_LOW,
    SENS_I2C_POWER_BRD_HIGH,
    SENS_I2C_POWER_VDUT2,
    SENS_I2C_POWER_VDUT3,
    SENS_I2C_POWER_VIN,
    SENS_I2C_POWER_DCDC_3_3,
    SENS_I2C_POWER_VDOUT_PWR,
    SENS_I2C_POWER_VD5, 
    /* Сюда в будущем можно добавлять любые другие параметры других модулей */
    PARAM_TOTAL_COUNT
} PARAM_ID;

/* Универсальный контейнер обмена данными */
typedef struct {
    union {
        uint32_t  integer;
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
 * Статическое размещение в секцию "param_routes" (без точки в имени).
 * Атрибут 'used' заставляет компилятор выдать структуру в объектный файл.
 */
#define PARAM_ROUTE_RW(id, set_fn, get_fn) \
    const struct param_handler __attribute__((section("param_routes"), used)) route_##id = { \
        .param_id = id, \
        .set = set_fn, \
        .get = get_fn, \
    }

#define PARAM_ROUTE_WO(id, set_fn) \
    PARAM_ROUTE_RW(id, set_fn, NULL)

#define PARAM_ROUTE_RO(id, get_fn) \
    PARAM_ROUTE_RW(id, NULL, get_fn)

#endif /* GLOBAL_PARAMS_H_ */
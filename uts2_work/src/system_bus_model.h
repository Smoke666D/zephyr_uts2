#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>


#define LED_CNT 3

#define LOW_CUR_DRIVER_COUNT 18
#define LIN_CONTROL_CNT  4
#define CHANNEL_DATA_SIZE (LOW_CUR_DRIVER_COUNT + LIN_CONTROL_CNT)

#define TOTAL_DAC_COUNT 3

typedef enum
{   AIN_AO1,
    AIN_AO7,
    AIN_AO13,
    AIN_AVsense1,
    LIN_AO1,
    AIN_AO2,
    AIN_AO8,
    AIN_AO14,
    AIN_AVsense2,    
    LIN_AO2,
    AIN_AO3,
    AIN_AO9,
    AIN_AO15,
    AIN_AVsense3,        
    LIN_AO3,
    AIN_AO4,
    AIN_AO10,
    AIN_AO16,
    AIN_AVsense4,
    LIN_AO4,
    AIN_AO5,
    AIN_AO11,
    AIN_AO17,
    AIN_AVsense5,
    DA41_GND1,
    AIN_AO6,                    
    AIN_AO12,                
    AIN_AO18,
    AIN_AVsense6,
    DA41_GND2,
    AIN_DA11_test1,
    AIN_DA20_test1,
    AIN_DA33_test1,
    AIN_DA44_test1,
    AIN_DA41_test1,
    AIN_DA11_test2,
    AIN_DA20_test2,
    AIN_DA33_test2,
    AIN_DA44_test2, 
    AIN_DA41_test2,
    ENV_P3V3,
    ENV_P5V0,
    ENV_VIN,
    ENV_VDOUT1,
    ENV_P40V,
    ENV_USB, 
    ENV_VDOUT2,
    ENV_VDOUT3,
    LED1,
    LED2,
    LED3,
    BUTTON1,
    BUTTON2,
    I2C1_TEMP1,
    I2C1_TEMP2,
    I2C1_LUX1,
    I2C1_LUX2,
    RF_POWER,
    NTC,
    EXT_VSENSE,
    BRD_DETECT,
    DOUT1,
    DOUT2,
    DOUT3,
    DOUT4,
    DOUT5,
    DOUT6,
    DOUT7,
    DOUT8,
    DOUT9,
    DOUT10,
    DOUT11,
    DOUT12,
    DOUT13,
    DOUT14,
    DOUT15,
    DOUT16,
    DOUT17,
    DOUT18,
    LIN_PD1,
    LIN_PD2,
    LIN_PD3,
    LIN_PD4,
    EN_DUT2_PSU,
    EN_DUT3_PSU,  
    EN_P12V,
    EN_P24V,
    EN_VA,
    EN_USB_OUT,
    EN_USB_BOOT,
    EN_USB_TOP,
    DAC_VALUE,
    ADJ_RES1,
    ADJ_RES2,
    SENS_BRD_LOW_CURRENT, 
    SENS_BRD_LOW_VOLTAGE,  
    SENS_BRD_HIGH_CURRENT, 
    SENS_BRD_HIGH_VOLTAGE, 
    SENS_VDUT2_CURRENT,   
    SENS_VDUT2_VOLTAGE,  
    SENS_VDUT3_CURRENT,  
    SENS_VDUT3_VOLTAGE,  
    SENS_VIN_CURRENT,    
    SENS_VIN_VOLTAGE,    
    SENS_DCDC_3_3_CURRENT,
    SENS_DCDC_3_3_VOLTAGE,
    SENS_VDOUT_PWR_CURRENT,
    SENS_VDOUT_PWR_VOLTAGE,
    SENS_VD5_CURRENT,      
    SENS_VD5_VOLTAGE,      
    HARDWARE_ERROR_REGISTER1,
    HARDWARE_ERROR_REGISTER2,
    /* Сюда в будущем можно добавлять любые другие параметры других модулей */
    SYSTEM_BUS_COUNT
} SYSTEM_BUS_ID;

typedef enum
{
   LIN_PULL_DOWN_OFF,
   LIN_PULL_DOWN_ON,
} lin_pull_down_state_t;


typedef enum
{
  SINGLE_DATA,
  FLOAT,
  BOOL,
  ARRAY_DATA,
  QUEUE_DATA,
} SYSTEM_BUS_DATA_TYPE;

/* Универсальный контейнер обмена данными */
typedef struct {
    union {
        uint32_t  integer;
        float     real;
        bool      boolean;
        uint8_t   raw[4];
        void *    pointer;
    } value;
} DATA_VAL;

typedef struct 
{
    bool  value[32];    
} BOOLEAN_ARRAY_CHANNEL_t;


typedef struct 
{
    float  value[4];    
} FLOAT_ARRAY_CHANNEL_4_t;

struct system_bus_handler
{
    SYSTEM_BUS_ID         bus_id;  //ID параметра
    const struct zbus_channel   *channel; // Ссылка на канал
    SYSTEM_BUS_DATA_TYPE  channel_type;    
    uint32_t              system_index;
};



/* 
 * Макрос для регистрации параметра. 
 * Сразу принимает имя Zbus-канала (например, chan_ain_ao1).
 */
#define PARAM_ROUTE_DEFINE(id, chan_ptr, index, chan_type) \
    const struct system_bus_handler __attribute__((section("param_routes"), used)) route_##id = { \
        .bus_id = id, \
        .channel = chan_ptr, \
        .channel_type = chan_type, \
        .system_index = index, \
    }

 int bus_set_bool(SYSTEM_BUS_ID id, bool _val);
 int bus_set_u32(SYSTEM_BUS_ID id, uint32_t _val);
 int bus_set_real(SYSTEM_BUS_ID id, float _val);

#define SYSTEM_BUS_SET(id, val) _Generic((val), \
        bool:       bus_set_bool(id, val), \
        int:        bus_set_u32(id, val),  \
        unsigned:   bus_set_u32(id, val),  \
        float:      bus_set_real(id, val) \
    )

int bus_get_bool(SYSTEM_BUS_ID id, bool * _val);
int bus_get_u32(SYSTEM_BUS_ID id,  uint32_t * _val);
int bus_get_real(SYSTEM_BUS_ID id, float * _val);

// 2. Пишем макрос, который смотрит на тип указателя `val`
#define SYSTEM_BUS_GET(id, val) _Generic(*(val), \
    bool:     bus_get_bool, \
    int:      bus_get_u32,  \
    unsigned: bus_get_u32,  \
    float:    bus_get_real  \
)((id), (val))



/* Базовые функции установки массива параметров */
int bus_set_bool_arr(bool val, const SYSTEM_BUS_ID *ids, size_t count);
int bus_set_u32_arr(uint32_t val, const SYSTEM_BUS_ID *ids, size_t count);
int bus_set_real_arr(float val, const SYSTEM_BUS_ID *ids, size_t count);

/* Вспомогательный макрос генерации массива "на лету" */
#define _BUS_CALL_MULTI(fn, val, ...) \
    fn((val), (const SYSTEM_BUS_ID[]){__VA_ARGS__}, sizeof((const SYSTEM_BUS_ID[]){__VA_ARGS__}) / sizeof(SYSTEM_BUS_ID))

/* Универсальный макрос с автоопределением типа значения */
#define SYSTEM_BUS_SET_MULTI(val, ...) _Generic((val), \
    bool:          bus_set_bool_arr, \
    int:           bus_set_u32_arr,  \
    unsigned int:  bus_set_u32_arr,  \
    uint32_t:      bus_set_u32_arr,  \
    float:         bus_set_real_arr  \
)((val), (const SYSTEM_BUS_ID[]){__VA_ARGS__}, sizeof((const SYSTEM_BUS_ID[]){__VA_ARGS__}) / sizeof(SYSTEM_BUS_ID))
    
#ifdef __cplusplus
}
#endif
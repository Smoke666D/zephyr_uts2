#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

typedef enum
{
    LED1,
    LED2,
    LED3,
    BUTTON1,
    BUTTON2,
    /* Сюда в будущем можно добавлять любые другие параметры других модулей */
    SYSTEM_BUS_COUNT
} SYSTEM_BUS_ID;




typedef enum
{
  SINGLE_DATA,
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
        float:      bus_set_real(id, val) \
    )

    
#ifdef __cplusplus
}
#endif
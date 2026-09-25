#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include "system_bus_model.h"



/* Автогенерация этих символов гарантирована линкером GNU ld */
extern const struct system_bus_handler __start_param_routes[];
extern const struct system_bus_handler __stop_param_routes[];

/* RAM-кэш указателей на структуры обработчиков в ROM */
static const struct system_bus_handler *cache[SYSTEM_BUS_COUNT] __dtcm_bss_section;

/**
 * @brief Функция автоинициализации кэша (вызывается до старта планировщика)
 */
static int system_bus_cache_init(void)
{

    for (const struct system_bus_handler *handler = __start_param_routes;
         handler < __stop_param_routes;
         handler++) 
    {         
        if (handler->bus_id < SYSTEM_BUS_COUNT) 
        {
            cache[handler->bus_id] = handler;
        }
    }
    return 0;
}

 int bus_set_bool(SYSTEM_BUS_ID id, bool _val)
{
    if (id >= SYSTEM_BUS_COUNT)
    {
        return -EINVAL;
    }

    const struct system_bus_handler *handler = cache[id];
    if (true
        && handler != NULL     
    )
    {
        if (handler->channel_type == ARRAY_DATA)
        {            
            // Захватываем данные канала
            if (zbus_chan_claim(handler->channel,K_MSEC(50)) == 0 )
            {
                BOOLEAN_ARRAY_CHANNEL_t * msg = zbus_chan_msg(handler->channel);                
                msg->value[handler->system_index] = _val;
                zbus_chan_finish(handler->channel);
                zbus_chan_notify(handler->channel,K_NO_WAIT); 
                return 0;
            }
        }         
    }
   return -ENODEV; /* Модуль обслуживания параметра не скомпилирован */
}

 int bus_set_u32(SYSTEM_BUS_ID id, uint32_t _val)
{
return 0;
}

 int bus_set_real(SYSTEM_BUS_ID id, float _val)
{
return 0;
}

    
SYS_INIT(system_bus_cache_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
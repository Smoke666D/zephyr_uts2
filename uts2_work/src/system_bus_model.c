#include <stdint.h>
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


static inline const struct system_bus_handler * _get_valid_handler(SYSTEM_BUS_ID id)
{
    // 1. Проверка границ массива кэша
    if (id >= SYSTEM_BUS_COUNT)
    {
        return NULL;
    }

    const struct system_bus_handler *handler = cache[id];

    // 2. Проверка, что хендлер существует и у него задан валидный канал
    if (handler == NULL || handler->channel == NULL)
    {
        return NULL;
    }

    return handler;
}


 int bus_set_bool(SYSTEM_BUS_ID id, bool _val)
{
    const struct system_bus_handler *handler = _get_valid_handler(id);
    if (handler == NULL)
    {
        return -EINVAL; // Или -ENODEV, в зависимости от того, хотите ли вы различать ошибки
    }

    if (handler->channel_type == ARRAY_DATA)
    {            
        // Захватываем данные канала
        if (zbus_chan_claim(handler->channel, K_MSEC(50)) == 0)
        {
            BOOLEAN_ARRAY_CHANNEL_t *msg = zbus_chan_msg(handler->channel);                
            msg->value[handler->system_index] = _val;
            zbus_chan_finish(handler->channel);
            zbus_chan_notify(handler->channel, K_NO_WAIT); 
            return 0;
        }
        return -EBUSY; // Если не удалось захватить канал по тайм-ауту
    }         

    return -ENOTSUP; /* Тип данных не поддерживается этой функцией */
}

int bus_set_u32(SYSTEM_BUS_ID id, uint32_t _val)
{
     const struct system_bus_handler *handler = _get_valid_handler(id);
    if (handler == NULL)
    {
        return -EINVAL; // Или -ENODEV, в зависимости от того, хотите ли вы различать ошибки
    }

    if (handler->channel_type == ARRAY_DATA)
    {            
        // Захватываем данные канала
        if (zbus_chan_claim(handler->channel, K_MSEC(50)) == 0)
        {
            uint32_t *msg = (uint32_t *)zbus_chan_msg(handler->channel);                
            msg[handler->system_index] = _val;
            zbus_chan_finish(handler->channel);
            zbus_chan_notify(handler->channel, K_NO_WAIT); 
            return 0;
        }
        return -EBUSY; // Если не удалось захватить канал по тайм-ауту
    }         

    return -ENOTSUP; /* Тип данных не поддерживается этой функцией */
}

 int bus_set_real(SYSTEM_BUS_ID id, float _val)
{
return 0;
}

    
 int bus_get_bool(SYSTEM_BUS_ID id, bool * _val)
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

 int bus_get_u32(SYSTEM_BUS_ID id, uint32_t * _val)
{
    const struct system_bus_handler *handler = _get_valid_handler(id);
    if (handler == NULL)
    {
        return -EINVAL; // Или -ENODEV, в зависимости от того, хотите ли вы различать ошибки
    }

    size_t msg_size = zbus_chan_msg_size(handler->channel);
    uint8_t raw_msg_buffer[136]; 
    if (msg_size > sizeof(raw_msg_buffer)) {
            return -ENOMEM; 
    }

    // Захватываем данные канала
   if (zbus_chan_read(handler->channel, raw_msg_buffer, K_MSEC(50)) == 0)
        {
            // 3. Интерпретируем данные как массив float и берем нужное по индексу
            uint32_t *values = (uint32_t *)raw_msg_buffer;
            
            *_val = values[handler->system_index];
            return 0;
        }          
    
   return -ENODEV; /* Модуль обслуживания параметра не скомпилирован */
}

int bus_get_real(SYSTEM_BUS_ID id, float * _val)
{
 

    const struct system_bus_handler *handler = _get_valid_handler(id);
    if (handler == NULL)
    {
        return -EINVAL; // Или -ENODEV, в зависимости от того, хотите ли вы различать ошибки
    }

    size_t msg_size = zbus_chan_msg_size(handler->channel);
    uint8_t raw_msg_buffer[160]; 
    if (msg_size > sizeof(raw_msg_buffer)) {
            return -ENOMEM; 
    }

    // Захватываем данные канала
   if (zbus_chan_read(handler->channel, raw_msg_buffer, K_MSEC(50)) == 0)
        {
            // 3. Интерпретируем данные как массив float и берем нужное по индексу
            float *values = (float *)raw_msg_buffer;
            
            *_val = values[handler->system_index];
            return 0;
        }          
    
   return -ENODEV; /* Модуль обслуживания параметра не скомпилирован */
}

SYS_INIT(system_bus_cache_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
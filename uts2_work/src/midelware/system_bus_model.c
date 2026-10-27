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










int bus_set_u32p(SYSTEM_BUS_ID id,  uint32_t * _val)
{
    return 0;
}

int bus_set_can(SYSTEM_BUS_ID id,  system_can_message_t * _val)
{
    const struct system_bus_handler *handler = _get_valid_handler(id);
    if (false
        || handler == NULL 
        || _val == NULL
    ) 
    {
        return -EINVAL;
    }          
    int ret = zbus_chan_pub(handler->channel, _val, K_MSEC(50));
    return ret;        
}

int bus_get_can(SYSTEM_BUS_ID id,  system_can_message_t * _val)
{

const struct system_bus_handler *handler = _get_valid_handler(id);
    if (false
        || handler == NULL 
        || _val == NULL
    ) 
    {
        return -EINVAL;
    } 
     const system_can_message_t *channel_msg = (const system_can_message_t *)zbus_chan_msg(handler->channel);
    if (channel_msg == NULL)
    {
        return -ENODEV;
    }   
     *_val = *channel_msg;  
    return 0;
}


    
/* Внутренний помощник для валидации и получения доступа к каналу */
static inline bool _validate_and_claim_handler(SYSTEM_BUS_ID id, SYSTEM_BUS_DATA_TYPE _type, const struct system_bus_handler **out_handler, void **out_msg)
{
    if (id >= SYSTEM_BUS_COUNT) {
        return false;
    }

    // Проверяем что декспритор канала существует и канал нужного типа
    const struct system_bus_handler *handler = cache[id];
    if (handler == NULL || handler->channel_type != _type) {
        return false;
    }

    // Проверям что индекс канал не больше чем его размер
    size_t msg_size = zbus_chan_msg_size(handler->channel);
    if ((handler->system_index + 1) * sizeof(bool) > msg_size) {
        return false;
    }

    // Захватываем данные канала Zbus с таймаутом
    if (zbus_chan_claim(handler->channel, K_MSEC(10)) != 0) {
        return false;
    }

    *out_handler = handler;
    *out_msg = zbus_chan_msg(handler->channel);
    return true;
}




int bus_set_bool(SYSTEM_BUS_ID id, bool _val)
{
    const struct system_bus_handler *handler;
    void *raw_msg;

    if (!_validate_and_claim_handler(id, BOOL_DATA,&handler, &raw_msg)) {
        return -ENODEV;
    }

    // Записываем данные
    bool *msg = (bool *)raw_msg;
    msg[handler->system_index] = _val;

    // Освобождаем канал и уведомляем подписчиков Zbus
    zbus_chan_finish(handler->channel);
    zbus_chan_notify(handler->channel, K_NO_WAIT);

    return 0;
}

int bus_get_bool(SYSTEM_BUS_ID id, bool *_val)
{
    if (_val == NULL) 
    {
        return -EINVAL;
    }

    const struct system_bus_handler *handler;
    void *raw_msg;

    if (!_validate_and_claim_handler(id, BOOL_DATA,&handler, &raw_msg)) {
        return -ENODEV;
    }

    // Читаем данные
    const bool *msg = (const bool *)raw_msg;
    *_val = msg[handler->system_index];

    // Освобождаем канал
    zbus_chan_finish(handler->channel);
    return 0;
}

int bus_get_u32(SYSTEM_BUS_ID id, uint32_t * _val)
{
    if (_val == NULL) 
    {
        return -EINVAL;
    }
    

    const struct system_bus_handler *handler;
    void *raw_msg;

    if (!_validate_and_claim_handler(id, UINT_DATA,&handler, &raw_msg)) 
    {
        return -ENODEV;
    }

    // Читаем данные
    const uint32_t *msg = (const uint32_t *)raw_msg;
    *_val = msg[handler->system_index];

    // Освобождаем канал
    zbus_chan_finish(handler->channel);
    return 0;
                   
}


int bus_set_u32(SYSTEM_BUS_ID id, uint32_t _val)
{

    const struct system_bus_handler *handler;
    void *raw_msg;

    if (!_validate_and_claim_handler(id, UINT_DATA,&handler, &raw_msg)) {
        return -ENODEV;
    }

    uint32_t *msg = (uint32_t *)raw_msg;
    msg[handler->system_index] = _val;

    // Освобождаем канал и уведомляем подписчиков Zbus
    zbus_chan_finish(handler->channel);
    zbus_chan_notify(handler->channel, K_NO_WAIT);

    return 0;
}


/**
 * @brief Установить (в единицу) конкретный бит (0..31) по системному ID
 */
int bus_set_bit(SYSTEM_BUS_ID id, uint8_t bit_index)
{
    if (bit_index > 31) {
        return -EINVAL;
    }

    uint32_t val = 0;
    int ret = bus_get_u32(id, &val);
    if (ret < 0) {
        return ret; // Возвращает -ENODEV или -EINVAL, если ID не валиден
    }

    // Устанавливаем бит в 1
    val |= (1U << bit_index);

    // Записываем обратно и уведомляем подписчиков Zbus
    return bus_set_u32(id, val);
}

/**
 * @brief Сбросить (в ноль) конкретный бит (0..31) по системному ID
 */
int bus_clear_bit(SYSTEM_BUS_ID id, uint8_t bit_index)
{
    if (bit_index > 31) {
        return -EINVAL;
    }

    uint32_t val = 0;
    int ret = bus_get_u32(id, &val);
    if (ret < 0) {
        return ret;
    }

    // Сбрасываем бит в 0
    val &= ~(1U << bit_index);

    // Записываем обратно и уведомляем подписчиков Zbus
    return bus_set_u32(id, val);
}

/**
 * @brief Безопасно установить бит в 1, если он еще не был установлен
 */
int bus_set_bit_if_needed(SYSTEM_BUS_ID id, uint8_t bit_index)
{
    if (bit_index > 31) {
        return -EINVAL;
    }

    uint32_t val = 0;
    int ret = bus_get_u32(id, &val);
    if (ret < 0) {
        return ret;
    }

    // Проверяем, установлен ли бит уже в 1
    bool is_already_set = (val & (1U << bit_index)) != 0;
    if (is_already_set) {
        return 0; // Бит уже в нужном состоянии, ничего не делаем (экономим трафик Zbus)
    }

    // Устанавливаем бит в 1 и обновляем через сеттер
    val |= (1U << bit_index);
    return bus_set_u32(id, val);
}

/**
 * @brief Безопасно сбросить бит в 0, если он еще не был сброшен
 */
int bus_clear_bit_if_needed(SYSTEM_BUS_ID id, uint8_t bit_index)
{
    if (bit_index > 31) {
        return -EINVAL;
    }

    uint32_t val = 0;
    int ret = bus_get_u32(id, &val);
    if (ret < 0) {
        return ret;
    }

    // Проверяем, сброшен ли бит уже в 0
    bool is_already_cleared = (val & (1U << bit_index)) == 0;
    if (is_already_cleared) {
        return 0; // Бит уже 0, ничего не делаем
    }

    // Сбрасываем бит в 0 и обновляем через сеттер
    val &= ~(1U << bit_index);
    return bus_set_u32(id, val);
}
 
 int bus_set_real(SYSTEM_BUS_ID id, float _val)
{


    const struct system_bus_handler *handler;
    void *raw_msg;

    if (!_validate_and_claim_handler(id, FLOAT_DATA,&handler, &raw_msg)) {
        return -ENODEV;
    }

    float *msg = (float *)raw_msg;
    msg[handler->system_index] = _val;

    // Освобождаем канал и уведомляем подписчиков Zbus
    zbus_chan_finish(handler->channel);
    zbus_chan_notify(handler->channel, K_NO_WAIT);

    return 0;    
}


int bus_get_real(SYSTEM_BUS_ID id, float * _val)
{
  
    if (_val == NULL) 
    {
        return -EINVAL;
    }
    

    const struct system_bus_handler *handler;
    void *raw_msg;

    if (!_validate_and_claim_handler(id, FLOAT_DATA,&handler, &raw_msg)) 
    {
        return -ENODEV;
    }

    // Читаем данные
    const float *msg = (const float *)raw_msg;
    *_val = msg[handler->system_index];

    // Освобождаем канал
    zbus_chan_finish(handler->channel);
    return 0;

}

SYS_INIT(system_bus_cache_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);



int bus_listener_attach(const struct zbus_observer *listener, SYSTEM_BUS_ID id)
{
    const struct system_bus_handler *handler = _get_valid_handler(id);
    if (handler == NULL || handler->channel == NULL || listener == NULL) {
        return -EINVAL;
    }

    // Добавляем слушателя к каналу динамически в рантайме
    // Требует включенного CONFIG_ZBUS_RUNTIME_OBSERVERS=y в prj.conf
    return zbus_chan_add_obs(handler->channel, listener, K_NO_WAIT);
}
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/uart.h>
#include "system_define.h"




#define EEPROM_EVENT_CH EVENT_1_REG

typedef enum
{
    EEPORM_IU_ERROR         = 0x0001 << 0,
    EEPORM_CU_ERROR         = 0x0001 << 1,
    FRAM_MU_ERROR           = 0x0001 << 3,
    ADS1115_IU_ERROR        = 0x0001 << 4,
    AD5243_IU_ERROR         = 0x0001 << 5,
    TMP112_MU_ERROR         = 0x0001 << 6,
    TMP112_IU_ERROR         = 0x0001 << 7,
    BH1750_1_IU_ERROR       = 0x0001 << 8,
    BH1750_2_IU_ERROR       = 0x0001 << 9,
 
} hw_error_channel_1_t;


#define AD5243_ERROR_CH   HARDWARE_ERROR_REGISTER1
#define EEPROM_ERROR_CH   HARDWARE_ERROR_REGISTER1
#define I2C_SENS_ERROR_CH HARDWARE_ERROR_REGISTER1

typedef enum
{    
    EVENT_EEPROM_IU_WRITE_OK    = 0x0001 << 0,
    EVENT_EEPROM_IU_WRITE_ERROR = 0x0001 << 1,
    EVENT_EEPROM_IU_CRC_ERROR   = 0x0001 << 2,
    EVENT_EEPROM_CU_WRITE_OK    = 0x0001 << 3,
    EVENT_EEPROM_CU_WRITE_ERROR = 0x0001 << 4,
    EVENT_EEPROM_CU_CRC_ERROR   = 0x0001 << 5,    

    ADS1115_IU_WARNNIG      = 0x0001 << 3,
    BRD_LOW_CUR_SEN_WARNING = 0x0001 << 4,
    
} HARDWARE_WARNING_REGISTER1_t;


typedef enum
{
   LIN_PULL_DOWN_OFF,
   LIN_PULL_DOWN_ON,
} lin_pull_down_state_t;

typedef enum
{
    STATE_IN = 0,
    STATE_LOW = 1,
    STATE_HIGH = 2,
    STATE_SHORT = 3,
} LOW_CUR_OUT_STATE_t;



#define POWER_CONTROL_LINE_CNT 8

typedef enum
{  
  QUEUE_DATA,
  BOOL_DATA,
  UINT_DATA,
  FLOAT_DATA,
  CAN_MSG_DATA,
} SYSTEM_BUS_DATA_TYPE;



typedef struct {
    uint32_t id;          // CAN Identifier (Standard / Extended)
    uint8_t  dlc;         // Data Length Code / или реальная длина в байтах (0-64)
    uint8_t  flags;       // Флаговая маска (CAN_FD_FRAME, CAN_IDE и т.д.)
    uint8_t  data[64];    // Полезная нагрузка CAN-FD
} system_can_message_t;





struct system_bus_handler
{
    SYSTEM_BUS_ID         bus_id;  //ID параметра
    const struct zbus_channel   *channel; // Ссылка на канал
    SYSTEM_BUS_DATA_TYPE  channel_type;    
    uint32_t              system_index;
    const struct k_msgq   *queue;
};

typedef union erver_data
{
    bool sd_bool;
    uint32_t sd_uint;
    float sd_float;
} server_data_t;

typedef struct server_request
{
    server_data_t value;
    SYSTEM_BUS_ID id;
    struct k_sem * sync;
    uint8_t * response;
} server_request;


/* 
 * Макрос для регистрации параметра. 
 * Сразу принимает имя Zbus-канала (например, chan_ain_ao1).
 */
#define SERVER_CHANNEL_DEFINE(id, chan_ptr, index, chan_type) \
    const struct system_bus_handler __attribute__((section("param_routes"), used)) route_##id = { \
        .bus_id = id, \
        .channel = chan_ptr, \
        .channel_type = chan_type, \
        .system_index = index, \
        .queue = NULL, \
    }

#define SERVER_IN_CHANNEL_DEFINE(id, chan_ptr, index, chan_type) \
    const struct system_bus_handler __attribute__((section("param_routes"), used)) route_##id = { \
        .bus_id = id, \
        .channel = chan_ptr, \
        .channel_type = chan_type, \
        .system_index = index, \
        .queue = NULL, \
    }


#define SERVER_OUT_CHANNEL_DEFINE(id, chan_ptr, index, chan_type) \
    const struct system_bus_handler __attribute__((section("param_routes"), used)) route_##id = { \
        .bus_id = id, \
        .channel = chan_ptr, \
        .channel_type = chan_type, \
        .system_index = index, \
        .queue = NULL, \
    }




#define SERVER_OUT_QUEUE_ONLY_CHANNEL_DEFINE(id, chan_ptr, chan_type) \
    const struct system_bus_handler __attribute__((section("param_routes"), used)) route_##id = { \
        .bus_id = id, \
        .channel = NULL, \
        .channel_type = chan_type, \
        .system_index = 0, \
        .queue = chan_ptr, \
    }

 int serever_set_bool(SYSTEM_BUS_ID id, bool _val,     struct k_sem * sync, uint8_t * ret);
 int serever_set_u32( SYSTEM_BUS_ID id, uint32_t _val, struct k_sem * sync, uint8_t * ret);
 int serever_set_real(SYSTEM_BUS_ID id, float _val,    struct k_sem * sync, uint8_t * ret);
 

#define SERVER_CH_SET_ASYC(id, val) _Generic((val),           \
        bool:       serever_set_bool(id, val, NULL, NULL),    \
        uint32_t:   serever_set_u32(id, val, NULL, NULL),     \
        float:      serever_set_real(id, val, NULL, NULL)     \
    )

#define SERVER_CH_SET_SYC(id, val, sync, ret) _Generic((val), \
        bool:       serever_set_bool(id, val, sync, ret),     \
        uint32_t:   serever_set_u32(id, val, sync, ret),      \
        float:      serever_set_real(id, val,  sync, ret)     \
    )


int server_get_real_fast(SYSTEM_BUS_ID id, float * _val);


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


int bus_get_bool_ch(SYSTEM_BUS_ID id, bool * _val,uint8_t size);
int bus_get_u32_ch(SYSTEM_BUS_ID id,  uint32_t * _val,uint8_t size);
int bus_get_real_ch(SYSTEM_BUS_ID id, float * _val,uint8_t size);

// 2. Пишем макрос, который смотрит на тип указателя `val`
#define SYSTEM_BUS_GET_CHANNEL(id, val, size) _Generic(*(val), \
    bool:     bus_get_bool_ch, \
    int:      bus_get_u32_ch,  \
    unsigned: bus_get_u32_ch,  \
    float:    bus_get_real_ch  \
)((id), (val), (size))

int bus_set_bit(SYSTEM_BUS_ID id, uint8_t bit_index);
int bus_clear_bit(SYSTEM_BUS_ID id, uint8_t bit_index);
int bus_set_bit_if_needed(SYSTEM_BUS_ID id, uint8_t bit_index);
int bus_clear_bit_if_needed(SYSTEM_BUS_ID id, uint8_t bit_index);
int bus_get_bit(SYSTEM_BUS_ID id, uint8_t bit_index, bool *bit_val);
int bus_set_mask(SYSTEM_BUS_ID id, uint32_t mask);
int bus_clear_mask(SYSTEM_BUS_ID id, uint32_t mask);


#define SERVER_CH_SET_BIT( id, mask) bus_set_bit(id, mask)
#define SERVER_CH_CLEAR_MASK( id, mask) bus_clear_mask(id, mask)
#define SERVER_CH_SET_MASK( id, mask) bus_set_mask(id, mask)
#define SERVER_CH_CLEAR_BIT( id, mask) bus_clear_bit(id, mask)
#define SERVER_CH_SET_BIT_IF_NEEDED( id, mask) bus_set_bit_if_needed(id, mask)
#define SERVER_CH_CLEAR_BIT_IF_NEEDED( id, mask) bus_clear_bit_if_needed(id, mask)
#define SERVER_CH_GET_BIT(id, mask, val) bus_get_bit(id, mask, val)

int bus_set_can(SYSTEM_BUS_ID id,  system_can_message_t * _val);

#define SYSTEM_BUS_SET_P(id, val) _Generic(*(val), \
    system_can_message_t: bus_set_can              \
)((id), (val))



int bus_get_can(SYSTEM_BUS_ID id,  system_can_message_t * _val);

#define SYSTEM_BUS_GET_P(id, val) _Generic(*(val), \
    system_can_message_t: bus_get_can              \
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
    

int bus_listener_attach(const struct zbus_observer *listener, SYSTEM_BUS_ID id);


static inline uint32_t uart_detect_pack_cfg(uint32_t baudrate, 
                                            enum uart_config_parity parity,
                                            enum uart_config_stop_bits stop,
                                            enum uart_config_data_bits data)
{
    return (baudrate & 0x00FFFFFF) |
           (((uint32_t)parity & 0x07) << 24) |
           (((uint32_t)stop & 0x03) << 27) |
           (((uint32_t)data & 0x07) << 29);
}

static inline uint32_t uart_detect_unpack_baud(uint32_t cfg)
{
    return cfg & 0x00FFFFFF;
}

static inline enum uart_config_parity uart_detect_unpack_parity(uint32_t cfg)
{
    return (enum uart_config_parity)((cfg >> 24) & 0x07);
}

static inline enum uart_config_stop_bits uart_detect_unpack_stop(uint32_t cfg)
{
    return (enum uart_config_stop_bits)((cfg >> 27) & 0x03);
}

static inline enum uart_config_data_bits uart_detect_unpack_data(uint32_t cfg)
{
    return (enum uart_config_data_bits)((cfg >> 29) & 0x07);
}


#ifdef __cplusplus
}
#endif
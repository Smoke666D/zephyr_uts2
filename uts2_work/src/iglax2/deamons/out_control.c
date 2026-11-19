#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/shell/shell.h>

#include "system_define.h"
//#include "system_bus_model.h"
//#include "app_worker.

LOG_MODULE_REGISTER(input_control_module, LOG_LEVEL_INF);

#define INPUTS_CNT 11

typedef struct inputs_msg 
{
    bool state[INPUTS_CNT]; // Наш тестовый буфер (8 шагов * 2 канала)
} inputs_msg_t;


typedef enum {
    DIN_CONFIG_NEGATIVE = 0U,
    DIN_CONFIG_POSITIVE = 1U,    
	DIN_IDLE   = 4,
} DIN_INPUT_TYPE;

typedef struct dinputs_desc
{
    uint32_t      ulCounter;
    uint32_t      ulLowCounter;
    uint32_t      ulHighCounter;
    bool          ucValue;
    bool          ucReadBit;
    uint8_t       ucTempValue;
    DIN_INPUT_TYPE eInputType;
    bool (*getPortCallback)(uint8_t );
};







ZBUS_CHAN_DEFINE(din_chan,
                 inputs_msg_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

static int _din_control_init(void)
{

    return 0;
}


SYS_INIT(_din_control_init, DIN_DEAMON_INIT_LEVEL, DIN_DEAMON_INIT_PRIOR);
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "scpi/ieee488.h"
#include <zephyr/kernel.h>
#include "display.h"
#include "system_bus_model.h"

/* ==================== RED LED ==================== */
scpi_result_t cmd_led_red(scpi_t *context) 
{
    scpi_bool_t val;

    if (!SCPI_ParamBool(context, &val, TRUE)) {
        return SCPI_RES_ERR;
    }    
    SERVER_CH_SET_ASYC(LED1, (bool)val);
    
    return SCPI_RES_OK;
}

scpi_result_t cmd_led_red_q(scpi_t *context)
{
    bool state_r;
    SYSTEM_BUS_GET(LED1, &state_r);
    SCPI_ResultBool(context, state_r);
    return SCPI_RES_OK;
}

/* ==================== GREEN LED ==================== */
scpi_result_t cmd_led_green(scpi_t *context) 
{
    scpi_bool_t val;
    if (!SCPI_ParamBool(context, &val, TRUE)) {
        return SCPI_RES_ERR;
    }
    SERVER_CH_SET_ASYC(LED2, (bool)val);
    return SCPI_RES_OK;
}

scpi_result_t cmd_led_green_q(scpi_t *context) 
{
    bool state_g;
    SYSTEM_BUS_GET(LED2, &state_g);
    SCPI_ResultBool(context, state_g);
    return SCPI_RES_OK;
}

/* ==================== BLUE LED ==================== */
scpi_result_t cmd_led_blue(scpi_t *context) 
{
    scpi_bool_t val;
    if (!SCPI_ParamBool(context, &val, TRUE)) {
        return SCPI_RES_ERR;
    }
    SERVER_CH_SET_ASYC(LED3, (bool)val);
    return SCPI_RES_OK;
}

scpi_result_t cmd_led_blue_q(scpi_t *context) 
{
    bool state_b;
    SYSTEM_BUS_GET(LED3, &state_b);
    SCPI_ResultBool(context, state_b);
    return SCPI_RES_OK;
}

scpi_result_t cmd_led_reset(scpi_t *context)
{    
    SERVER_CH_SET_ASYC(LED1, (bool)false);
    SERVER_CH_SET_ASYC(LED2, (bool)false);
    SERVER_CH_SET_ASYC(LED3, (bool)false);
}

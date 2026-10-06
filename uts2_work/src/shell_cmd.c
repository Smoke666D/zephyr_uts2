#include <stdint.h>
#include <stdlib.h>  /* Для strtol */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "system_bus_model.h"
#include <zephyr/zbus/zbus.h>
#include "out_and_power_control.h"

 /* ------------------------------------------------------------------ */
/*  Аналогоавые выхода                                                */
/* ------------------------------------------------------------------ */    
static int cmd_dac_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) 
    {        
        return -EINVAL;
    }

     /* Парсим вещественное значение напряжения (float) из аргумента */
    char *endptr;
    float voltage = strtof(argv[1], &endptr);
    
    /* Проверяем корректность парсинга и границы диапазона */
    if (*endptr != '\0' || voltage < 0.0f || voltage > 30.0f) 
    {
        shell_error(sh, "Invalid voltage: %s (Must be between 0.0 and 30 V)", argv[1]);
        return -EINVAL;
    }

    SYSTEM_BUS_SET(DAC_VALUE,voltage);
    
    return 0;
}

SHELL_CMD_REGISTER(dac_set, NULL, 
                   "Set DAC output value: dac_set <0-4095>", 
                   cmd_dac_set);

uint32_t adj_res_name[] =
{
    ADJ_RES1,
    ADJ_RES2,  
};


static int cmd_adj_res_set(const struct shell *sh, size_t argc, char **argv)
{
    LOW_CUR_OUT_STATE_t out_state;
    /* Проверяем количество переданных аргументов (команда + 2 параметра) */
    if (argc != 3)
    {
        shell_error(sh, "Usage: discrete_set <1-%d> <ON/OFF>", 2 );
        return -EINVAL;
    }
    
    char *endptr;
    long channel_num = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || channel_num < 1 || channel_num > 2 ) 
    {
        shell_error(sh, "Invalid channel: %s (Must be 1 to %d)", argv[1], 2 );
        return -EINVAL;
    }


     /* Парсим вещественное значение напряжения (float) из аргумента */
  
    float resistance = strtof(argv[2], &endptr);
    
    /* Проверяем корректность парсинга и границы диапазона */
    if (*endptr != '\0' || resistance < 0.0f || resistance > 100.0f) 
    {
        shell_error(sh, "Invalid voltage: %s (Must be between 0.0 and 100.0 kOm)", argv[2]);
        return -EINVAL;
    }

    
    /* 4. Выполняем безопасный атомарный Zero-Copy доступ к каналу Zbus [2] */
    SYSTEM_BUS_SET(adj_res_name[channel_num-1],resistance);
    return 0;
}

SHELL_CMD_REGISTER(adj_res_set, NULL, 
                   "Set state of out: out_set <1-18> <IN/HI/LO>", 
                   cmd_adj_res_set);                   



uint32_t name[] =
{
    EN_DUT2_PSU,
    EN_DUT3_PSU,  
    EN_P12V,
    EN_P24V,
    EN_VA,
    EN_USB_OUT,
    EN_USB_BOOT,
    EN_USB_TOP,
};



static int cmd_discrete_set(const struct shell *sh, size_t argc, char **argv)
{
    /* Проверяем количество переданных аргументов (команда + 2 параметра) */
    if (argc != 3)
    {
        shell_error(sh, "Usage: discrete_set <1-%d> <ON/OFF>", CONTROL_LINE_CNT);
        return -EINVAL;
    }

    /* 2. Парсим логический номер выхода (1 .. CONTROL_LINE_CNT) */
    char *endptr;
    long channel_num = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || channel_num < 1 || channel_num > CONTROL_LINE_CNT) {
        shell_error(sh, "Invalid channel: %s (Must be 1 to %d)", argv[1], CONTROL_LINE_CNT);
        return -EINVAL;
    }

    /* 3. Парсим желаемое состояние (ON/OFF) */
    bool state;
    if (strcmp(argv[2], "ON") == 0 || strcmp(argv[2], "on") == 0) {
        state = true;
    } else if (strcmp(argv[2], "OFF") == 0 || strcmp(argv[2], "off") == 0) {
        state = false;
    } else {
        shell_error(sh, "Invalid state: %s (Must be ON or OFF)", argv[2]);
        return -EINVAL;
    }

    /* 4. Выполняем безопасный атомарный Zero-Copy доступ к каналу Zbus [2] */
    SYSTEM_BUS_SET(name[channel_num-1],(bool)state);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 5. РЕГИСТРАЦИЯ КОМАНДЫ В СИСТЕМЕ SHELL                             */
/* ------------------------------------------------------------------ */

SHELL_CMD_REGISTER(en_set, NULL, 
                   "Set state of a discrete channel: discrete_set <1-22> <ON/OFF>", 
                   cmd_discrete_set);
                   





/* ------------------------------------------------------------------ */
/*  Светодиоды                                                        */
/* ------------------------------------------------------------------ */

uint32_t led_name[] =
{
    LED1,
    LED2,  
    LED3,    
};


static int cmd_led_set(const struct shell *sh, size_t argc, char **argv)
{
    /* Проверяем количество переданных аргументов (команда + 2 параметра) */
    if (argc != 3)
    {
        shell_error(sh, "Usage: discrete_set <1-%d> <ON/OFF>", LED_CNT );
        return -EINVAL;
    }
    
    char *endptr;
    long channel_num = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || channel_num < 1 || channel_num > LED_CNT ) 
    {
        shell_error(sh, "Invalid channel: %s (Must be 1 to %d)", argv[1], LED_CNT );
        return -EINVAL;
    }

    bool state;
    if (strcmp(argv[2], "ON") == 0 || strcmp(argv[2], "on") == 0) 
    {
        state = true;
    } 
    else 
    if (strcmp(argv[2], "OFF") == 0 || strcmp(argv[2], "off") == 0) 
    {
        state = false;
    } 
    else 
    {
        shell_error(sh, "Invalid state: %s (Must be ON or OFF)", argv[2]);
        return -EINVAL;
    }

    /* 4. Выполняем безопасный атомарный Zero-Copy доступ к каналу Zbus [2] */
    SYSTEM_BUS_SET(led_name[channel_num-1],(bool)state);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 5. РЕГИСТРАЦИЯ КОМАНДЫ В СИСТЕМЕ SHELL                             */
/* ------------------------------------------------------------------ */

SHELL_CMD_REGISTER(led_set, NULL, 
                   "Set state of LED: discrete_set <1-3> <ON/OFF>", 
                   cmd_led_set);



/* ------------------------------------------------------------------ */
/*  Дискетные выхода                                                  */
/* ------------------------------------------------------------------ */

uint32_t out_name[] =
{
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
    DOUT18 
};


static int cmd_out_set(const struct shell *sh, size_t argc, char **argv)
{
    LOW_CUR_OUT_STATE_t out_state;
    /* Проверяем количество переданных аргументов (команда + 2 параметра) */
    if (argc != 3)
    {
        shell_error(sh, "Usage: out_set <1-18> <IN/HI/LO>");
        return -EINVAL;
    }
    
    char *endptr;
    long channel_num = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || channel_num < 1 || channel_num > CHANNEL_DATA_SIZE ) 
    {
        shell_error(sh, "Invalid channel: %s (Must be 1 to %d)", argv[1], CHANNEL_DATA_SIZE );
        return -EINVAL;
    }

    bool state;
    if (strcmp(argv[2], "IN") == 0 || strcmp(argv[2], "in") == 0) 
    {
        out_state = STATE_IN;
    } 
    else 
    if (strcmp(argv[2], "HI") == 0 || strcmp(argv[2], "hi") == 0) 
    {
        out_state = STATE_HIGH;
    } 
    else
    if (strcmp(argv[2], "LO") == 0 || strcmp(argv[2], "lo") == 0) 
    {
        out_state = STATE_LOW;
    } 
    else 
    {
        shell_error(sh, "Invalid state: %s (Must be ON or OFF)", argv[2]);
        return -EINVAL;
    }

    /* 4. Выполняем безопасный атомарный Zero-Copy доступ к каналу Zbus [2] */
    SYSTEM_BUS_SET(out_name[channel_num-1],(uint32_t)out_state);
    return 0;
}

SHELL_CMD_REGISTER(out_set, NULL, 
                   "Set state of out: out_set <1-18> <IN/HI/LO>", 
                   cmd_out_set);


                   

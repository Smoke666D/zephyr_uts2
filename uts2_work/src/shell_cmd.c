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
    SYSTEM_BUS_SET(adj_res_name[channel_num-1],(float)resistance);
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


uint32_t linpb_name[] =
{
    LIN_PD1,
    LIN_PD2,  
    LIN_PD3,    
    LIN_PD4,

};


static int cmd_linpb_set(const struct shell *sh, size_t argc, char **argv)
{
    /* Проверяем количество переданных аргументов (команда + 2 параметра) */
    if (argc != 3)
    {
        shell_error(sh, "Usage: linpb_set <1-4> <ON/OFF>");
        return -EINVAL;
    }
    
    char *endptr;
    long channel_num = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || channel_num < 1 || channel_num > 4 ) 
    {
        shell_error(sh, "Invalid channel: %s (Must be 1 to %d)", argv[1], 4 );
        return -EINVAL;
    }

    uint32_t state;
    if (strcmp(argv[2], "ON") == 0 || strcmp(argv[2], "on") == 0) 
    {
        state = 0x01;
    } 
    else 
    if (strcmp(argv[2], "OFF") == 0 || strcmp(argv[2], "off") == 0) 
    {
        state = 0x00;
    } 
    else 
    {
        shell_error(sh, "Invalid state: %s (Must be ON or OFF)", argv[2]);
        return -EINVAL;
    }

    /* 4. Выполняем безопасный атомарный Zero-Copy доступ к каналу Zbus [2] */
    SYSTEM_BUS_SET(linpb_name[channel_num-1],state);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 5. РЕГИСТРАЦИЯ КОМАНДЫ В СИСТЕМЕ SHELL                             */
/* ------------------------------------------------------------------ */

SHELL_CMD_REGISTER(linpb_set, NULL, 
                   "Set state of linpb: linpb_set <1-4> <ON/OFF>", 
                   cmd_linpb_set);


                   
static int cmd_can_send(const struct shell *sh, size_t argc, char **argv) {
    // Аргументы: can_send <can_num> <id_hex> <data_bytes_hex...>
    int can_num = atoi(argv[1]);
    uint32_t id = strtoul(argv[2], NULL, 16);
    
    system_can_message_t msg = {
        .id = id,
        .dlc = argc - 3, // количество байт данных
        .flags = 0,
    };

    for (size_t i = 3; i < argc && (i - 3) < 64; i++) {
        msg.data[i - 3] = (uint8_t)strtoul(argv[i], NULL, 16);
    }

    int target_bus = (can_num == 1) ? CAN1_TX : (can_num == 2) ? CAN2_TX : CAN3_TX;
    int ret = SYSTEM_BUS_SET_P(target_bus, &msg);
    
    if (ret < 0) {
        shell_print(sh, "CAN send error: %d", ret);
    } else {
        shell_print(sh, "CAN%d sent ID: 0x%X", can_num, id);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_can,
    SHELL_CMD(send, NULL, "Send CAN frame: can send <num> <id> <b1> <b2>...", cmd_can_send),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(can, &sub_can, "CAN bus commands", NULL);


/* 
 * Обработчик команд консоли для отправки данных в LIN/UART каналы.
 * Синтаксис: lin_send <канал 1-4> <байт1> [байт2] [байт3] ...
 */
static int cmd_lin_send(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Ошибка: Неверное число аргументов.");
        shell_print(sh, "Использование: lin_send <1-4> <байт1> [байт2] ...");
        return -EINVAL;
    }

    // 1. Парсим номер канала (1, 2, 3 или 4)
    int channel = atoi(argv[1]);
    if (channel < 1 || channel > 4) {
        shell_error(sh, "Ошибка: Номер канала должен быть от 1 до 4.");
        return -EINVAL;
    }

    // 2. Определяем SYSTEM_BUS_ID для передачи в зависимости от канала
    SYSTEM_BUS_ID target_bus_id;
    switch (channel) {
        case 1: target_bus_id = LIN1_TX; break; // Замени на свои реальные ID из system_bus_model.h
        case 2: target_bus_id = LIN2_TX; break;
        case 3: target_bus_id = LIN3_TX; break;
        case 4: target_bus_id = LIN4_TX; break;
        default: return -EINVAL;
    }

    shell_print(sh, "Отправка в канал LIN %d (аргументов: %zu)...", channel, argc - 2);

    // 3. Парсим все последующие аргументы как байты данных
    for (size_t i = 2; i < argc; i++) {
        // Поддерживаем как десятичный формат (например, 255), так и hex (например, 0xFF)
        char *endptr;
        unsigned long byte_val = strtoul(argv[i], &endptr, 0);

        if (*endptr != '\0' || byte_val > 0xFF) {
            shell_error(sh, "Ошибка: Некорректный байт '%s' (ожидается 0..255 или 0x00..0xFF)", argv[i]);
            return -EINVAL;
        }

        uint8_t byte_data = (uint8_t)byte_val;

        // 4. Перекладываем байт через твой макрос SYSTEM_BUS_SET
        // (Предполагается, что на стороне шины настроен обработчик u32 или raw для этих ID)
        int err = SYSTEM_BUS_SET(target_bus_id, (uint32_t)byte_data);
        if (err != 0) {
            shell_error(sh, "Ошибка отправки байта 0x%02X в шину (err: %d)", byte_data, err);
            return err;
        }

        shell_print(sh, "  -> Отправлен байт: 0x%02X (%u)", byte_data, byte_data);
    }

    shell_print(sh, "Пакет успешно передан в систему шин.");
    return 0;
}

/* 
 * Регистрация команды в подсистеме Zephyr Shell
 * Синтаксис макроса: SHELL_CMD_ARG(имя_команды, табуляция_подкомманд, справка, указатель_на_функцию, мин_аргументов, макс_аргументов)
 */
SHELL_CMD_ARG_REGISTER(lin_send, NULL, 
    "Отправить данные в LIN канал.\nИспользование: lin_send <1-4> <байт1> [байт2] ...", 
    cmd_lin_send, 3, SHELL_OPT_ARG_CHECK_SKIP);
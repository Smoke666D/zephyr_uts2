#include <stdint.h>
#include <stdlib.h>  /* Для strtol */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "system_bus_model.h"
#include <zephyr/zbus/zbus.h>
#include "shell_common.h"

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
        int err = SYSTEM_BUS_SET(target_bus_id, (float)byte_data);
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

/***************************************************************************************************
 *                                           DEFINITIONS & TABLES
 **************************************************************************************************/
#define TEST_TOTAL_CHANNELS  40
#define TEST_CHANNELS_PER_ROW 5
#define TEST_TOTAL_ROWS      8  // 8 рядов по 5 каналов = 40 каналов

/* Массив имен всех 40 каналов в точном порядке их вывода */
static const char *const test_channel_names[TEST_TOTAL_CHANNELS] = {
    "AIN_AO1",    "AIN_AO7",    "AIN_AO13",   "AIN_AVsense1", "LIN_AO1",
    "AIN_AO2",    "AIN_AO8",    "AIN_AO14",   "AIN_AVsense2", "LIN_AO2",
    "AIN_AO3",    "AIN_AO9",    "AIN_AO15",   "AIN_AVsense3", "LIN_AO3",
    "AIN_AO4",    "AIN_AO10",   "AIN_AO16",   "AIN_AVsense4", "LIN_AO4",
    "AIN_AO5",    "AIN_AO11",   "AIN_AO17",   "AIN_AVsense5", "DA41_GND1",
    "AIN_AO6",    "AIN_AO12",   "AIN_AO18",   "AIN_AVsense6", "DA41_GND2",
    "AIN_DA11_1", "AIN_DA20_1", "AIN_DA33_1", "AIN_DA44_1",   "AIN_DA41_1",
    "AIN_DA11_2", "AIN_DA20_2", "AIN_DA33_2", "AIN_DA44_2",   "AIN_DA41_2"
};

/* Универсальная функция получения float-значения по имени канала из вашей шины */
static float _get_bus_float(const char *name)
{
    float val = 0.0f;

    // Автоматический поиск и чтение через макросы системы (добавьте недостающие, если имена отличаются)
    if      (strcmp(name, "AIN_AO1") == 0)    { SYSTEM_BUS_GET(AIN_AO1, &val); }
    else if (strcmp(name, "AIN_AO2") == 0)    { SYSTEM_BUS_GET(AIN_AO2, &val); }
    else if (strcmp(name, "AIN_AO3") == 0)    { SYSTEM_BUS_GET(AIN_AO3, &val); }
    else if (strcmp(name, "AIN_AO4") == 0)    { SYSTEM_BUS_GET(AIN_AO4, &val); }
    else if (strcmp(name, "AIN_AO5") == 0)    { SYSTEM_BUS_GET(AIN_AO5, &val); }
    else if (strcmp(name, "AIN_AO6") == 0)    { SYSTEM_BUS_GET(AIN_AO6, &val); }
    else if (strcmp(name, "AIN_AO7") == 0)    { SYSTEM_BUS_GET(AIN_AO7, &val); }
    else if (strcmp(name, "AIN_AO8") == 0)    { SYSTEM_BUS_GET(AIN_AO8, &val); }
    else if (strcmp(name, "AIN_AO9") == 0)    { SYSTEM_BUS_GET(AIN_AO9, &val); }
    else if (strcmp(name, "AIN_AO10") == 0)   { SYSTEM_BUS_GET(AIN_AO10, &val); }
    else if (strcmp(name, "AIN_AO11") == 0)   { SYSTEM_BUS_GET(AIN_AO11, &val); }
    else if (strcmp(name, "AIN_AO12") == 0)   { SYSTEM_BUS_GET(AIN_AO12, &val); }
    else if (strcmp(name, "AIN_AO13") == 0)   { SYSTEM_BUS_GET(AIN_AO13, &val); }
    else if (strcmp(name, "AIN_AO14") == 0)   { SYSTEM_BUS_GET(AIN_AO14, &val); }
    else if (strcmp(name, "AIN_AO15") == 0)   { SYSTEM_BUS_GET(AIN_AO15, &val); }
    else if (strcmp(name, "AIN_AO16") == 0)   { SYSTEM_BUS_GET(AIN_AO16, &val); }
    else if (strcmp(name, "AIN_AO17") == 0)   { SYSTEM_BUS_GET(AIN_AO17, &val); }
    else if (strcmp(name, "AIN_AO18") == 0)   { SYSTEM_BUS_GET(AIN_AO18, &val); }

    else if (strcmp(name, "AIN_AVsense1") == 0) { SYSTEM_BUS_GET(AIN_AVsense1, &val); }
    else if (strcmp(name, "AIN_AVsense2") == 0) { SYSTEM_BUS_GET(AIN_AVsense2, &val); }
    else if (strcmp(name, "AIN_AVsense3") == 0) { SYSTEM_BUS_GET(AIN_AVsense3, &val); }
    else if (strcmp(name, "AIN_AVsense4") == 0) { SYSTEM_BUS_GET(AIN_AVsense4, &val); }
    else if (strcmp(name, "AIN_AVsense5") == 0) { SYSTEM_BUS_GET(AIN_AVsense5, &val); }
    else if (strcmp(name, "AIN_AVsense6") == 0) { SYSTEM_BUS_GET(AIN_AVsense6, &val); }

    else if (strcmp(name, "LIN_AO1") == 0) { SYSTEM_BUS_GET(LIN_AO1, &val); }
    else if (strcmp(name, "LIN_AO2") == 0) { SYSTEM_BUS_GET(LIN_AO2, &val); }
    else if (strcmp(name, "LIN_AO3") == 0) { SYSTEM_BUS_GET(LIN_AO3, &val); }
    else if (strcmp(name, "LIN_AO4") == 0) { SYSTEM_BUS_GET(LIN_AO4, &val); }

    else if (strcmp(name, "DA41_GND1") == 0) { SYSTEM_BUS_GET(DA41_GND1, &val); }
    else if (strcmp(name, "DA41_GND2") == 0) { SYSTEM_BUS_GET(DA41_GND2, &val); }

    else if (strcmp(name, "AIN_DA11_1") == 0) { SYSTEM_BUS_GET(AIN_DA11_test1, &val); }
    else if (strcmp(name, "AIN_DA20_1") == 0) { SYSTEM_BUS_GET(AIN_DA20_test1, &val); }
    else if (strcmp(name, "AIN_DA33_1") == 0) { SYSTEM_BUS_GET(AIN_DA33_test1, &val); }
    else if (strcmp(name, "AIN_DA44_1") == 0) { SYSTEM_BUS_GET(AIN_DA41_test1, &val); } // Проверьте суффикс _test1 для 44
    else if (strcmp(name, "AIN_DA41_1") == 0) { SYSTEM_BUS_GET(AIN_DA41_test1, &val); }

    else if (strcmp(name, "AIN_DA11_2") == 0) { SYSTEM_BUS_GET(AIN_DA11_test2, &val); }
    else if (strcmp(name, "AIN_DA20_2") == 0) { SYSTEM_BUS_GET(AIN_DA20_test2, &val); }
    else if (strcmp(name, "AIN_DA33_2") == 0) { SYSTEM_BUS_GET(AIN_DA33_test2, &val); }
    else if (strcmp(name, "AIN_DA44_2") == 0) { SYSTEM_BUS_GET(AIN_DA41_test2, &val); } // Проверьте суффикс _test2 для 44
    else if (strcmp(name, "AIN_DA41_2") == 0) { SYSTEM_BUS_GET(AIN_DA41_test2, &val); }

    return val;
}

/***************************************************************************************************
 *                                        COMMAND HANDLER
 **************************************************************************************************/

static int _cmd_ai_test(const struct shell *_sh, size_t _argc, char **_argv)
{
    ARG_UNUSED(_argc);
    ARG_UNUSED(_argv);

    shell_print(_sh, "=== AI_TEST MATRIX (40 channels: 5 per row, 8 rows) ===");

    for (int row = 0; row < TEST_TOTAL_ROWS; row++) 
    {
        char row_buf[384] = {0};
        int offset = 0;

        for (int col = 0; col < TEST_CHANNELS_PER_ROW; col++) 
        {
            int ch_index = row * TEST_CHANNELS_PER_ROW + col;
            if (ch_index >= TEST_TOTAL_CHANNELS) break;

            const char *name = test_channel_names[ch_index];
            float val = _get_bus_float(name);

            // Форматируем колонку: "Имя: Значение V" (с фикс. отступами для красоты)
            int written = snprintf(row_buf + offset, sizeof(row_buf) - offset, 
                                   "%-12s: %8.4f V   ", name, (double)val);
            if (written > 0) {
                offset += written;
            }
        }

        // Выводим готовый ряд из 5 каналов
        shell_print(_sh, "%s", row_buf);
    }

    shell_print(_sh, "=======================================================");
    return 0;
}

// Регистрация тестовой команды
SHELL_CMD_ARG_REGISTER(
    AI_TEST, 
    NULL, 
    "Print matrix of 40 float analog channels (5 columns x 8 rows).\n"
    "Usage: AI_TEST", 
    _cmd_ai_test, 
    1, 
    0
);
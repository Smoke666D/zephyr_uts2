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

#define NUM_I2C_OUT2_CHANNELS 9

/* Массив имен каналов (индекс 0 - заглушка) */
static const char *const i2c_out2_names[NUM_I2C_OUT2_CHANNELS + 1] = {
    "",
    "JTAG_MUX_EN",
    "EN_USB_UART",
    "EN_USB_OUT1",
    "EN_USB_OUT2",
    "EN_USB_OUT3",
    "UART_FB_EN",
    "UART_RX_EN",
    "OK2",
    "OK1"
};

/* Маппинг номера канала (1-9) на SYSTEM_BUS_ID */
static inline SYSTEM_BUS_ID _get_i2c_out2_bus_id(int ch_num)
{
    switch (ch_num) {
        case 1:  return JTAG_MUX_EN;
        case 2:  return EN_USB_UART;
        case 3:  return EN_USB_OUT1;
        case 4:  return EN_USB_OUT2;
        case 5:  return EN_USB_OUT3;
        case 6:  return UART_FB_EN;
        case 7:  return UART_RX_EN;
        case 8:  return OK2;
        case 9:  return OK1;
        default: return (SYSTEM_BUS_ID)-1;
    }
}

/***************************************************************************************************
 *                                        COMMAND HANDLER
 **************************************************************************************************/

static int _cmd_i2c_out2(const struct shell *_sh, size_t _argc, char **_argv)
{
    // ВЕТКА 1: Вывод статуса (если вызвано как "I2C_OUT2" или "I2C_OUT2 ?")
    if (_argc == 1 || (_argc == 2 && strcmp(_argv[1], "?") == 0)) 
    {
        shell_print(_sh, "Current I2C_OUT2 states:");
        for (int i = 1; i <= NUM_I2C_OUT2_CHANNELS; i++) 
        {
            bool state = false;
            SYSTEM_BUS_ID id = _get_i2c_out2_bus_id(i);
            
            if (SYSTEM_BUS_GET(id, &state) == 0) {
                shell_print(_sh, "  %d - %-13s: %s", i, i2c_out2_names[i], state ? "ON" : "OFF");
            } else {
                shell_print(_sh, "  %d - %-13s: [N/A]", i, i2c_out2_names[i]);
            }
        }
        return 0;
    }

    // ВЕТКА 2: Глобальное управление ВСЕМИ каналами (например: I2C_OUT2 ON или I2C_OUT2 OFF)
    if (_argc == 2) 
    {
        bool state = false;
        if (strcmp(_argv[1], "ON") == 0 || strcmp(_argv[1], "1") == 0) {
            state = true;
        } else if (strcmp(_argv[1], "OFF") == 0 || strcmp(_argv[1], "0") == 0) {
            state = false;
        } else {
            shell_error(_sh, "Error: Invalid global state. Use 'ON' or 'OFF'.");
            return -EINVAL;
        }

        shell_print(_sh, "Applying state %s to ALL I2C_OUT2 channels:", state ? "ON" : "OFF");
        for (int i = 1; i <= NUM_I2C_OUT2_CHANNELS; i++) {
            SYSTEM_BUS_ID id = _get_i2c_out2_bus_id(i);
            int ret = SYSTEM_BUS_SET(id, (bool)state);
            if (ret == 0) {
                shell_print(_sh, "  %s -> %s", i2c_out2_names[i], state ? "ON" : "OFF");
            } else {
                shell_error(_sh, "  %s -> Failed (err: %d)", i2c_out2_names[i], ret);
            }
        }
        return 0;
    }

    // ВЕТКА 3: Управление конкретным каналом (ожидаем: I2C_OUT2 <1-9> <ON|OFF>)
    if (_argc < 3) 
    {
        shell_error(_sh, "Invalid arguments.");
        shell_print(_sh, "Usage:");
        shell_print(_sh, "  I2C_OUT2                 - Show status of all channels");
        shell_print(_sh, "  I2C_OUT2 ?               - Show status of all channels");
        shell_print(_sh, "  I2C_OUT2 <ON|OFF>        - Turn ALL channels ON or OFF");
        shell_print(_sh, "  I2C_OUT2 <1-9> <ON|OFF>  - Turn specific channel ON or OFF");
        shell_print(_sh, "\nChannels map:");
        for (int i = 1; i <= NUM_I2C_OUT2_CHANNELS; i++) {
            shell_print(_sh, "  %d: %s", i, i2c_out2_names[i]);
        }
        return -EINVAL;
    }

    int ch_num = atoi(_argv[1]);
    if (ch_num < 1 || ch_num > NUM_I2C_OUT2_CHANNELS) 
    {
        shell_error(_sh, "Error: Invalid channel number. Use from 1 to %d.", NUM_I2C_OUT2_CHANNELS);
        return -EINVAL;
    }

    bool state = false;
    if (strcmp(_argv[2], "ON") == 0 || strcmp(_argv[2], "1") == 0) {
        state = true;
    } else if (strcmp(_argv[2], "OFF") == 0 || strcmp(_argv[2], "0") == 0) {
        state = false;
    } else {
        shell_error(_sh, "Error: Invalid state value. Use 'ON' or 'OFF'.");
        return -EINVAL;
    }

    SYSTEM_BUS_ID id = _get_i2c_out2_bus_id(ch_num);
    int ret = SYSTEM_BUS_SET(id, (bool)state);

    if (ret == 0) {
        shell_print(_sh, "%s (%d) set to %s", i2c_out2_names[ch_num], ch_num, state ? "ON" : "OFF");
    } else {
        shell_error(_sh, "Failed to state for %s (err: %d)", i2c_out2_names[ch_num], ret);
    }

    return 0;
}

// Регистрация команды I2C_OUT2 в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    I2C_OUT2, 
    NULL, 
    "Control I2C_OUT2 digital/mux channels (JTAG, USB, UART, OK).\n"
    "Usage:\n"
    "  I2C_OUT2                 - Show status of all channels\n"
    "  I2C_OUT2 ?               - Show status of all channels\n"
    "  I2C_OUT2 <ON|OFF>        - Turn ALL channels ON or OFF\n"
    "  I2C_OUT2 <1-9> <ON|OFF>  - Turn specific channel ON or OFF", 
    _cmd_i2c_out2, 
    1, 
    2
);
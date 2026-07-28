#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <string.h>

#include "seq_adc_processor.h"

// Структура сопоставляет имя канала с его индексом в массиве voltages (0..31)
typedef struct {
    const char *str_name;     // Строковое имя для ввода в консоли
    int         channel_idx;  // Индекс элемента в массиве processed_msg.voltages
} adc_shell_map_t;

// Таблица соответствия согласно перечислению AIN_MUX_CHANNEL_NUMBER
static const adc_shell_map_t shell_map[] = {
    {"AO1",        0},
    {"AO7",        1},
    {"AO13",       2},
    {"AVsense1",   3},
    {"AO2",        4},
    {"AO8",        5},
    {"AO14",       6},
    {"AVsense2",   7},
    {"AO3",        8},
    {"AO9",        9},
    {"AO15",       10},
    {"AVsense3",   11},
    {"AO4",        12},
    {"AO10",       13},
    {"AO16",       14},
    {"AVsense4",   15},
    {"AO5",        16},
    {"AO11",       17},
    {"AO17",       18},
    {"AVsense5",   19},
    {"AO6",        20},
    {"AO12",       21},
    {"AO18",       22},
    {"AVsense6",   23},
    {"DA11_test1", 24},
    {"DA20_test1", 25},
    {"DA33_test1", 26},
    {"DA44_test1", 27},
    {"DA11_test2", 28},
    {"DA20_test2", 29},
    {"DA33_test2", 30},
    {"DA44_test2", 31},
};

#define MAP_SIZE (sizeof(shell_map) / sizeof(shell_map[0]))

/**
 *  @brief Команда "get_adc <channel_name | ALL>"
 */
static int cmd_read_chan_by_friendly_name(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) 
    {
        shell_error(sh, "Usage: get_adc <channel_name | ALL>");
        return -EINVAL;
    }

    const char *requested_name = argv[1];

    // Считываем весь массив обработанных данных за один раз
    adc_processed_msg_t msg;
    int ret = zbus_chan_read(&adc_processed_chan, &msg, K_NO_WAIT);
    if (ret < 0) 
    {
        // Если воркер еще не опубликовал данные, инициализируем массив нулями
        memset(&msg, 0, sizeof(msg));
    }

    // Обработка специального запроса "ALL" или "all" — выгрузить всё одной строкой
    if (strcmp(requested_name, "ALL") == 0 || strcmp(requested_name, "all") == 0) 
    {
        for (int i = 0; i < TOTAL_CHANNELS_CNT; i++) 
        {
            shell_fprintf(sh, SHELL_NORMAL, "%.4f", (double)msg.voltages[i]);
            if (i < TOTAL_CHANNELS_CNT - 1) 
            {
                shell_fprintf(sh, SHELL_NORMAL, " ");
            }
        }
        shell_fprintf(sh, SHELL_NORMAL, "\n");
        return 0;
    }

    // Поиск конкретного одиночного канала по имени
    const adc_shell_map_t *found_entry = NULL;
    for (size_t i = 0; i < MAP_SIZE; i++) 
    {
        if (strcmp(shell_map[i].str_name, requested_name) == 0) 
        {
            found_entry = &shell_map[i];
            break;
        }
    }

    if (found_entry == NULL) 
    {
        shell_error(sh, "Error: Channel name '%s' is not defined", requested_name);
        return -EINVAL;
    }

    // Выводим значение конкретного элемента по его индексу
    shell_fprintf(sh, SHELL_NORMAL, "%.4f\n", (double)msg.voltages[found_entry->channel_idx]);
    return 0;
}

/**
 *  @brief Команда "get_adc_hex" (бинарный HEX-дубль)
 */
static int cmd_read_adc_hex(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    adc_processed_msg_t msg;
    int ret = zbus_chan_read(&adc_processed_chan, &msg, K_NO_WAIT);
    if (ret < 0) 
    {
        memset(&msg, 0, sizeof(msg));
    }

    // Выводим массив из 128 байт памяти как HEX-строку
    uint8_t *byte_ptr = (uint8_t *)msg.voltages;
    for (size_t i = 0; i < sizeof(msg.voltages); i++) 
    {
        shell_fprintf(sh, SHELL_NORMAL, "%02X", byte_ptr[i]);
    }
    shell_fprintf(sh, SHELL_NORMAL, "\n");

    return 0;
}

// Регистрация команд в Shell
SHELL_CMD_REGISTER(get_adc, NULL, "Read ADC channel by its string name or ALL", cmd_read_chan_by_friendly_name);
SHELL_CMD_REGISTER(get_adc_hex, NULL, "Read all 32 channels as raw HEX data", cmd_read_adc_hex);
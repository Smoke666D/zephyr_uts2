#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <string.h>

#include "seq_adc_processor.h"

// Структура для маппинга символьного имени на канал ZBus
typedef struct {
    const char *str_name;                 // Строковое имя для ввода в консоли
    const struct zbus_channel *zbus_chan; // Соответствующий ZBus-канал (0..31)
} adc_shell_map_t;

// Таблица соответствия согласно вашему перечислению AIN_MUX_CHANNEL_NUMBER
static const adc_shell_map_t shell_map[] = {
    {"AO1",        &adc_out_chan_0},
    {"AO7",        &adc_out_chan_1},
    {"AO13",       &adc_out_chan_2},
    {"AVsense1",   &adc_out_chan_3},
    {"AO2",        &adc_out_chan_4},
    {"AO8",        &adc_out_chan_5},
    {"AO14",       &adc_out_chan_6},
    {"AVsense2",   &adc_out_chan_7},
    {"AO3",        &adc_out_chan_8},
    {"AO9",        &adc_out_chan_9},
    {"AO15",       &adc_out_chan_10},
    {"AVsense3",   &adc_out_chan_11},
    {"AO4",        &adc_out_chan_12},
    {"AO10",       &adc_out_chan_13},
    {"AO16",       &adc_out_chan_14},
    {"AVsense4",   &adc_out_chan_15},
    {"AO5",        &adc_out_chan_16},
    {"AO11",       &adc_out_chan_17},
    {"AO17",       &adc_out_chan_18},
    {"AVsense5",   &adc_out_chan_19},
    {"AO6",        &adc_out_chan_20},
    {"AO12",       &adc_out_chan_21},
    {"AO18",       &adc_out_chan_22},
    {"AVsense6",   &adc_out_chan_23},
    {"DA11_test1", &adc_out_chan_24},
    {"DA20_test1", &adc_out_chan_25},
    {"DA33_test1", &adc_out_chan_26},
    {"DA44_test1", &adc_out_chan_27},
    {"DA11_test2", &adc_out_chan_28},
    {"DA20_test2", &adc_out_chan_29},
    {"DA33_test2", &adc_out_chan_30},
    {"DA44_test2", &adc_out_chan_31},
};

#define MAP_SIZE (sizeof(shell_map) / sizeof(shell_map[0]))

/**
 *  @brief Команда "adc <строковое_имя_канала>"
 */
static int cmd_read_chan_by_friendly_name(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) 
    {
        shell_error(sh, "Usage: adc <channel_name>");
        return -EINVAL;
    }

    const char *requested_name = argv[1];
    const adc_shell_map_t *found_entry = NULL;

    // Линейный поиск имени канала в таблице соответствия
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

    int32_t voltage = 0;
    // Безопасное чтение значения канала
    int ret = zbus_chan_read(found_entry->zbus_chan, &voltage, K_NO_WAIT);
    
    if (ret == 0) 
    {
        shell_fprintf(sh, SHELL_NORMAL, "%u\n", voltage);
    } 
    else 
    {
        shell_fprintf(sh, SHELL_NORMAL, "0.0000\n");
    }

    return 0;
}

// Регистрация команды в Shell
SHELL_CMD_REGISTER(adc, NULL, "Read ADC channel by its string name", cmd_read_chan_by_friendly_name);
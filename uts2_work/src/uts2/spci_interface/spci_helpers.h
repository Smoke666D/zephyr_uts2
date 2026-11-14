#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "output.h"
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"


scpi_result_t get_server_float_ch_to_spci(scpi_t *context,  scpi_choice_def_t * targer_choices, int target_count );
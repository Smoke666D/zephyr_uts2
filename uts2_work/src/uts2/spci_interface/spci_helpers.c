#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "spci_helpers.h"
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"

scpi_result_t get_server_float_ch_to_spci(scpi_t *context, scpi_choice_def_t * targer_choices, int target_count )
{
    int32_t channel;
    float float_val;
    int count = 0;
    
     while (SCPI_ParamChoice(context, targer_choices, &channel, FALSE)) 
     {
        SYSTEM_BUS_GET(channel, &float_val);        
        SCPI_ResultFloat(context, float_val);
        count++;
    }
    if (count == 0)
    {
        for (int ch = 0; ch < target_count; ch++) 
        {
            SYSTEM_BUS_GET(targer_choices[ch].tag, &float_val);                    
            SCPI_ResultFloat(context, float_val);
        }
    }
    return SCPI_RES_OK;
}
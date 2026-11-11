#include "system.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "scpi/ieee488.h"
#include "scpi/scpi.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"

scpi_result_t cmd_system_wait(scpi_t *context)
{
    scpi_number_t param;

    /* Передаем NULL вместо scpi_special_numbers_def */
    if (!SCPI_ParamNumber(context, NULL, &param, TRUE)) {
        return SCPI_RES_ERR;
    }

    /* libscpi автоматически переводит значение с размерностью (50ms, 1s) в секунды */
    double time_s = param.content.value;

    /* Проверка диапазона (от 1 мс до 60 секунд) */
    if (time_s < 0.001 || time_s > 60.0) {
        SCPI_ErrorPush(context, -222); /* -222 = Data out of range */
        return SCPI_RES_ERR;
    }

    uint32_t delay_ms = (uint32_t)(time_s * 1000.0);

    if (delay_ms > 0) {
        k_msleep(delay_ms);
    }

    return SCPI_RES_OK;
}




static const scpi_choice_def_t uid_targets[] = {
    {"MU", SETTING_MU_ID_1},
    {"IU", SETTING_IU_ID},
    {"CU", SETTING_CU_ID},
    SCPI_CHOICE_LIST_END
};

static const scpi_choice_def_t sn_targets[] = {
    {"MU", SETTING_MU_SN},
    {"IU", SETTING_IU_SN},
    {"CU", SETTING_CU_SN},
    SCPI_CHOICE_LIST_END
};

/* Вспомогательная функция получения UID любой из трех микросхем */
static int get_uid_string(int target, char *out_buf, size_t max_len)
{
    if (target == SETTING_MU_ID_1) 
    {
        // Чтение 96-bit UID процессора STM32H7
        uint32_t id1 = 0, id2 = 0, id3 = 0;
        if (SYSTEM_BUS_GET(SETTING_MU_ID_1, &id1) != 0 ||
            SYSTEM_BUS_GET(SETTING_MU_ID_2, &id2) != 0 ||
            SYSTEM_BUS_GET(SETTING_MU_ID_3, &id3) != 0) {
            return -EIO;
        }
        snprintf(out_buf, max_len, "0x%08X-0x%08X-0x%08X", id1, id2, id3);
        return 0;
    } else 
    {
        uint32_t val;
        SYSTEM_BUS_GET(target,&val);
        snprintf(out_buf, max_len, "0x%08X", val);
        return 0;
    }
}

scpi_result_t cmd_system_uid_q(scpi_t *context)
{
    int32_t target;
    char uid_buf[48];

    // Запрос конкретного чипа: SYST:UID? MU
    if (SCPI_ParamChoice(context, uid_targets, &target, FALSE))
    {
        if (get_uid_string(target, uid_buf, sizeof(uid_buf)) != 0) {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
        SCPI_ResultText(context, uid_buf);
    } 
    // Запрос всех трех UID разом: SYST:UID? -> "MU_UID","IU_UID","CU_UID"
    else {
        for (int i = 0; i < ARRAY_SIZE(uid_targets) -1; i++) 
        {
            if (get_uid_string(uid_targets[i].tag, uid_buf, sizeof(uid_buf)) != 0) 
            {
                SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
                return SCPI_RES_ERR;
            }
            SCPI_ResultText(context, uid_buf);
        }
    }
    return SCPI_RES_OK;
}


K_SEM_DEFINE(eeprom_done_sem, 0, 1);
static uint8_t eeprom_resp_code = 0;

scpi_result_t cmd_system_serial(scpi_t *context)
{
    int32_t _target;
    uint32_t sn;

    if (!SCPI_ParamChoice(context, sn_targets, &_target, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(context, &sn, TRUE)) return SCPI_RES_ERR;

    k_sem_reset(&eeprom_done_sem);

    SERVER_CH_SET_SYC(_target, (uint32_t)sn, &eeprom_done_sem, &eeprom_resp_code);
    
   if (false 
        || k_sem_take(&eeprom_done_sem, K_MSEC(300)) != 0
        || eeprom_resp_code != 0
       ) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

scpi_result_t cmd_system_serial_q(scpi_t *context)
{
    int32_t _target;
    uint32_t sn;

    if (SCPI_ParamChoice(context, sn_targets, &_target, FALSE))
    {
        SYSTEM_BUS_GET(_target,&sn);            
        SCPI_ResultUInt32(context, sn);        
    } 
    else 
    {
        // Отдаем оба серийника через запятую: "SN_IU,SN_CU"
        for (int i = 0; i < ARRAY_SIZE(sn_targets) -1; i++) 
        {
            SYSTEM_BUS_GET(sn_targets[i].tag,&sn);            
            SCPI_ResultUInt32(context, sn);
        }
    }
    return SCPI_RES_OK;
}
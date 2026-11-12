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
        || k_sem_take(&eeprom_done_sem, K_MSEC(500)) != 0
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


enum unit_target {
    UNIT_IU = 0,
    UNIT_CU = 1,
    UNIT_COUNT
};

static const scpi_choice_def_t unit_targets[] = {
    {"IU", UNIT_IU},
    {"CU", UNIT_CU},
    SCPI_CHOICE_LIST_END
};

/* ========================================================================= */
/* 1. ФУНКЦИИ УПАКОВКИ И РАСПАКОВКИ В ДВА 32-БИТНЫХ СЛОВА                    */
/* ========================================================================= */

// Упаковка 5 полей в два 32-битных слова
static void pack_hw_to_words(uint16_t ccc, uint8_t a, uint8_t mm, 
                             uint8_t dd, uint8_t ee, 
                             uint32_t *w1, uint32_t *w2)
{
    // Слово 1: ccc (биты 16..31) | a (биты 8..15) | mm (биты 0..7)
    *w1 = (((uint32_t)ccc & 0xFFFF) << 16) |
          (((uint32_t)a   & 0xFF)   << 8)  |
          (((uint32_t)mm  & 0xFF));

    // Слово 2: dd (биты 8..15) | ee (биты 0..7)
    *w2 = (((uint32_t)dd  & 0xFF)   << 8)  |
          (((uint32_t)ee  & 0xFF));
}

// Распаковка двух 32-битных слов обратно в 5 полей
static void unpack_words_to_hw(uint32_t w1, uint32_t w2, 
                               uint16_t *ccc, uint8_t *a, uint8_t *mm, 
                               uint8_t *dd, uint8_t *ee)
{
    *ccc = (uint16_t)((w1 >> 16) & 0xFFFF);
    *a   = (uint8_t)((w1 >> 8)   & 0xFF);
    *mm  = (uint8_t)(w1          & 0xFF);

    *dd  = (uint8_t)((w2 >> 8)   & 0xFF);
    *ee  = (uint8_t)(w2          & 0xFF);
}

/* ========================================================================= */
/* 2. СТРОГИЙ ПАРСИНГ И ВАЛИДАЦИЯ СТРОКИ: ccc.a.mm.dd.ee                     */
/* ========================================================================= */

static bool parse_hw_string(const char *str, uint16_t *ccc, uint8_t *a, 
                            uint8_t *mm, uint8_t *dd, uint8_t *ee)
{
    unsigned int v_ccc, v_a, v_mm, v_dd, v_ee;
    char trailing;

    // Считываем ровно 5 чисел через точку. %c проверяет, что нет мусора в конце
    int count = sscanf(str, "%u.%u.%u.%u.%u %c", &v_ccc, &v_a, &v_mm, &v_dd, &v_ee, &trailing);
    if (count != 5) {
        return false;
    }

    // Проверка физических диапазонов по ТЗ:
    // ccc (0-999), a (0-9), mm (0-99), dd (0-99), ee (0-99)
    if (v_ccc > 999 || v_a > 9 || v_mm > 99 || v_dd > 99 || v_ee > 99) {
        return false;
    }

    *ccc = (uint16_t)v_ccc;
    *a   = (uint8_t)v_a;
    *mm  = (uint8_t)v_mm;
    *dd  = (uint8_t)v_dd;
    *ee  = (uint8_t)v_ee;

    return true;
}

/* ========================================================================= */
/* 3. КОЛБЭК ЗАПИСИ (SET): SYSTem:HARDware <IU|CU>, <hw_string>              */
/* ========================================================================= */

scpi_result_t cmd_system_hardware(scpi_t *context)
{
    int32_t unit;
    const char *str_ptr;
    char raw_str[32];
    size_t len;
    uint16_t ccc;
    uint8_t a, mm, dd, ee;
    uint32_t word1, word2;
    

    // 1. Считываем целевой узел (IU или CU)
    if (!SCPI_ParamChoice(context, unit_targets, &unit, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    // 2. Считываем строковый параметр версии (например: "001.0.12.01.02")
    if (!SCPI_ParamCharacters(context, &str_ptr, &len, TRUE)) 
    {
        return SCPI_RES_ERR;
    }
    
     // Проверяем, чтобы строка влезла в наш временный буфер для sscanf
    if (len >= sizeof(raw_str)) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    // Копируем и закрываем строку нулем (\0) для безопасной работы sscanf
    memcpy(raw_str, str_ptr, len);
    raw_str[len] = '\0';

    // 3. Парсинг и валидация 5 сегментов строки
    if (!parse_hw_string(raw_str, &ccc, &a, &mm, &dd, &ee)) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE); // Ошибка -224
        return SCPI_RES_ERR;
    }

    // 4. Упаковываем проверенные значения в два 32-битных слова
    pack_hw_to_words(ccc, a, mm, dd, ee, &word1, &word2);

    // 5. Запись через ваш SERVER_CH_SET (синхронно или асинхронно)
    if (unit == UNIT_IU) 
    {
        SERVER_CH_SET_ASYC(SETTING_IU_HW_ID, word1);
        k_sem_reset(&eeprom_done_sem);
            
        SERVER_CH_SET_SYC(SETTING_IU_HW_VER, (uint32_t)word2,&eeprom_done_sem, &eeprom_resp_code);
        
        if (false 
            || k_sem_take(&eeprom_done_sem, K_MSEC(500)) != 0
            || eeprom_resp_code != 0
        ) 
        {
            SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
            return SCPI_RES_ERR; 
        }
    } 
    else 
    {
        SERVER_CH_SET_ASYC(SETTING_CU_HW_ID, word1);
        k_sem_reset(&eeprom_done_sem);
            
        SERVER_CH_SET_SYC(SETTING_CU_HW_VER, (uint32_t)word2,&eeprom_done_sem, &eeprom_resp_code);
        
        if (false 
            || k_sem_take(&eeprom_done_sem, K_MSEC(500)) != 0
            || eeprom_resp_code != 0
        ) 
        {
            SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
            return SCPI_RES_ERR; 
        }
    }

    return SCPI_RES_OK;
}

/* ========================================================================= */
/* 4. КОЛБЭК ЧТЕНИЯ (GET): SYSTem:HARDware? [<IU|CU>]                         */
/* ========================================================================= */

// Вспомогательное чтение двух слов по шине и сборка строки
static int read_hw_string_from_bus(int unit, char *out_str, size_t max_len)
{
    uint32_t w1 = 0, w2 = 0;    

    if (unit == UNIT_IU) 
    {
        SYSTEM_BUS_GET(SETTING_IU_HW_ID, &w1);
        SYSTEM_BUS_GET(SETTING_IU_HW_VER, &w2);
    } 
    else 
    {
        SYSTEM_BUS_GET(SETTING_CU_HW_ID, &w1);
        SYSTEM_BUS_GET(SETTING_CU_HW_VER, &w2);
    }
    

    uint16_t ccc;
    uint8_t a, mm, dd, ee;

    // Распаковываем слова в 5 чисел
    unpack_words_to_hw(w1, w2, &ccc, &a, &mm, &dd, &ee);

    // Форматируем обратно в каноническую строку вида "001.0.12.01.02"
    snprintf(out_str, max_len, "%03u.%u.%02u.%02u.%02u", ccc, a, mm, dd, ee);
    return 0;
}

scpi_result_t cmd_system_hardware_q(scpi_t *context)
{
    int32_t unit;
    char hw_str[24];

    // 1. Запрос конкретного узла: SYST:HARD? CU
    if (SCPI_ParamChoice(context, unit_targets, &unit, FALSE)) 
    {
        if (read_hw_string_from_bus(unit, hw_str, sizeof(hw_str)) != 0) 
        {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
        SCPI_ResultText(context, hw_str);
    } 
    // 2. Запрос обоих узлов разом: SYST:HARD? -> "001.0.12.01.02","002.1.05.00.01"
    else 
    {
        for (int i = 0; i < UNIT_COUNT; i++) 
        {
            if (read_hw_string_from_bus(i, hw_str, sizeof(hw_str)) != 0) 
            {
                SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
                return SCPI_RES_ERR;
            }
            SCPI_ResultText(context, hw_str);
        }
    }

    return SCPI_RES_OK;
}


static const scpi_choice_def_t user_targets[] = {
    {"IU", SETTING_IU_USER_DATA},
    {"CU", SETTING_CU_USER_DATA},
    SCPI_CHOICE_LIST_END
};

scpi_result_t cmd_system_user(scpi_t *context)
{
    int32_t _target;
    uint32_t val;

    if (!SCPI_ParamChoice(context, user_targets, &_target, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(context, &val, TRUE)) return SCPI_RES_ERR;

    k_sem_reset(&eeprom_done_sem);
            
    SERVER_CH_SET_SYC(_target, (uint32_t)val ,&eeprom_done_sem, &eeprom_resp_code);
        
    if (false 
        || k_sem_take(&eeprom_done_sem, K_MSEC(500)) != 0
        || eeprom_resp_code != 0
    ) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }    
    return SCPI_RES_OK;
}

scpi_result_t cmd_system_user_q(scpi_t *context)
{
    int32_t _target;
    uint32_t val;

    if (SCPI_ParamChoice(context, user_targets, &_target, FALSE)) 
    {
        SYSTEM_BUS_GET(_target,&val);        
        SCPI_ResultUInt32(context, val);
    } 
    else 
    {
        for (int i = 0; i < ARRAY_SIZE(user_targets) - 1 ; i++) 
        {
            SYSTEM_BUS_GET(user_targets[i].tag, &val);        
            SCPI_ResultUInt32(context, val);
        }
    }
    return SCPI_RES_OK;
}
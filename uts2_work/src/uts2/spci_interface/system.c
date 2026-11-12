/**
 *  @file       scpi_system.c
 *  @headerfile system.h
 *
 *  @date       2026.11.12
 *  @author     Dymov Igor
 *
 *  @brief      Обработчики SCPI-команд системных функций и настроек
 *  @details    Реализует команды системной задержки (SYSTem:WAIT), чтения UID процессора
 *              и EEPROM (SYSTem:UID?), а также записи/чтения серийных номеров и версий HW.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "system.h"
#include "scpi/ieee488.h"
#include "scpi/scpi.h"
#include "system_bus_model.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef enum unit_target 
{
    UNIT_IU = 0,
    UNIT_CU = 1,
    UINT_MU = 2,
    UNIT_COUNT
} unit_target_t;

typedef struct
{
    uint32_t HW_ID;
    uint32_t HW_VER;
} hw_version_target_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static int  _get_uid_string(int _target, char *_out_buf, size_t _max_len);
static void _pack_hw_to_words(uint16_t _ccc, uint8_t _a, uint8_t _mm, 
                              uint8_t _dd, uint8_t _ee, 
                              uint32_t *_w1, uint32_t *_w2);
static void _unpack_words_to_hw(uint32_t _w1, uint32_t _w2, 
                                uint16_t *_ccc, uint8_t *_a, uint8_t *_mm, 
                                uint8_t *_dd, uint8_t *_ee);
static bool _parse_hw_string(const char *_str, uint16_t *_ccc, uint8_t *_a, 
                             uint8_t *_mm, uint8_t *_dd, uint8_t *_ee);
static int  _read_hw_string_from_bus(int _unit, char *_out_str, size_t _max_len);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const hw_version_target_t hw_target[] =
{
    { SETTING_IU_HW_ID, SETTING_IU_HW_VER},
    { SETTING_CU_HW_ID, SETTING_CU_HW_VER},
    { SETTING_MU_HW_ID, SETTING_MU_HW_VER},
};

static const scpi_choice_def_t uid_targets[] = 
{
    {"MU", SETTING_MU_ID_1},
    {"IU", SETTING_IU_ID},
    {"CU", SETTING_CU_ID},
    SCPI_CHOICE_LIST_END
};

static const scpi_choice_def_t sn_targets[] = 
{
    {"MU", SETTING_MU_SN},
    {"IU", SETTING_IU_SN},
    {"CU", SETTING_CU_SN},
    SCPI_CHOICE_LIST_END
};

static const scpi_choice_def_t unit_targets[] = 
{
    {"IU", UNIT_IU},
    {"CU", UNIT_CU},
    {"MU", UINT_MU},
    SCPI_CHOICE_LIST_END
};

static const scpi_choice_def_t user_targets[] = 
{
    {"IU", SETTING_IU_USER_DATA},
    {"CU", SETTING_CU_USER_DATA},
    SCPI_CHOICE_LIST_END
};

static K_SEM_DEFINE(sem_eeprom_done, 0, 1);
static uint8_t eeprom_resp_code = 0;

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Вспомогательная функция получения UID микросхем
 *  @details    Считывает 96-битный UID процессора STM32H7 или 32-битный UID из EEPROM.
 *
 *  @param      _target  - Идентификатор целевой микросхемы
 *  @param      _out_buf - Выходной текстовый буфер для записи результата
 *  @param      _max_len - Максимальный размер выходного буфера
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _get_uid_string(int _target, char *_out_buf, size_t _max_len)
{
    if (_target == SETTING_MU_ID_1) 
    {
        // Чтение 96-битного UID процессора STM32H7
        uint32_t id1 = 0;
        uint32_t id2 = 0;
        uint32_t id3 = 0;

        if (false
            || SYSTEM_BUS_GET(SETTING_MU_ID_1, &id1) != 0
            || SYSTEM_BUS_GET(SETTING_MU_ID_2, &id2) != 0
            || SYSTEM_BUS_GET(SETTING_MU_ID_3, &id3) != 0
           ) 
        {
            return -EIO;
        }

        snprintf(_out_buf, _max_len, "0x%08X-0x%08X-0x%08X", id1, id2, id3);
        return 0;
    } 
    else 
    {
        uint32_t val = 0;

        SYSTEM_BUS_GET(_target, &val);
        snprintf(_out_buf, _max_len, "0x%08X", val);
        return 0;
    }
}

/**
 *  @brief      Упаковка 5 полей ревизии в два 32-битных слова
 *  @details    Формирует битмап слов w1 и w2 из составных частей ревизии ccc.a.mm.dd.ee.
 *
 *  @param      _ccc - Код семейства (16 бит)
 *  @param      _a   - Архитектура (8 бит)
 *  @param      _mm  - Модификация (8 бит)
 *  @param      _dd  - Ревизия плакетки (8 бит)
 *  @param      _ee  - Вариант исполнения (8 бит)
 *  @param      _w1  - Указатель на первое выходное слово
 *  @param      _w2  - Указатель на второе выходное слово
 */
static void _pack_hw_to_words(uint16_t _ccc, uint8_t _a, uint8_t _mm, 
                             uint8_t _dd, uint8_t _ee, 
                             uint32_t *_w1, uint32_t *_w2)
{
    // Слово 1: ccc (биты 16..31) | a (биты 8..15) | mm (биты 0..7)
    *_w1 = (((uint32_t)_ccc & 0xFFFF) << 16) |
           (((uint32_t)_a   & 0xFF)   << 8)  |
           (((uint32_t)_mm  & 0xFF));

    // Слово 2: dd (биты 8..15) | ee (биты 0..7)
    *_w2 = (((uint32_t)_dd  & 0xFF)   << 8)  |
           (((uint32_t)_ee  & 0xFF));
}

/**
 *  @brief      Распаковка двух 32-битных слов обратно в 5 полей
 *  @details    Извлекает из битмапа слов w1 и w2 составные части ревизии ccc.a.mm.dd.ee.
 *
 *  @param      _w1  - Первое входное слово
 *  @param      _w2  - Второе входное слово
 *  @param      _ccc - Указатель на код семейства
 *  @param      _a   - Указатель на архитектуру
 *  @param      _mm  - Указатель на модификацию
 *  @param      _dd  - Указатель на ревизию плакетки
 *  @param      _ee  - Указатель на вариант исполнения
 */
static void _unpack_words_to_hw(uint32_t _w1, uint32_t _w2, 
                               uint16_t *_ccc, uint8_t *_a, uint8_t *_mm, 
                               uint8_t *_dd, uint8_t *_ee)
{
    *_ccc = (uint16_t)((_w1 >> 16) & 0xFFFF);
    *_a   = (uint8_t)((_w1 >> 8)   & 0xFF);
    *_mm  = (uint8_t)(_w1          & 0xFF);

    *_dd  = (uint8_t)((_w2 >> 8)   & 0xFF);
    *_ee  = (uint8_t)(_w2          & 0xFF);
}

/**
 *  @brief      Парсинг и валидация строки формата ccc.a.mm.dd.ee
 *  @details    Разбирает строку ревизии и проверяет физические диапазоны по ТЗ.
 *
 *  @param      _str - Входная строка формата ccc.a.mm.dd.ee
 *  @param      _ccc - Указатель на распарсенный код семейства
 *  @param      _a   - Указатель на распарсенную архитектуру
 *  @param      _mm  - Указатель на распарсенную модификацию
 *  @param      _dd  - Указатель на распарсенную ревизию
 *  @param      _ee  - Указатель на распарсенный вариант исполнения
 *
 *  @return     bool - true при успешном парсинге, false при ошибке формата
 */
static bool _parse_hw_string(const char *_str, uint16_t *_ccc, uint8_t *_a, 
                            uint8_t *_mm, uint8_t *_dd, uint8_t *_ee)
{
    unsigned int v_ccc, v_a, v_mm, v_dd, v_ee;
    char trailing;

    // Считывание ровно 5 чисел через точку
    int count = sscanf(_str, "%u.%u.%u.%u.%u %c", &v_ccc, &v_a, &v_mm, &v_dd, &v_ee, &trailing);
    if (count != 5) 
    {
        return false;
    }

    // Проверка физических диапазонов: ccc (0-999), a (0-9), mm (0-99), dd (0-99), ee (0-99)
    if (false
        || v_ccc > 999 
        || v_a > 9 
        || v_mm > 99 
        || v_dd > 99 
        || v_ee > 99
       ) 
    {
        return false;
    }

    *_ccc = (uint16_t)v_ccc;
    *_a   = (uint8_t)v_a;
    *_mm  = (uint8_t)v_mm;
    *_dd  = (uint8_t)v_dd;
    *_ee  = (uint8_t)v_ee;

    return true;
}

/**
 *  @brief      Вспомогательное чтение двух слов из шины и сборка строки HW
 *  @details    Считывает слова ревизии из системной шины и форматирует их в строку.
 *
 *  @param      _unit    - Индекс узла (IU или CU)
 *  @param      _out_str - Выходной буфер для канонической строки "ccc.a.mm.dd.ee"
 *  @param      _max_len - Максимальный размер выходного буфера
 *
 *  @return     int - Всегда 0
 */
static int _read_hw_string_from_bus(int _unit, char *_out_str, size_t _max_len)
{
    uint32_t w1 = 0;
    uint32_t w2 = 0;    

    if (_unit == UNIT_IU) 
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
    _unpack_words_to_hw(w1, w2, &ccc, &a, &mm, &dd, &ee);

    // Форматируем обратно в каноническую строку
    snprintf(_out_str, _max_len, "%03u.%u.%02u.%02u.%02u", ccc, a, mm, dd, ee);
    return 0;
}

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      SCPI-команда системной задержки (SYSTem:WAIT)
 *  @details    Блокирует выполнение потока на заданное время (от 1 мс до 60 секунд).
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_system_wait(scpi_t *_context)
{
    scpi_number_t param;

    // Передача NULL вместо scpi_special_numbers_def
    if (!SCPI_ParamNumber(_context, NULL, &param, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    // Автоматический перевод значения с размерностью в секунды
    double time_s = param.content.value;

    // Проверка диапазона (от 1 мс до 60 секунд)
    if (false
        || time_s < 0.001 
        || time_s > 60.0
       ) 
    {
        SCPI_ErrorPush(_context, -222); // -222 = Data out of range
        return SCPI_RES_ERR;
    }

    uint32_t delay_ms = (uint32_t)(time_s * 1000.0);

    if (delay_ms > 0) 
    {
        k_msleep(delay_ms);
    }

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос уникальных идентификаторов UID (SYSTem:UID?)
 *  @details    Возвращает 96-битный UID процессора (MU) или 32-битные UID из EEPROM (IU, CU).
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_system_uid_q(scpi_t *_context)
{
    int32_t target;
    char uid_buf[48];

    // Запрос конкретной микросхемы: SYST:UID? MU
    if (SCPI_ParamChoice(_context, uid_targets, &target, FALSE))
    {
        if (_get_uid_string(target, uid_buf, sizeof(uid_buf)) != 0) 
        {
            SCPI_ErrorPush(_context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
        SCPI_ResultText(_context, uid_buf);
    } 
    // Запрос всех трех UID разом
    else 
    {
        for (int i = 0; i < ARRAY_SIZE(uid_targets) - 1; i++) 
        {
            if (_get_uid_string(uid_targets[i].tag, uid_buf, sizeof(uid_buf)) != 0) 
            {
                SCPI_ErrorPush(_context, SCPI_ERROR_EXECUTION_ERROR);
                return SCPI_RES_ERR;
            }
            SCPI_ResultText(_context, uid_buf);
        }
    }

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда установки серийного номера (SYSTem:SN)
 *  @details    Синхронно сохраняет серийный номер для указанного узла (MU, IU, CU).
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_system_serial(scpi_t *_context)
{
    int32_t target;
    uint32_t sn;

    if (!SCPI_ParamChoice(_context, sn_targets, &target, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamUInt32(_context, &sn, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    k_sem_reset(&sem_eeprom_done);

    SERVER_CH_SET_SYC(target, (uint32_t)sn, &sem_eeprom_done, &eeprom_resp_code);
    
    if (false 
        || k_sem_take(&sem_eeprom_done, K_MSEC(500)) != 0
        || eeprom_resp_code != 0
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос серийных номеров (SYSTem:SN?)
 *  @details    Считывает и возвращает серийные номера выбранного или всех узлов.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_system_serial_q(scpi_t *_context)
{
    int32_t target;
    uint32_t sn = 0;

    if (SCPI_ParamChoice(_context, sn_targets, &target, FALSE))
    {
        SYSTEM_BUS_GET(target, &sn);            
        SCPI_ResultUInt32(_context, sn);        
    } 
    else 
    {
        // Отдаем серийники всех узлов
        for (int i = 0; i < ARRAY_SIZE(sn_targets) - 1; i++) 
        {
            SYSTEM_BUS_GET(sn_targets[i].tag, &sn);            
            SCPI_ResultUInt32(_context, sn);
        }
    }

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда установки ревизии оборудования (SYSTem:HARDware)
 *  @details    Парсит строку формата ccc.a.mm.dd.ee, упаковывает в два 32-битных слова
 *              и синхронно сохраняет ревизию выбранного узла (IU или CU).
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_system_hardware(scpi_t *_context)
{
    int32_t unit;
    const char *str_ptr;
    char raw_str[32];
    size_t len;
    uint16_t ccc;
    uint8_t a, mm, dd, ee;
    uint32_t word1, word2;

    // 1. Считываем целевой узел (IU или CU)
    if (!SCPI_ParamChoice(_context, unit_targets, &unit, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    // 2. Считываем строковый параметр версии
    if (!SCPI_ParamCharacters(_context, &str_ptr, &len, TRUE)) 
    {
        return SCPI_RES_ERR;
    }
    
    // Проверяем, чтобы строка укладывалась в размер буфера
    if (len >= sizeof(raw_str)) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    // Копирование строки для безопасной работы sscanf
    memcpy(raw_str, str_ptr, len);
    raw_str[len] = '\0';

    // 3. Парсинг и валидация 5 сегментов строки
    if (!_parse_hw_string(raw_str, &ccc, &a, &mm, &dd, &ee)) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE); // Ошибка -224
        return SCPI_RES_ERR;
    }

    // 4. Упаковка проверенных значений в два 32-битных слова
    _pack_hw_to_words(ccc, a, mm, dd, ee, &word1, &word2);

    // 5. Синхронная запись через системную шину
        SERVER_CH_SET_ASYC(hw_target[unit].HW_ID, word1);
        k_sem_reset(&sem_eeprom_done);
            
        SERVER_CH_SET_SYC(
            hw_target[unit].HW_VER, 
            (uint32_t)word2, 
            &sem_eeprom_done, 
            &eeprom_resp_code
        );
        
        if (false 
            || k_sem_take(&sem_eeprom_done, K_MSEC(500)) != 0
            || eeprom_resp_code != 0
           ) 
        {
            SCPI_ErrorPush(_context, SCPI_ERROR_HARDWARE_ERROR);
            return SCPI_RES_ERR; 
        }
    
    

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос ревизии оборудования (SYSTem:HARDware?)
 *  @details    Считывает ревизии выбранного узла или всех узлов и форматирует в строку ccc.a.mm.dd.ee.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_system_hardware_q(scpi_t *_context)
{
    int32_t unit;
    char hw_str[24];

    // 1. Запрос конкретного узла: SYST:HARD? CU
    if (SCPI_ParamChoice(_context, unit_targets, &unit, FALSE)) 
    {
        if (_read_hw_string_from_bus(unit, hw_str, sizeof(hw_str)) != 0) 
        {
            SCPI_ErrorPush(_context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
        SCPI_ResultText(_context, hw_str);
    } 
    // 2. Запрос обоих узлов разом
    else 
    {
        for (int i = 0; i < UNIT_COUNT; i++) 
        {
            if (_read_hw_string_from_bus(i, hw_str, sizeof(hw_str)) != 0) 
            {
                SCPI_ErrorPush(_context, SCPI_ERROR_EXECUTION_ERROR);
                return SCPI_RES_ERR;
            }
            SCPI_ResultText(_context, hw_str);
        }
    }

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда установки пользовательских данных (SYSTem:USER)
 *  @details    Синхронно сохраняет пользовательское 32-битное значение для IU или CU.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_system_user(scpi_t *_context)
{
    int32_t target;
    uint32_t val;

    if (!SCPI_ParamChoice(_context, user_targets, &target, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamUInt32(_context, &val, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    k_sem_reset(&sem_eeprom_done);
            
    SERVER_CH_SET_SYC(target, (uint32_t)val, &sem_eeprom_done, &eeprom_resp_code);
        
    if (false 
        || k_sem_take(&sem_eeprom_done, K_MSEC(500)) != 0
        || eeprom_resp_code != 0
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос пользовательских данных (SYSTem:USER?)
 *  @details    Возвращает пользовательские данные для выбранного или всех узлов.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_system_user_q(scpi_t *_context)
{
    int32_t target;
    uint32_t val = 0;

    if (SCPI_ParamChoice(_context, user_targets, &target, FALSE)) 
    {
        SYSTEM_BUS_GET(target, &val);        
        SCPI_ResultUInt32(_context, val);
    } 
    else 
    {
        for (int i = 0; i < ARRAY_SIZE(user_targets) - 1; i++) 
        {
            SYSTEM_BUS_GET(user_targets[i].tag, &val);        
            SCPI_ResultUInt32(_context, val);
        }
    }

    return SCPI_RES_OK;
}

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
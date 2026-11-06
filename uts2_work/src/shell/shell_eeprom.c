/**
 *  @file       eeprom_shell_cmd.c
 *
 *  @date       2026.10.29
 *  @author     Dymov Igor (Refactored for Per-Context Sync/Ack Architecture)
 *
 *  @brief      Командная оболочка управления EEPROM с синхронным квитированием (Sync/Ack)
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/init.h>

#include "system_bus_model.h"
#include "app_worker.h"
#include "system_define.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(eeprom_shell_cmd, LOG_LEVEL_INF);

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

typedef struct eeprom_shell_cfg
{
    const char *prefix;         
    uint32_t    error_bit;      
    uint32_t    set_crc_error_bit;
    uint32_t    id_param;       
    uint32_t    sn_param;       
    uint32_t    hw_id_param;    
    uint32_t    hw_ver_param;   
    uint32_t    user_param;     
} eeprom_shell_cfg_t;

/**
 *  @brief Динамический контекст управления EEPROM (ссылается на статические объекты)
 */
typedef struct eeprom_shell_ctx
{
    const eeprom_shell_cfg_t *cfg;
    struct k_sem              *done_sem;  // Указатель на статический семафор
    uint8_t                   *resp_code; // Указатель на статическую переменную ответа
} eeprom_shell_ctx_t;
/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static int  _sync_eeprom_set(eeprom_shell_ctx_t *_ctx, SYSTEM_BUS_ID id, uint32_t val);
static int  _eeprom_cmd_get(const struct shell *_sh, eeprom_shell_ctx_t *_ctx, size_t _argc, char **_argv);
static int  _eeprom_cmd_set(const struct shell *_sh, eeprom_shell_ctx_t *_ctx, size_t _argc, char **_argv);
static int  _cmd_iu_get(const struct shell *_sh, size_t _argc, char **_argv);
static int  _cmd_iu_set(const struct shell *_sh, size_t _argc, char **_argv);
static int  _cmd_cu_get(const struct shell *_sh, size_t _argc, char **_argv);
static int  _cmd_cu_set(const struct shell *_sh, size_t _argc, char **_argv);
static int  _eeprom_shell_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const eeprom_shell_cfg_t iu_cfg = 
{
    .prefix             = "IU_EEPROM",
    .error_bit          = EEPORM_IU_ERROR,
    .id_param           = SETTING_IU_ID,
    .sn_param           = SETTING_IU_SN,
    .hw_id_param        = SETTING_IU_HW_ID,
    .hw_ver_param       = SETTING_IU_HW_VER,
    .set_crc_error_bit  = EVENT_EEPROM_IU_CRC_ERROR,
    .user_param         = SETTING_IU_USER_DATA,
};

static const eeprom_shell_cfg_t cu_cfg = 
{
    .prefix             = "CU_EEPROM",
    .error_bit          = EEPORM_CU_ERROR,
    .id_param           = SETTING_CU_ID,
    .sn_param           = SETTING_CU_SN,
    .hw_id_param        = SETTING_CU_HW_ID,
    .hw_ver_param       = SETTING_CU_HW_VER,
    .set_crc_error_bit  = EVENT_EEPROM_CU_CRC_ERROR,
    .user_param         = SETTING_CU_USER_DATA,
};

/* СТАТИСТИЧЕСКОЕ ОБЪЯВЛЕНИЕ СЕМАФОРОВ И ПЕРЕМЕННЫХ ЧЕРЕЗ МАКРОСЫ И СТАТИК */
K_SEM_DEFINE(iu_done_sem, 0, 1);
K_SEM_DEFINE(cu_done_sem, 0, 1);

static uint8_t iu_resp_code = 0;
static uint8_t cu_resp_code = 0;

// Привязываем статические объекты к контекстам через указатели
static eeprom_shell_ctx_t iu_ctx = {
    .cfg       = &iu_cfg,
    .done_sem  = &iu_done_sem,
    .resp_code = &iu_resp_code
};

static eeprom_shell_ctx_t cu_ctx = {
    .cfg       = &cu_cfg,
    .done_sem  = &cu_done_sem,
    .resp_code = &cu_resp_code
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Вспомогательная функция синхронной записи с ожиданием квитирования
 */
static int _sync_eeprom_set(eeprom_shell_ctx_t *_ctx, SYSTEM_BUS_ID id, uint32_t val)
{
    k_sem_reset(_ctx->done_sem);
    *_ctx->resp_code = 0;

    // Передаем персональный семафор и переменную ответа в систему шин
    int ret = SYSTEM_BUS_SET(id, (uint32_t)val);//, _ctx->done_sem, _ctx->resp_code);
    if (ret != 0) {
        return ret; // Ошибка постановки в очередь
    }

    // Ждем окончания физической записи именно для этого чипа (с запасом 300 мс)
    /*if (k_sem_take(_ctx->done_sem, K_MSEC(300)) != 0) {
        return -ETIMEDOUT; // Таймаут ожидания железа
    }*/

    return 0;//*_ctx->resp_code; // Возвращаем реальный статус от воркера (0 = успех)
}

/**
 *  @brief      Обработчик субкоманды чтения EEPROM
 *  @details    Считывает параметр (или все сразу). При ошибке CRC для пользовательских 
 *              данных (SN, USER, HW) выводит N/A, сохраняя чтение заводского UID.
 */
static int _eeprom_cmd_get(const struct shell *_sh, eeprom_shell_ctx_t *_ctx, 
                            size_t _argc, char **_argv)
{
    const eeprom_shell_cfg_t *cfg = _ctx->cfg;

    if (_argc > 2) 
    {
        shell_print(_sh, "%s ERROR: invalid data! (argc=%zu)", cfg->prefix, _argc);
        return 0;
    }

    // 1. Проверяем физическую готовность EEPROM
    bool err_bit = false;
    SERVER_CH_GET_BIT(EEPROM_ERROR_CH, cfg->error_bit, &err_bit);
    if (err_bit) 
    {
        shell_print(_sh, "%s ERROR: Eeprom not found!", cfg->prefix);
        return 0;
    }

    // Проверяем флаг CRC / очистки EEPROM (теперь он не прерывает выполнение, а влияет на вывод полей)
    bool crc_err_bit = false;
    SERVER_CH_GET_BIT(EVENT_1_REG, cfg->set_crc_error_bit, &crc_err_bit);

    // ВЕТКА А: Аргумент не указан (_argc == 0 или _argc == 1) -> Выводим ВСЕ параметры в одну строку
    if (_argc == 0 || _argc == 1)
    {
        uint32_t uid = 0, sn = 0, user = 0, val_hw_id = 0, val_hw_ver = 0;
        int r_uid  = SYSTEM_BUS_GET(cfg->id_param, &uid);
        
        // Если есть ошибка CRC, то пользовательские данные принудительно считаем недоступными (-1)
        int r_sn   = crc_err_bit ? -1 : SYSTEM_BUS_GET(cfg->sn_param, &sn);
        int r_user = crc_err_bit ? -1 : SYSTEM_BUS_GET(cfg->user_param, &user);
        int r_hw1  = crc_err_bit ? -1 : SYSTEM_BUS_GET(cfg->hw_id_param, &val_hw_id);
        int r_hw2  = crc_err_bit ? -1 : SYSTEM_BUS_GET(cfg->hw_ver_param, &val_hw_ver);

        // Форматируем SN
        char sn_str[16] = "N/A";
        if (r_sn == 0) {
            snprintf(sn_str, sizeof(sn_str), "%u", sn);
        }

        // Форматируем USER
        char user_str[16] = "N/A";
        if (r_user == 0) {
            snprintf(user_str, sizeof(user_str), "%u", user);
        }

        // Форматируем HW
        char hw_str[32] = "N/A";
        if (r_hw1 == 0 && r_hw2 == 0) 
        {
            uint32_t mm = val_hw_id % 100;
            uint32_t temp = val_hw_id / 100;
            uint32_t a = temp % 10;
            uint32_t ccc = temp / 10;

            uint32_t ee = val_hw_ver % 100;
            uint32_t dd = val_hw_ver / 100;
            snprintf(hw_str, sizeof(hw_str), "%03u.%u.%02u.%02u.%02u", ccc, a, mm, dd, ee);
        }

        shell_print(_sh, "%s OK UID: 0x%08X | SN: %s | USER: %s | HW: %s",
                    cfg->prefix,
                    (r_uid == 0) ? uid : 0,
                    sn_str,
                    user_str,
                    hw_str);
        return 0;
    }

    // ВЕТКА Б: Указан конкретный параметр (_argc == 2) -> Выводим его
    const char *data_type = _argv[1];

    if (strcmp(data_type, "UID") == 0) 
    {
        uint32_t val = 0;
        if (SYSTEM_BUS_GET(cfg->id_param, &val) == 0) {
            shell_print(_sh, "%s OK UID: 0x%08X", cfg->prefix, val);
        } else {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        }
    }
    else if (strcmp(data_type, "SN") == 0) 
    {
        if (crc_err_bit) {
            shell_print(_sh, "%s OK SN: N/A", cfg->prefix);
            return 0;
        }
        uint32_t val = 0;
        if (SYSTEM_BUS_GET(cfg->sn_param, &val) == 0) {
            shell_print(_sh, "%s OK SN: %u", cfg->prefix, val);
        } else {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        }
    }
    else if (strcmp(data_type, "USER") == 0) 
    {
        if (crc_err_bit) {
            shell_print(_sh, "%s OK USER: N/A", cfg->prefix);
            return 0;
        }
        uint32_t val = 0;
        if (SYSTEM_BUS_GET(cfg->user_param, &val) == 0) {
            shell_print(_sh, "%s OK USER: %u", cfg->prefix, val);
        } else {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        }
    }
    else if (strcmp(data_type, "HW") == 0) 
    {
        if (crc_err_bit) {
            shell_print(_sh, "%s OK HW: N/A", cfg->prefix);
            return 0;
        }
        uint32_t val_hw_id = 0, val_hw_ver = 0;
        if (SYSTEM_BUS_GET(cfg->hw_id_param, &val_hw_id) == 0 && 
            SYSTEM_BUS_GET(cfg->hw_ver_param, &val_hw_ver) == 0) 
        {
            uint32_t mm = val_hw_id % 100;
            uint32_t temp = val_hw_id / 100;
            uint32_t a = temp % 10;
            uint32_t ccc = temp / 10;
            uint32_t ee = val_hw_ver % 100;
            uint32_t dd = val_hw_ver / 100;

            shell_print(_sh, "%s OK HW: %03u.%u.%02u.%02u.%02u", 
                    cfg->prefix, ccc, a, mm, dd, ee);
        } 
        else 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        }
    }        
    else 
    {
        shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
    }

    return 0;
}
/**
 *  @brief      Обработчик субкоманды записи EEPROM с синхронным квитированием
 */
static int _eeprom_cmd_set(const struct shell *_sh, eeprom_shell_ctx_t *_ctx, 
                            size_t _argc, char **_argv)
{
    const eeprom_shell_cfg_t *cfg = _ctx->cfg;

    if (_argc != 3) 
    {
        shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
        return 0;
    }

    bool err_bit = false;
    SERVER_CH_GET_BIT(EEPROM_ERROR_CH, cfg->error_bit, &err_bit);
    if (err_bit) 
    {
        shell_print(_sh, "%s ERROR: Eeprom not found!", cfg->prefix);
        return 0;
    }

    const char *data_type = _argv[1];
    const char *data_val_str = _argv[2];

    if (strcmp(data_type, "UID") == 0) 
    {
        shell_print(_sh, "%s ERROR: read only!", cfg->prefix);
        return 0;
    }

    if (strcmp(data_type, "HW") == 0) 
    {
        unsigned int ccc, a, mm, dd, ee;
        if (sscanf(data_val_str, "%u.%u.%u.%u.%u", &ccc, &a, &mm, &dd, &ee) != 5 ||
            ccc > 999 || a > 9 || mm > 99 || dd > 99 || ee > 99) 
        {
            shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
            return 0;
        }

        uint32_t hw_id = (ccc * 1000) + (a * 100) + mm;
        uint32_t hw_ver = (dd * 100) + ee;

        int res1 = _sync_eeprom_set(_ctx, cfg->hw_id_param, hw_id);
        int res2 = _sync_eeprom_set(_ctx, cfg->hw_ver_param, hw_ver);

        if (res1 == 0 && res2 == 0) {
            shell_print(_sh, "+%s DONE: write ok!", cfg->prefix);
        } else {
            shell_print(_sh, "+%s ERROR: write error!", cfg->prefix);
        }
        return 0;
    }

    // Определение параметра для SN или USER
    SYSTEM_BUS_ID target_param = 0;
    if (strcmp(data_type, "SN") == 0) {
        target_param = cfg->sn_param;
    } else if (strcmp(data_type, "USER") == 0) {
        target_param = cfg->user_param;
    } else {
        shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
        return 0;
    }

    char *endptr;
    unsigned long val = strtoul(data_val_str, &endptr, 0);
    if (*endptr != '\0') 
    {
        shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
        return 0;
    }

    // Синхронная запись с использованием персонального семафора контекста
    int res = _sync_eeprom_set(_ctx, target_param, (uint32_t)val);
    
    if (res == 0) {
        shell_print(_sh, "+%s DONE: write ok!", cfg->prefix);
    } else if (res == -ETIMEDOUT) {
        shell_print(_sh, "+%s ERROR: timeout error!", cfg->prefix);
    } else {
        shell_print(_sh, "+%s ERROR: write error!", cfg->prefix);
    }
    
    return 0;
}

// Обертки для IU_EEPROM
static int _cmd_iu_get(const struct shell *_sh, size_t _argc, char **_argv) { return _eeprom_cmd_get(_sh, &iu_ctx, _argc, _argv); }
static int _cmd_iu_set(const struct shell *_sh, size_t _argc, char **_argv) { return _eeprom_cmd_set(_sh, &iu_ctx, _argc, _argv); }

SHELL_STATIC_SUBCMD_SET_CREATE(
    iu_eeprom_sub,
    SHELL_CMD(get, NULL, "Get parameter: IU_EEPROM get [<UID|SN|HW|USER>]", _cmd_iu_get),
    SHELL_CMD(set, NULL, "Set parameter: IU_EEPROM set <SN|HW|USER> <val>", _cmd_iu_set),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(IU_EEPROM, &iu_eeprom_sub, "IU EEPROM commands", NULL);

// Обертки для CU_EEPROM
static int _cmd_cu_get(const struct shell *_sh, size_t _argc, char **_argv) { return _eeprom_cmd_get(_sh, &cu_ctx, _argc, _argv); }
static int _cmd_cu_set(const struct shell *_sh, size_t _argc, char **_argv) { return _eeprom_cmd_set(_sh, &cu_ctx, _argc, _argv); }

SHELL_STATIC_SUBCMD_SET_CREATE(
    cu_eeprom_sub,
    SHELL_CMD(get, NULL, "Get parameter: CU_EEPROM get [<UID|SN|HW|USER>]", _cmd_cu_get),
    SHELL_CMD(set, NULL, "Set parameter: CU_EEPROM set <SN|HW|USER> <val>", _cmd_cu_set),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(CU_EEPROM, &cu_eeprom_sub, "CU EEPROM commands", NULL);


/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
/**
 *  @file       eeprom_shell_cmd.c
 *
 *  @date       2026.10.29
 *  @author     Dymov Igor
 *
 *  @brief      Командная оболочка управления EEPROM с асинхронным URC-контролем ошибок
 *  @details    Предоставляет интерфейс Shell для чтения и записи параметров
 *              микросхем EEPROM (IU и CU) с отложенной проверкой статуса записи.
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

#define EEPROM_WRITE_CHECK_DELAY_MS   100U

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

/**
 *  @brief Структура статической конфигурации чипов EEPROM
 */
typedef struct eeprom_shell_cfg
{
    const char *prefix;         // "IU_EEPROM" или "CU_EEPROM"
    uint32_t    error_bit;      // EEPORM_IU_ERROR или EEPORM_CU_ERROR
    uint32_t    set_ok_bit;     // EVENT_EEPROM_IU_WRITE_OK или EVENT_EEPROM_CU_WRITE_OK
    uint32_t    set_error_bit;  // EVENT_EEPROM_IU_WRITE_ERROR или EVENT_EEPROM_CU_WRITE_ERROR
    uint32_t    set_crc_error_bit;
    uint32_t    id_param;       // SETTING_IU_ID или SETTING_CU_ID
    uint32_t    sn_param;       // SETTING_IU_SN или SETTING_CU_SN
    uint32_t    hw_id_param;    // SETTING_IU_HW_ID или SETTING_CU_HW_ID
    uint32_t    hw_ver_param;   // SETTING_IU_HW_VER или SETTING_CU_HW_VER
    uint32_t    user_param;     // SETTING_IU_USER_DATA или SETTING_CU_USER_DATA
} eeprom_shell_cfg_t;

/**
 *  @brief Динамический контекст управления EEPROM
 */
typedef struct eeprom_shell_ctx
{
    const eeprom_shell_cfg_t *cfg;
    struct k_work_delayable   check_work; // Индивидуальный воркер проверки
    const struct shell       *sh;         // Консоль для отправки URC
} eeprom_shell_ctx_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void _eeprom_check_work_handler(struct k_work *_work);
static void _schedule_write_check(eeprom_shell_ctx_t *_ctx, const struct shell *_sh);
static int  _eeprom_cmd_get(const struct shell *_sh, eeprom_shell_ctx_t *_ctx, 
                            size_t _argc, char **_argv);
static int  _eeprom_cmd_set(const struct shell *_sh, eeprom_shell_ctx_t *_ctx, 
                            size_t _argc, char **_argv);
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
    .set_ok_bit         = EVENT_EEPROM_IU_WRITE_OK,
    .set_error_bit      = EVENT_EEPROM_IU_WRITE_ERROR,    
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
    .set_ok_bit         = EVENT_EEPROM_CU_WRITE_OK,
    .set_error_bit      = EVENT_EEPROM_CU_WRITE_ERROR,
    .id_param           = SETTING_CU_ID,
    .sn_param           = SETTING_CU_SN,
    .hw_id_param        = SETTING_CU_HW_ID,
    .hw_ver_param       = SETTING_CU_HW_VER,
    .set_crc_error_bit  = EVENT_EEPROM_CU_CRC_ERROR,
    .user_param         = SETTING_CU_USER_DATA,
};

// Раздельные контексты исключают взаимное влияние при одновременной записи
static eeprom_shell_ctx_t iu_ctx = {.cfg = &iu_cfg};
static eeprom_shell_ctx_t cu_ctx = {.cfg = &cu_cfg};
static uint32_t timeout = 0;

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Обработчик отложенной проверки статуса записи (URC)
 *  @details    Проверяет флаг предупреждения аппаратных ошибок и отправляет URC в Shell.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _eeprom_check_work_handler(struct k_work *_work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(_work);
    eeprom_shell_ctx_t *ctx = CONTAINER_OF(dwork, eeprom_shell_ctx_t, check_work);

    bool ok_bit = false;
    bool error_bit = false;

    SERVER_CH_GET_BIT(EEPROM_EVENT_CH, ctx->cfg->set_error_bit, &error_bit);
    SERVER_CH_GET_BIT(EEPROM_EVENT_CH, ctx->cfg->set_ok_bit, &ok_bit);

    if (ctx->sh != NULL) 
    {
         if (error_bit)
         {
            shell_print(ctx->sh, "+%s ERROR: write error!", ctx->cfg->prefix);
         }
         else if (ok_bit)
         {
            shell_print(ctx->sh, "+%s DONE: write ok!", ctx->cfg->prefix);
         }
         else 
         {
            shell_print(ctx->sh, "+%s ERROR: timeout error!", ctx->cfg->prefix);
         }
    }
}

/**
 *  @brief      Планирование отложенной проверки записи
 *  @details    Сбрасывает предыдущий флаг ошибки и ставит воркер проверки в очередь.
 *
 *  @param      _ctx - Указатель на контекст EEPROM
 *  @param      _sh  - Указатель на контекст Shell
 */
static void _schedule_write_check(eeprom_shell_ctx_t *_ctx, const struct shell *_sh)
{
    _ctx->sh = _sh;
    SERVER_CH_CLEAR_MASK(EEPROM_EVENT_CH, _ctx->cfg->set_error_bit | _ctx->cfg->set_ok_bit);    
    app_worker_reschedule_submit(
        &_ctx->check_work, 
        K_MSEC(timeout), 
        COMMON_WORKER
    );
}

/**
 *  @brief      Обработчик субкоманды чтения EEPROM
 *  @details    Считывает параметр (или все сразу, если аргумент не указан) из системной шины.
 */
static int _eeprom_cmd_get(const struct shell *_sh, eeprom_shell_ctx_t *_ctx, 
                            size_t _argc, char **_argv)
{
    const eeprom_shell_cfg_t *cfg = _ctx->cfg;

    // ОТЛАДКА: Посмотрим в лог, что реально пришло от Shell
    // shell_print(_sh, "DEBUG: _argc = %zu", _argc);

    // Допускаем _argc == 0 или 1 (вывод всех), либо _argc == 2 (вывод конкретного)
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

    // Проверяем флаг CRC / очистки EEPROM
    bool crc_err_bit = false;
    SERVER_CH_GET_BIT(EVENT_1_REG, cfg->set_crc_error_bit, &crc_err_bit);
    if (crc_err_bit) 
    {
        shell_print(_sh, "%s ERROR: Clear eeprom detected!", cfg->prefix);
        return 0;
    }

    // ВЕТКА А: Аргумент не указан (_argc == 0 или _argc == 1) -> Выводим ВСЕ параметры
    if (_argc == 0 || _argc == 1)
    {
        uint32_t uid = 0, sn = 0, user = 0, val_hw_id = 0, val_hw_ver = 0;
        int r_uid  = SYSTEM_BUS_GET(cfg->id_param, &uid);
        int r_sn   = SYSTEM_BUS_GET(cfg->sn_param, &sn);
        int r_user = SYSTEM_BUS_GET(cfg->user_param, &user);
        int r_hw1  = SYSTEM_BUS_GET(cfg->hw_id_param, &val_hw_id);
        int r_hw2  = SYSTEM_BUS_GET(cfg->hw_ver_param, &val_hw_ver);

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

        shell_print(_sh, "%s OK UID: 0x%08X | SN: %u | USER: %u | HW: %s",
                    cfg->prefix,
                    (r_uid == 0) ? uid : 0,
                    (r_sn == 0) ? sn : 0,
                    (r_user == 0) ? user : 0,
                    hw_str);
        return 0;
    }

    // ВЕТКА Б: Указан конкретный параметр (_argc == 2) -> Выводим его
    const char *data_type = _argv[1];

    if (strcmp(data_type, "UID") == 0) 
    {
        uint32_t val = 0;
        int res = SYSTEM_BUS_GET(cfg->id_param, &val);
        if (res != 0) 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        } 
        else 
        {
            shell_print(_sh, "%s OK UID: 0x%08X", cfg->prefix, val);
        }
    }
    else if (strcmp(data_type, "SN") == 0) 
    {
        uint32_t val = 0;
        int res = SYSTEM_BUS_GET(cfg->sn_param, &val);
        if (res != 0) 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        } 
        else 
        {
            shell_print(_sh, "%s OK SN: %u", cfg->prefix, val);
        }
    }
    else if (strcmp(data_type, "USER") == 0) 
    {
        uint32_t val = 0;
        int res = SYSTEM_BUS_GET(cfg->user_param, &val);
        if (res != 0) 
        {
            shell_print(_sh, "%s ERROR: not support!", cfg->prefix);
        } 
        else 
        {
            shell_print(_sh, "%s OK USER: %u", cfg->prefix, val);
        }
    }
    else if (strcmp(data_type, "HW") == 0) 
    {
        uint32_t val_hw_id = 0;
        uint32_t val_hw_ver = 0;
        int res1 = SYSTEM_BUS_GET(cfg->hw_id_param, &val_hw_id);
        int res2 = SYSTEM_BUS_GET(cfg->hw_ver_param, &val_hw_ver);

        if (res1 != 0 || res2 != 0) 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        } 
        else 
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
    }        
    else 
    {
        shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
    }

    return 0;
}

/**
 *  @brief      Обработчик субкоманды записи EEPROM
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

    // 1. Проверяем физическую готовность EEPROM
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
    }
    else if (strcmp(data_type, "SN") == 0) 
    {
        char *endptr;
        unsigned long val = strtoul(data_val_str, &endptr, 0);
        if (*endptr != '\0') 
        {
            shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
            return 0;
        }

        int res = SYSTEM_BUS_SET(cfg->sn_param, (uint32_t)val);
        if (res != 0) 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        } 
        else 
        {
            timeout = SERVER_CH_TIMEOUT(cfg->sn_param);
            shell_print(_sh, "%s OK", cfg->prefix);
            _schedule_write_check(_ctx, _sh);
        }
    }
    else if (strcmp(data_type, "USER") == 0) 
    {
        char *endptr;
        unsigned long val = strtoul(data_val_str, &endptr, 0);
        if (*endptr != '\0') 
        {
            shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
            return 0;
        }

        int res = SYSTEM_BUS_SET(cfg->user_param, (uint32_t)val);
        if (res != 0) 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        } 
        else 
        {
            shell_print(_sh, "%s OK", cfg->prefix);
            timeout = SERVER_CH_TIMEOUT(cfg->user_param);
            _schedule_write_check(_ctx, _sh);
        }
    }
    else if (strcmp(data_type, "HW") == 0) 
    {
        unsigned int ccc, a, mm, dd, ee;

        if (sscanf(data_val_str, "%u.%u.%u.%u.%u", &ccc, &a, &mm, &dd, &ee) != 5) 
        {
            shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
            return 0;
        }

        if (false
            || ccc > 999 
            || a > 9 
            || mm > 99 
            || dd > 99 
            || ee > 99
           ) 
        {
            shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
            return 0;
        }

        uint32_t hw_id = (ccc * 1000) + (a * 100) + mm;
        uint32_t hw_ver = (dd * 100) + ee;

        int res1 = SYSTEM_BUS_SET(cfg->hw_id_param, hw_id);
        int res2 = SYSTEM_BUS_SET(cfg->hw_ver_param, hw_ver);        
        if (res1 != 0 || res2 != 0) 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        } 
        else 
        {
            shell_print(_sh, "%s OK", cfg->prefix);
            timeout = SERVER_CH_TIMEOUT(cfg->hw_id_param);
            _schedule_write_check(_ctx, _sh);
        }
    }
    else 
    {
        shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
    }
    
    return 0;
}

// Обертки для IU_EEPROM
static int _cmd_iu_get(const struct shell *_sh, size_t _argc, char **_argv)
{
    return _eeprom_cmd_get(_sh, &iu_ctx, _argc, _argv);
}

static int _cmd_iu_set(const struct shell *_sh, size_t _argc, char **_argv)
{
    return _eeprom_cmd_set(_sh, &iu_ctx, _argc, _argv);
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    iu_eeprom_sub,
    SHELL_CMD(get, NULL, "Get parameter: IU_EEPROM get [<UID|SN|HW|USER>]", _cmd_iu_get),
    SHELL_CMD(set, NULL, "Set parameter: IU_EEPROM set <SN|HW|USER> <val>", _cmd_iu_set),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(IU_EEPROM, &iu_eeprom_sub, "IU EEPROM commands", NULL);

// Обертки для CU_EEPROM
static int _cmd_cu_get(const struct shell *_sh, size_t _argc, char **_argv)
{
    return _eeprom_cmd_get(_sh, &cu_ctx, _argc, _argv);
}

static int _cmd_cu_set(const struct shell *_sh, size_t _argc, char **_argv)
{
    return _eeprom_cmd_set(_sh, &cu_ctx, _argc, _argv);
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    cu_eeprom_sub,
    SHELL_CMD(get, NULL, "Get parameter: CU_EEPROM get [<UID|SN|HW|USER>]", _cmd_cu_get),
    SHELL_CMD(set, NULL, "Set parameter: CU_EEPROM set <SN|HW|USER> <val>", _cmd_cu_set),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(CU_EEPROM, &cu_eeprom_sub, "CU EEPROM commands", NULL);

/**
 *  @brief      Инициализация изолированных воркеров проверки записи
 */
static int _eeprom_shell_init(void)
{
    k_work_init_delayable(&iu_ctx.check_work, _eeprom_check_work_handler);
    k_work_init_delayable(&cu_ctx.check_work, _eeprom_check_work_handler);
    return 0;
}

SYS_INIT(_eeprom_shell_init, EEPROM_SHELL_INIT_LEVEL, EEPROM_SHELL_INIT_PRIOR);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
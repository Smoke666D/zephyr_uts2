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
    uint32_t    warn_bit;       // EEPORM_IU_WARNING или EEPORM_CU_WARNING
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
    .prefix       = "IU_EEPROM",
    .error_bit    = EEPORM_IU_ERROR,
    .warn_bit     = EEPORM_IU_WARNING,
    .id_param     = SETTING_IU_ID,
    .sn_param     = SETTING_IU_SN,
    .hw_id_param  = SETTING_IU_HW_ID,
    .hw_ver_param = SETTING_IU_HW_VER,
    .user_param   = SETTING_IU_USER_DATA,
};

static const eeprom_shell_cfg_t cu_cfg = 
{
    .prefix       = "CU_EEPROM",
    .error_bit    = EEPORM_CU_ERROR,
    .warn_bit     = EEPORM_CU_WARNING,
    .id_param     = SETTING_CU_ID,
    .sn_param     = SETTING_CU_SN,
    .hw_id_param  = SETTING_CU_HW_ID,
    .hw_ver_param = SETTING_CU_HW_VER,
    .user_param   = SETTING_CU_USER_DATA,
};

// Раздельные контексты исключают взаимное влияние при одновременной записи
static eeprom_shell_ctx_t iu_ctx = {.cfg = &iu_cfg};
static eeprom_shell_ctx_t cu_ctx = {.cfg = &cu_cfg};

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

    bool warn_bit = false;
    SYSTEM_BUS_GET_BIT(HARDWARE_WARNING_REG1, ctx->cfg->warn_bit, &warn_bit);

    if (warn_bit) 
    {
        if (ctx->sh != NULL) 
        {
            // Формат URC: +IU_EEPROM ERROR: write error!
            shell_print(ctx->sh, "+%s ERROR: write error!", ctx->cfg->prefix);
        }
    }
}

/**
 *  @brief      Планирование отложенной проверки записи
 *  @details    Сбрасывает предыдущий флаг ошибки и ставит воркер проверки в очередь на 100 мс.
 *
 *  @param      _ctx - Указатель на контекст EEPROM
 *  @param      _sh  - Указатель на контекст Shell
 */
static void _schedule_write_check(eeprom_shell_ctx_t *_ctx, const struct shell *_sh)
{
    _ctx->sh = _sh;
    // Сбрасываем флаг перед проверкой, чтобы не читать устаревшую ошибку
    SYSTEM_BUS_CLEAR_BIT_IF_NEEDED(HARDWARE_WARNING_REG1, _ctx->cfg->warn_bit);

    app_worker_reschedule_submit(
        &_ctx->check_work, 
        K_MSEC(EEPROM_WRITE_CHECK_DELAY_MS), 
        COMMON_WORKER
    );
}

/**
 *  @brief      Обработчик субкоманды чтения EEPROM
 *  @details    Считывает запрашиваемый параметр (UID, SN, HW, USER) из системной шины
 *              и выводит результат в консоль Shell.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _ctx  - Указатель на контекст EEPROM
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *
 *  @return     int - Всегда 0
 */
static int _eeprom_cmd_get(const struct shell *_sh, eeprom_shell_ctx_t *_ctx, 
                            size_t _argc, char **_argv)
{
    const eeprom_shell_cfg_t *cfg = _ctx->cfg;

    if (_argc != 2) 
    {
        shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
        return 0;
    }

    // 1. Проверяем физическую готовность EEPROM
    bool err_bit = false;
    SYSTEM_BUS_GET_BIT(HARDWARE_ERROR_REGISTER1, cfg->error_bit, &err_bit);
    if (err_bit) 
    {
        shell_print(_sh, "%s ERROR: Eeprom not found!", cfg->prefix);
        return 0;
    }

    const char *data_type = _argv[1];

    // 2. UID
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
    // 3. SN
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
    // 4. USER
    else if (strcmp(data_type, "USER") == 0) 
    {
        uint32_t val = 0;
        int res = SYSTEM_BUS_GET(cfg->user_param, &val);
        if (res != 0) 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        } 
        else 
        {
            shell_print(_sh, "%s OK USER: %u", cfg->prefix, val);
        }
    }
    // 5. HW
    else if (strcmp(data_type, "HW") == 0) 
    {
        uint32_t val_hw_id = 0;
        uint32_t val_hw_ver = 0;
        int res1 = SYSTEM_BUS_GET(cfg->hw_id_param, &val_hw_id);
        int res2 = SYSTEM_BUS_GET(cfg->hw_ver_param, &val_hw_ver);

        if (false
            || res1 != 0 
            || res2 != 0
           ) 
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
    // 6. Неизвестный параметр
    else 
    {
        shell_print(_sh, "%s ERROR: invalid data!", cfg->prefix);
    }

    return 0;
}

/**
 *  @brief      Обработчик субкоманды записи EEPROM
 *  @details    Записывает передаваемое значение (SN, HW, USER) в системную шину
 *              и планирует проверку отложенной записи URC.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _ctx  - Указатель на контекст EEPROM
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *
 *  @return     int - Всегда 0
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
    SYSTEM_BUS_GET_BIT(HARDWARE_ERROR_REGISTER1, cfg->error_bit, &err_bit);
    if (err_bit) 
    {
        shell_print(_sh, "%s ERROR: Eeprom not found!", cfg->prefix);
        return 0;
    }

    const char *data_type = _argv[1];
    const char *data_val_str = _argv[2];

    // 2. UID — только чтение
    if (strcmp(data_type, "UID") == 0) 
    {
        shell_print(_sh, "%s ERROR: read only!", cfg->prefix);
    }
    // 3. SN (uint32_t)
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
            shell_print(_sh, "%s OK", cfg->prefix);
            _schedule_write_check(_ctx, _sh);
        }
    }
    // 4. USER (uint32_t)
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
            _schedule_write_check(_ctx, _sh);
        }
    }
    // 5. HW (Формат: ccc.a.mm.dd.ee)
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

        if (false
            || res1 != 0 
            || res2 != 0
           ) 
        {
            shell_print(_sh, "%s ERROR: not supported!", cfg->prefix);
        } 
        else 
        {
            shell_print(_sh, "%s OK", cfg->prefix);
            _schedule_write_check(_ctx, _sh);
        }
    }
    // 6. Неизвестный параметр
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
    SHELL_CMD(get, NULL, "Get parameter: IU_EEPROM get <UID|SN|HW|USER>", _cmd_iu_get),
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
    SHELL_CMD(get, NULL, "Get parameter: CU_EEPROM get <UID|SN|HW|USER>", _cmd_cu_get),
    SHELL_CMD(set, NULL, "Set parameter: CU_EEPROM set <SN|HW|USER> <val>", _cmd_cu_set),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(CU_EEPROM, &cu_eeprom_sub, "CU EEPROM commands", NULL);

/**
 *  @brief      Инициализация изолированных воркеров проверки записи
 *  @details    Инициализирует отложенные воркеры k_work_delayable для IU и CU.
 *
 *  @return     int - Ноль при успехе
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
#include <stdint.h>
#include <stdlib.h>  /* Для strtoul */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "system_bus_model.h"

LOG_MODULE_REGISTER(settings_shell, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* 1. КОМАНДА: uid_get (Чтение 96-битного уникального ID процессора) */
/* ------------------------------------------------------------------ */
static int cmd_uid_get(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint32_t id1, id2, id3;

    /* Запрашиваем 3 слова UID у Сервера Параметров */
    int ret1 = SYSTEM_BUS_GET(SETTING_MU_ID_1, &id1);
    int ret2 = SYSTEM_BUS_GET(SETTING_MU_ID_2, &id2);
    int ret3 = SYSTEM_BUS_GET(SETTING_MU_ID_3, &id3);

    if (ret1 == 0 && ret2 == 0 && ret3 == 0) {
        shell_print(sh, "STM32H7 96-bit UID: 0x%08X-0x%08X-0x%08X",id1,id2,id3);
    } else 
    {
        shell_error(sh, "Failed to read UID from Parameter Server!");
        return -ENODEV;
    }
    return 0;
}

SHELL_CMD_REGISTER(MU_UID_GET, NULL, 
                   "Get STM32H7 96-bit Unique ID (UID)", 
                   cmd_uid_get);

/* ------------------------------------------------------------------ */
/* КОМАНДА: MU_SN (Чтение или запись серийного номера устройства)     */
/* ------------------------------------------------------------------ */
static int cmd_mu_sn(const struct shell *sh, size_t argc, char **argv)
{
    // ВЕТКА 1: Чтение серийного номера (вызвано как "MU_SN" или "MU_SN ?")
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "?") == 0)) 
    {
        uint32_t val;
        int ret = SYSTEM_BUS_GET(SETTING_MU_SN, &val);

        if (ret == 0) {
            shell_print(sh, "Device Serial Number (SN): %u (0x%08X)", val, val);
        } else {
            shell_error(sh, "Failed to read SN from Parameter Server!");
            return ret;
        }
        return 0;
    }

    // ВЕТКА 2: Запись нового серийного номера (вызвано как "MU_SN <val>")
    if (argc == 2) 
    {
        /* Парсим серийный номер (поддерживает и десятичный, и 0xHEX формат) */
        char *endptr;
        unsigned long sn_val = strtoul(argv[1], &endptr, 0);
        if (*endptr != '\0') {
            shell_error(sh, "Invalid serial number format: %s", argv[1]);
            return -EINVAL;
        }

        uint32_t val = (uint32_t)sn_val;
        
        /* 
         * Публикуем новое значение через Сервер Параметров.
         * Это обновит Zbus-канал и автоматически вызовет сохранение во FRAM с пересчетом CRC16.
         */
        int ret = SYSTEM_BUS_SET(SETTING_MU_SN, val);

        if (ret == 0) {
            shell_print(sh, "SN successfully set to %lu (0x%08X) and persisted to FRAM.",
                        sn_val, (uint32_t)sn_val);
        } else {
            shell_error(sh, "Failed to set Serial Number: %d", ret);
        }

        return ret;
    }

    // Если аргументов больше, чем нужно
    shell_error(sh, "Invalid arguments. Usage: MU_SN [val] or MU_SN ?");
    return -EINVAL;
}

/* Регистрация объединенной команды в системе Shell */
SHELL_CMD_ARG_REGISTER(
    MU_SN, 
    NULL, 
    "Get or set Device Serial Number (saved to FRAM).\n"
    "Usage:\n"
    "  MU_SN          - Read serial number\n"
    "  MU_SN ?        - Read serial number\n"
    "  MU_SN <val>    - Set serial number (dec or 0xHEX)", 
    cmd_mu_sn, 
    1, 
    1
);
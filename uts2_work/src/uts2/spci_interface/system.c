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

scpi_result_t cmd_system_uid_q(scpi_t *context)
{
    uint32_t id1 = 0, id2 = 0, id3 = 0;
    char uid_str[36]; // Буфер под "0xXXXXXXXX-0xXXXXXXXX-0xXXXXXXXX"

    int ret1 = SYSTEM_BUS_GET(SETTING_MU_ID_1, &id1);
    int ret2 = SYSTEM_BUS_GET(SETTING_MU_ID_2, &id2);
    int ret3 = SYSTEM_BUS_GET(SETTING_MU_ID_3, &id3);

    // Если шина настроек вернула ошибку хотя бы по одному слову
    if (ret1 != 0 || ret2 != 0 || ret3 != 0) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); // -200
        return SCPI_RES_ERR;
    }

    // Форматируем 96-битный UID
    snprintf(uid_str, sizeof(uid_str), "0x%08X-0x%08X-0x%08X", id1, id2, id3);

    // Возвращаем строку хосту
    SCPI_ResultText(context, uid_str);
    return SCPI_RES_OK;
}
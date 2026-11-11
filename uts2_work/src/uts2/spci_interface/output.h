/***************************************************************************************************
 *   Project:       UTS2
 *   Author:        Dymov Igor
 ***************************************************************************************************
 *   Distribution:  
 *
 ***************************************************************************************************
 *   MCU Family:    STM32Fx 
 *   Compiler:      GCC (Zephyr toolchain)
 ***************************************************************************************************
 *   File:          output.h
 *   Description:   Интерфейс обработчиков SCPI-команд управления силовыми выходами и мультиплексорами
 *
 ***************************************************************************************************
 *   History:       2026.11.10 - файл создан и оформлен по стандарту
 *
 **************************************************************************************************/
#pragma once

/***************************************************************************************************
 *                                         INCLUDED FILES
 **************************************************************************************************/
#include "scpi/scpi.h"
#include "scpi/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************************************
 *                                    PUBLIC FUNCTION PROTOTYPES
 **************************************************************************************************/

/// @brief SCPI-команда установки состояния канала питания
scpi_result_t cmd_output_state(scpi_t *_context);

/// @brief SCPI-запрос состояния канала питания
scpi_result_t cmd_output_state_q(scpi_t *_context);

/// @brief SCPI-команда установки состояния всех каналов питания
scpi_result_t cmd_output_all(scpi_t *_context);

/// @brief SCPI-команда полного сброса всех силовых выходов и мультиплексоров
scpi_result_t cmd_out_reset(scpi_t *_context);

/// @brief SCPI-команда управления подтяжкой LIN к GND
scpi_result_t cmd_output_lin_gnd(scpi_t *_context);

/// @brief SCPI-запрос состояния подтяжки LIN к GND
scpi_result_t cmd_output_lin_gnd_q(scpi_t *_context);

/// @brief SCPI-команда управления каналами мультиплексора
scpi_result_t cmd_output_mux(scpi_t *_context);

/// @brief SCPI-запрос состояния каналов мультиплексора
scpi_result_t cmd_output_mux_q(scpi_t *_context);

/// @brief SCPI-команда управления режимом работы дискретного выхода DOUT
scpi_result_t cmd_output_dout(scpi_t *_context);

/// @brief SCPI-запрос режима работы дискретных выходов DOUT
scpi_result_t cmd_output_dout_q(scpi_t *_context);

/// @brief SCPI-команда выбора канала мультиплексора JTAG (1..5)
scpi_result_t cmd_mux_jtag(scpi_t *_context);

/// @brief SCPI-запрос текущего канала мультиплексора JTAG
scpi_result_t cmd_mux_jtag_q(scpi_t *_context);

/// @brief SCPI-команда выбора канала мультиплексора UART (1..4)
scpi_result_t cmd_mux_uart(scpi_t *_context);

/// @brief SCPI-запрос текущего канала мультиплексора UART
scpi_result_t cmd_mux_uart_q(scpi_t *_context);

#ifdef __cplusplus
}
#endif

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
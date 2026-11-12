/**
 *  @file       spci_uts2.c
 *  @headerfile spci_uts2.h
 *
 *  @date       2026.11.11
 *  @author     Dymov Igor
 *
 *  @brief      Сервер парсера SCPI-команд для контрольно-измерительного стенда UTS2
 *  @details    Обеспечивает прием команд через UART с использованием кольцевого буфера,
 *              декодирование и выполнение команд IEEE 488.2 и подсистем стенда.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/ring_buffer.h>

#include "scpi/scpi.h"
#include "scpi/ieee488.h"

#include "spci_uts2.h"
#include "system_bus_model.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
#define SCPI_INPUT_BUF_LEN      256
#define SCPI_ERROR_QUEUE_SIZE   16
#define RING_BUF_SIZE           256

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static size_t        _scpi_write(scpi_t *_context, const char *_data, size_t _len);
static int           _scpi_error(scpi_t *_context, int_fast16_t _err);
static scpi_result_t _scpi_reset(scpi_t *_context);
static void          _uart_cb(const struct device *_dev, void *_user_data);
static void          _scpi_server_thread(void *_p1, void *_p2, void *_p3);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static char          scpi_input_buffer[SCPI_INPUT_BUF_LEN];
static scpi_error_t  scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];
static scpi_t        scpi_context;

RING_BUF_DECLARE(uart_rx_ringbuf, RING_BUF_SIZE);

// Интерфейс коллбэков парсера SCPI
static scpi_interface_t scpi_interface = 
{
    .error   = _scpi_error,
    .write   = _scpi_write,
    .control = NULL,
    .flush   = NULL,
    .reset   = _scpi_reset,
};

// Таблица команд контрольно-измерительного стенда
static const scpi_command_t scpi_commands[] = 
{
    // Обязательные IEEE 488.2 команды
    { .pattern = "*IDN?",                       .callback = scpi_cmd_idn_q          },
    { .pattern = "*RST",                        .callback = scpi_cmd_rst            },
    { .pattern = "*CLS",                        .callback = scpi_cmd_cls            },
    { .pattern = "*OPC",                        .callback = scpi_cmd_opc            },     
    { .pattern = "*OPC?",                       .callback = scpi_cmd_opc_q          },   

    // Подсистема DISPlay
    { .pattern = "DISPlay:LED[:STATe]",          .callback = cmd_disp_led_state      },
    { .pattern = "DISPlay:LED[:STATe]?",         .callback = cmd_disp_led_state_q    },

    // Управление выходами питания
    { .pattern = "OUTPut:POWer[:STATe]",        .callback = cmd_output_state        },
    { .pattern = "OUTPut:POWer[:STATe]?",       .callback = cmd_output_state_q      },
    { .pattern = "OUTPut:POWer:ALL",            .callback = cmd_output_all          },

    // Управление ключами подтяжки LIN к GND
    { .pattern = "OUTPut:LINPulldown[:STATe]",  .callback = cmd_output_lin_gnd      },
    { .pattern = "OUTPut:LINPulldown[:STATe]?", .callback = cmd_output_lin_gnd_q    },

    // Коммутатор интерфейсов USB/JTAG/UART и оптопар
    { .pattern = "OUTPut:MUX[:STATe]",          .callback = cmd_output_mux          },
    { .pattern = "OUTPut:MUX[:STATe]?",         .callback = cmd_output_mux_q        },

    { .pattern = "OUTPut:MUX:JTAG[:CHANnel]",  .callback = cmd_mux_jtag            },
    { .pattern = "OUTPut:MUX:JTAG[:CHANnel]?", .callback = cmd_mux_jtag_q          },
    { .pattern = "OUTPut:MUX:UART[:CHANnel]",  .callback = cmd_mux_uart            },
    { .pattern = "OUTPut:MUX:UART[:CHANnel]?", .callback = cmd_mux_uart_q          },

    { .pattern = "OUTPut:DOUT[:STATe]",         .callback = cmd_output_dout         },
    { .pattern = "OUTPut:DOUT[:STATe]?",        .callback = cmd_output_dout_q       },

    // Подсистема SOURce:
    { .pattern = "SOURce:DAC[:VALue]",          .callback = cmd_source_dac          },
    { .pattern = "SOURce:DAC[:VALue]?",         .callback = cmd_source_dac_q        },
    
    // I2C цифровое переменное сопротивление
    { .pattern = "SOURce:RESistance[:VALue]",  .callback = cmd_source_res          },
    { .pattern = "SOURce:RESistance[:VALue]?", .callback = cmd_source_res_q        },

    // Подсистема SENSe:UART (Приемник телеметрии DUT)
    { .pattern = "SENSe:UART:DATA?",            .callback = cmd_sense_uart_data_q   },
    { .pattern = "SENSe:UART:COUNt?",           .callback = cmd_sense_uart_count_q  },
    { .pattern = "SENSe:UART:CLEar",            .callback = cmd_sense_uart_clear    },
    { .pattern = "SENSe:UART:BAUDrate",         .callback = cmd_sense_uart_baud     },
    { .pattern = "SENSe:UART:BAUDrate?",        .callback = cmd_sense_uart_baud_q   },

    { .pattern = "SENSe:TEMPerature[:DATA]?",    .callback = cmd_sense_temp_q        },
    { .pattern = "SENSe:LIGht[:DATA]?",          .callback = cmd_sense_light_q       },
    { .pattern = "SENSe:VOLTage[:DC]?",          .callback = cmd_sense_voltage_q     },
    
    // Опциональный псевдоним VISA
    { .pattern = "MEASure:VOLTage[:DC]?",        .callback = cmd_sense_voltage_q     },

    // Подсистема SYSTem
    { .pattern = "SYSTem:WAIT",                 .callback = cmd_system_wait         },
    { .pattern = "SYSTem:UID?",                 .callback = cmd_system_uid_q        },
    { .pattern = "SYSTem:SERial",               .callback = cmd_system_serial,      },
    { .pattern = "SYSTem:SERial?",              .callback = cmd_system_serial_q,    },
    { .pattern = "SYSTem:HARDware[:VERSion]",  .callback = cmd_system_hardware, },
    { .pattern = "SYSTem:HARDware[:VERSion]?", .callback = cmd_system_hardware_q, },
    SCPI_CMD_LIST_END
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Коллбэк отправки данных обратно клиенту по UART
 *  @details    Побайтово отправляет сформированную парсером SCPI строку через UART.
 *
 *  @param      _context - Указатель на контекст SCPI
 *  @param      _data    - Указатель на буфер с данными
 *  @param      _len     - Длина отправляемых данных в байтах
 *
 *  @return     size_t - Количество отправленных байт
 */
static size_t _scpi_write(scpi_t *_context, const char *_data, size_t _len) 
{
    ARG_UNUSED(_context);

    for (size_t i = 0; i < _len; i++) 
    {
        uart_poll_out(uart_dev, _data[i]);
    }
    return _len;
}

/**
 *  @brief      Обработчик ошибок парсера SCPI
 *  @details    Выводит расшифровку ошибки в системную консоль RTT и отправляет
 *              сообщение с кодом ошибки клиенту в терминал.
 *
 *  @param      _context - Указатель на контекст SCPI
 *  @param      _err     - Код ошибки SCPI
 *
 *  @return     int - Всегда 0
 */
static int _scpi_error(scpi_t *_context, int_fast16_t _err) 
{
    if (_err == 0) 
    {
        return 0;
    }

    const char *err_desc = SCPI_ErrorTranslate(_err);

    // 1. Вывод в системную консоль и RTT Zephyr
    printk("[SCPI ERROR] Code: %d, Description: %s\r\n", (int)_err, err_desc);

    // 2. Вывод прямо в терминал клиенту для отображения причины ошибки
    char err_msg[128];
    int len = snprintf(err_msg, sizeof(err_msg), "ERR: %d, \"%s\"\r\n", (int)_err, err_desc);

    if (true
        && len > 0 
        && _context->interface != NULL 
        && _context->interface->write != NULL
       ) 
    {
        _context->interface->write(_context, err_msg, len);
    }

    return 0;
}

/**
 *  @brief      Команды сброса стенда (*RST)
 *  @details    Выполняет сброс всех компонентов стенда (светодиоды, выходы,
 *              источники ЦАП и резисторы) в состояние по умолчанию.
 *
 *  @param      _context - Указатель на контекст SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
static scpi_result_t _scpi_reset(scpi_t *_context) 
{
    cmd_led_reset(_context);
    cmd_out_reset(_context);
    cmd_source_reset(_context);

    return SCPI_RES_OK;
}

/**
 *  @brief      Обработчик прерывания UART по приему данных
 *  @details    Считывает байты из аппаратного FIFO UART и помещает их в кольцевой буфер.
 *
 *  @param      _dev       - Указатель на устройство UART
 *  @param      _user_data - Пользовательские данные
 */
static void _uart_cb(const struct device *_dev, void *_user_data)
{
    ARG_UNUSED(_user_data);
    uint8_t c;

    if (!uart_irq_update(_dev)) 
    {
        return;
    }

    while (uart_irq_rx_ready(_dev)) 
    {
        int len = uart_fifo_read(_dev, &c, 1);
        if (len > 0) 
        {
            ring_buf_put(&uart_rx_ringbuf, &c, 1);
        }
    }
}

/**
 *  @brief      Главный поток сервера SCPI
 *  @details    Инициализирует парсер SCPI, включает прерывания UART и обрабатывает
 *              входящие символы из кольцевого буфера.
 *
 *  @param      _p1 - Неиспользуемый параметр потока 1
 *  @param      _p2 - Неиспользуемый параметр потока 2
 *  @param      _p3 - Неиспользуемый параметр потока 3
 */
static void _scpi_server_thread(void *_p1, void *_p2, void *_p3)
{
    ARG_UNUSED(_p1);
    ARG_UNUSED(_p2);
    ARG_UNUSED(_p3);

    // Инициализация контекста SCPI
    SCPI_Init(&scpi_context,
              scpi_commands,
              &scpi_interface,
              scpi_units_def,
              "Author-ALARM",           // Производитель (IDN1)
              "UST2",                   // Модель        (IDN2)
              "SN123456",               // Серийный номер(IDN3)
              "v1.0.0",                 // Прошивка      (IDN4)
              scpi_input_buffer,
              SCPI_INPUT_BUF_LEN,
              scpi_error_queue_data,
              SCPI_ERROR_QUEUE_SIZE);

    // Включение прерываний UART по приему
    uart_irq_callback_user_data_set(uart_dev, _uart_cb, NULL);
    uart_irq_rx_enable(uart_dev);
    
    uint8_t buffer[SCPI_INPUT_BUF_LEN];

    // Цикл приема и обработки входящих данных
    while (1) 
    {
        uint32_t bytes_read = ring_buf_get(&uart_rx_ringbuf, buffer, sizeof(buffer));
        if (bytes_read > 0) 
        {
            SCPI_Input(&scpi_context, (char *)buffer, bytes_read);
        } 
        else 
        {        
            k_msleep(2);
        }        
    }
}

K_THREAD_DEFINE(scpi_tid, 2048, _scpi_server_thread, NULL, NULL, NULL, 10, 0, 0);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
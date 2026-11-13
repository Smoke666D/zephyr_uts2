/**
 *  @file       spci_uts2.c
 *  @headerfile spci_uts2.h
 *
 *  @date       2026.11.11
 *  @author     Dymov Igor
 *
 *  @brief      Сервер парсера SCPI-команд для контрольно-измерительного стенда UTS2
 *  @details    Обеспечивает прием команд через UART и выполнение асинхронных 
 *              пользовательских задач в едином потоке воркера (k_work_q),
 *              полностью исключая пересечение и порчу данных в UART.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/init.h>
#include "app_worker.h"
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
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static size_t        _scpi_write(scpi_t *_context, const char *_data, size_t _len);
static int           _scpi_error(scpi_t *_context, int_fast16_t _err);
static scpi_result_t _scpi_reset(scpi_t *_context);
static void          _uart_cb(const struct device *_dev, void *_user_data);
static void          _scpi_rx_work_handler(struct k_work *_work);
static int           _scpi_server_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static char          scpi_input_buffer[SCPI_INPUT_BUF_LEN];
static scpi_error_t  scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];
static scpi_t        scpi_context;


RING_BUF_DECLARE(uart_rx_ringbuf, RING_BUF_SIZE);

static struct k_work scpi_rx_work;

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
    { .pattern = "DISPlay:LED[:STATe]",         .callback = cmd_disp_led_state      },
    { .pattern = "DISPlay:LED[:STATe]?",        .callback = cmd_disp_led_state_q    },

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

    { .pattern = "OUTPut:MUX:JTAG[:CHANnel]",   .callback = cmd_mux_jtag            },
    { .pattern = "OUTPut:MUX:JTAG[:CHANnel]?",  .callback = cmd_mux_jtag_q          },
    { .pattern = "OUTPut:MUX:UART[:CHANnel]",   .callback = cmd_mux_uart            },
    { .pattern = "OUTPut:MUX:UART[:CHANnel]?",  .callback = cmd_mux_uart_q          },

    { .pattern = "OUTPut:DOUT[:STATe]",         .callback = cmd_output_dout         },
    { .pattern = "OUTPut:DOUT[:STATe]?",        .callback = cmd_output_dout_q       },

    // Подсистема SOURce:
    { .pattern = "SOURce:DAC[:VALue]",          .callback = cmd_source_dac          },
    { .pattern = "SOURce:DAC[:VALue]?",         .callback = cmd_source_dac_q        },
    
    // I2C цифровое переменное сопротивление
    { .pattern = "SOURce:RESistance[:VALue]",   .callback = cmd_source_res          },
    { .pattern = "SOURce:RESistance[:VALue]?",  .callback = cmd_source_res_q        },

    // Подсистема SENSe:UART (Приемник телеметрии DUT)
    { .pattern = "SENSe:UART:DATA?",            .callback = cmd_sense_uart_data_q   },
    { .pattern = "SENSe:UART:COUNt?",           .callback = cmd_sense_uart_count_q  },
    { .pattern = "SENSe:UART:CLEar",            .callback = cmd_sense_uart_clear    },
    { .pattern = "SENSe:UART:BAUDrate",         .callback = cmd_sense_uart_baud     },
    { .pattern = "SENSe:UART:BAUDrate?",        .callback = cmd_sense_uart_baud_q   },

    { .pattern = "SENSe:TEMPerature[:DATA]?",   .callback = cmd_sense_temp_q        },
    { .pattern = "SENSe:LIGht[:DATA]?",         .callback = cmd_sense_light_q       },
    { .pattern = "SENSe:VOLTage[:DC]?",         .callback = cmd_sense_voltage_q     },
    { .pattern = "MEASure:VOLTage[:DC]?",       .callback = cmd_sense_voltage_q     },

    /* Подсистема SENSe: Датчики тока и мощности (8 каналов) */
    { .pattern = "SENSe:CURRent[:DC]?",     .callback = cmd_sense_current_q,       },
    { .pattern = "MEASure:CURRent[:DC]?",   .callback = cmd_sense_current_q,       }, // Псевдоним

    { .pattern = "SENSe:POWer[:DC]?",       .callback = cmd_sense_power_q,         },
    { .pattern = "MEASure:POWer[:DC]?",     .callback = cmd_sense_power_q,         }, // Псевдоним

    { .pattern = "SENSe:POWer:VOLTage?",    .callback = cmd_sense_power_voltage_q, },
    { .pattern = "SENSe:POWer:ALL?",        .callback = cmd_sense_power_all_q,     },

    // Подсистема SYSTem
    { .pattern = "SYSTem:ERRor[:NEXT]?",       .callback = SCPI_SystemErrorNextQ    },
    { .pattern = "SYSTem:ERRor:COUNt?",        .callback = SCPI_SystemErrorCountQ   },
    { .pattern = "SYSTem:WAIT",                 .callback = cmd_system_wait         },
    { .pattern = "SYSTem:UID?",                 .callback = cmd_system_uid_q        },
    { .pattern = "SYSTem:SERial",               .callback = cmd_system_serial,      },
    { .pattern = "SYSTem:SERial?",              .callback = cmd_system_serial_q,    },
    { .pattern = "SYSTem:HARDware[:VERSion]",   .callback = cmd_system_hardware,    },
    { .pattern = "SYSTem:HARDware[:VERSion]?",  .callback = cmd_system_hardware_q,  },
    { .pattern = "SYSTem:USER",                 .callback = cmd_system_user,        },
    { .pattern = "SYSTem:USER?",                .callback = cmd_system_user_q,      },


    { .pattern = "MONItor[:VOLTage]:STARt",  .callback = cmd_monitor_start,         },
    { .pattern = "MONItor[:VOLTage]:STOP",   .callback = cmd_monitor_stop,          },
    { .pattern = "MONItor[:VOLTage]:STATe?", .callback = cmd_monitor_state_q,       },
    
    SCPI_CMD_LIST_END
};

/***************************************************************************************************
 *                                        PUBLIC API ДЛЯ АСИНХРОННЫХ ЗАДАЧ
 **************************************************************************************************/

/**
 *  @brief      Прямая безопасная отправка строки клиенту из контекста асинхронной задачи
 */
void scpi_async_send_text(const char *text)
{
    if (text != NULL) {
        _scpi_write(&scpi_context, text, strlen(text));
    }
}

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

static size_t _scpi_write(scpi_t *_context, const char *_data, size_t _len) 
{
    ARG_UNUSED(_context);

    for (size_t i = 0; i < _len; i++) 
    {
        uart_poll_out(uart_dev, _data[i]);
    }
    return _len;
}

static int _scpi_error(scpi_t *_context, int_fast16_t _err) 
{
    if (_err == 0) 
    {
        return 0;
    }

    const char *err_desc = SCPI_ErrorTranslate(_err);

    // 1. Системный лог
    printk("[SCPI ERROR] Code: %d, Description: %s\r\n", (int)_err, err_desc);

    // 2. Ответ клиенту
    char err_msg[128];
    int len = snprintf(err_msg, sizeof(err_msg), "ERR: %d, \"%s\"\r\n", (int)_err, err_desc);

    if (len > 0 && _context->interface != NULL && _context->interface->write != NULL) 
    {
        _context->interface->write(_context, err_msg, len);
    }

    return 0;
}

static scpi_result_t _scpi_reset(scpi_t *_context) 
{
    cmd_led_reset(_context);
    cmd_out_reset(_context);
    cmd_source_reset(_context);
    cmd_monitor_reset(_context);
    return SCPI_RES_OK;
}

/**
 *  @brief      Прерывание UART: только складывает байты в буфер и будит воркер
 */
static void _uart_cb(const struct device *_dev, void *_user_data)
{
    ARG_UNUSED(_user_data);
    uint8_t c;
    bool has_data = false;

    if (!uart_irq_update(_dev)) 
    {
        return;
    }

    while (uart_irq_rx_ready(_dev)) 
    {
        if (uart_fifo_read(_dev, &c, 1) > 0) 
        {
            if (ring_buf_put(&uart_rx_ringbuf, &c, 1) > 0) 
            {
                has_data = true;
            }
        }
    }

    // Если поступили байты — активируем обработчик в очереди воркера
    if (has_data) 
    {
        app_worker_submit(&scpi_rx_work, APPLICATION_WORKER);
    }
}

/**
 *  @brief      Обработчик воркера: скармливает принятые байты парсеру
 *  @note       Выполняется в контексте scpi_work_q
 */
static void _scpi_rx_work_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    uint8_t buffer[64];
    uint32_t bytes_read;

    // Выгребаем все доступные байты пачками
    while ((bytes_read = ring_buf_get(&uart_rx_ringbuf, buffer, sizeof(buffer))) > 0) 
    {
        SCPI_Input(&scpi_context, (char *)buffer, bytes_read);
    }
}

/**
 *  @brief      Инициализация сервера SCPI и запуск выделенного воркера
 */
static int _scpi_server_init(void)
{
    // 1. Инициализация контекста библиотеки libscpi
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

    k_work_init(&scpi_rx_work, _scpi_rx_work_handler);

    scpi_monitor_init();
    // 4. Включение прерываний UART
    uart_irq_callback_user_data_set(uart_dev, _uart_cb, NULL);
    uart_irq_rx_enable(uart_dev);

    return 0;
}

SYS_INIT(_scpi_server_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
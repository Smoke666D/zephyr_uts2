#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include <zephyr/drivers/uart.h>
#include "scpi/scpi.h"
 #include "scpi/ieee488.h"
 #include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/ring_buffer.h>
#include "spci_uts2.h"

#include "system_bus_model.h"

static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#define SCPI_INPUT_BUF_LEN     256
#define SCPI_ERROR_QUEUE_SIZE  16

static char scpi_input_buffer[SCPI_INPUT_BUF_LEN];
static scpi_error_t scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];
static scpi_t scpi_context;


// Колбэк отправки данных обратно на ПК
static size_t SCPI_Write(scpi_t *context, const char *data, size_t len) {
    (void)context;
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart_dev, data[i]);
    }
    return len;
}

static scpi_result_t SCPI_Reset(scpi_t *context) {
    // Сброс вашего стенда в дефолтное состояние (*RST)

    cmd_led_reset(context);
    cmd_out_reset(context);

    return SCPI_RES_OK;
}

static int SCPI_Error(scpi_t *context, int_fast16_t err) {
    // Логирование ошибки парсера
    if (err == 0) return 0;

    const char *err_desc = SCPI_ErrorTranslate(err);

    // 1. Вывод в системную консоль / RTT Zephyr
    printk("[SCPI ERROR] Code: %d, Description: %s\r\n", (int)err, err_desc);

    // 2. Вывод прямо в терминал клиенту (чтобы сразу видеть причину в окне отправки)
    char err_msg[128];
    int len = snprintf(err_msg, sizeof(err_msg), "ERR: %d, \"%s\"\r\n", (int)err, err_desc);
    if (len > 0 && context->interface && context->interface->write) {
        context->interface->write(context, err_msg, len);
    }

    return 0;
}

// Интерфейс колбэков
static scpi_interface_t scpi_interface = {
    .error = SCPI_Error,
    .write = SCPI_Write,
    .control = NULL,
    .flush = NULL,
    .reset = SCPI_Reset,
};

// Обработчик команды установки напряжения: e.g. "SOURce:VOLTage 3.3"
static scpi_result_t cmd_source_voltage(scpi_t *context) {
    double volt;
    if (!SCPI_ParamDouble(context, &volt, TRUE)) {
        return SCPI_RES_ERR;
    }
    // Здесь ваш вызов Zephyr API: dac_write_value(...) или управление питанием
    return SCPI_RES_OK;
}

// Обработчик чтения напряжения: e.g. "MEASure:VOLTage?"
static scpi_result_t cmd_measure_voltage_q(scpi_t *context) {
    double measured_val = 3.298; // Результат из adc_read(...)
    
    // Отправляем ответ клиенту
    SCPI_ResultDouble(context, measured_val);
    return SCPI_RES_OK;
}


static scpi_result_t scpi_cmd_idn_q(scpi_t *context) {
    for (int i = 0; i < 4; i++) {
        if (context->idn[i]) {
            SCPI_ResultMnemonic(context, context->idn[i]);
        } else {
            SCPI_ResultMnemonic(context, "0");
        }
    }
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_rst(scpi_t *context) {
    if (context && context->interface && context->interface->reset) {
        return context->interface->reset(context);
    }
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_cls(scpi_t *context) {
    SCPI_ErrorClear(context);
    return SCPI_RES_OK;
}

/* Запрос Operation Complete: возвращает 1 */
static scpi_result_t scpi_cmd_opc_q(scpi_t *context) {
    SCPI_ResultInt32(context, 1);
    return SCPI_RES_OK;
}

/* Команда Operation Complete (без вопроса): просто подтверждаем выполнение */
static scpi_result_t scpi_cmd_opc(scpi_t *context) {
    return SCPI_RES_OK;
}




// Таблица команд стенда
static const scpi_command_t scpi_commands[] = {
    /* Обязательные IEEE 488.2 команды (поддерживаются библиотекой из коробки) */
    { .pattern = "*IDN?", .callback = scpi_cmd_idn_q, },
    { .pattern = "*RST",  .callback = scpi_cmd_rst, },
    { .pattern = "*CLS",  .callback = scpi_cmd_cls, },
    { .pattern = "*OPC",        .callback = scpi_cmd_opc, },     
    { .pattern = "*OPC?",       .callback = scpi_cmd_opc_q, },   

    /* Подсистема DISPlay в параметрическом стиле */
    { .pattern = "DISPlay:LED[:STATe]",  .callback = cmd_disp_led_state, },
    { .pattern = "DISPlay:LED[:STATe]?", .callback = cmd_disp_led_state_q, },

    /* Управление выходами питания */
    { .pattern = "OUTPut:POWer[:STATe]",        .callback = cmd_output_state, },
    { .pattern = "OUTPut:POWer[:STATe]?",       .callback = cmd_output_state_q, },
    { .pattern = "OUTPut:POWer:ALL",          .callback = cmd_output_all, },

     /* Управление ключами подтяжки LIN к GND */
    { .pattern = "OUTPut:LINPulldown[:STATe]",  .callback = cmd_output_lin_gnd, },
    { .pattern = "OUTPut:LINPulldown[:STATe]?", .callback = cmd_output_lin_gnd_q, },

    /* Коммутатор интерфейсов USB/JTAG/UART и оптопар */
    { .pattern = "OUTPut:MUX[:STATe]",  .callback = cmd_output_mux, },
    { .pattern = "OUTPut:MUX[:STATe]?", .callback = cmd_output_mux_q, },
    
    { .pattern = "OUTPut:MUX:JTAG[:CHANnel]",  .callback = cmd_mux_jtag, },
    { .pattern = "OUTPut:MUX:JTAG[:CHANnel]?", .callback = cmd_mux_jtag_q, },
    { .pattern = "OUTPut:MUX:UART[:CHANnel]",  .callback = cmd_mux_uart, },
    { .pattern = "OUTPut:MUX:UART[:CHANnel]?", .callback = cmd_mux_uart_q, },

    { .pattern = "OUTPut:DOUT[:STATe]",  .callback = cmd_output_dout, },
    { .pattern = "OUTPut:DOUT[:STATe]?", .callback = cmd_output_dout_q, },

    SCPI_CMD_LIST_END
};


#define RING_BUF_SIZE 256
RING_BUF_DECLARE(uart_rx_ringbuf, RING_BUF_SIZE);


// Колбэк прерывания UART: вызывается аппаратно на КАЖДЫЙ байт
static void uart_cb(const struct device *dev, void *user_data)
{
    uint8_t c;
    if (!uart_irq_update(dev)) return;

    while (uart_irq_rx_ready(dev)) {
        int len = uart_fifo_read(dev, &c, 1);
        if (len > 0) {
            ring_buf_put(&uart_rx_ringbuf, &c, 1);
        }
    }
}


static scpi_t scpi_context;

void scpi_server_thread(void)
{
    // Инициализация SCPI контекста
     SCPI_Init(&scpi_context,
              scpi_commands,
              &scpi_interface,
              scpi_units_def,
              "Author-ALARM",            /* Производитель (IDN1) */
              "UST2",         /* Модель        (IDN2) */
              "SN123456",             /* Серийник      (IDN3) */
              "v1.0.0",               /* Прошивка      (IDN4) */
              scpi_input_buffer,
              SCPI_INPUT_BUF_LEN,
              scpi_error_queue_data,
              SCPI_ERROR_QUEUE_SIZE);

    // Цикл приема данных (например, через опрос UART или ring_buf)
    uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL);
    uart_irq_rx_enable(uart_dev);
    uint8_t buffer[128];

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

K_THREAD_DEFINE(scpi_tid, 2048, scpi_server_thread, NULL, NULL, NULL, 10, 0, 0);
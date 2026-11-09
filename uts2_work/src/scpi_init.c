#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include "scpi/scpi.h"
 #include "scpi/ieee488.h"
 #include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

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
    return SCPI_RES_OK;
}

static int SCPI_Error(scpi_t *context, int_fast16_t err) {
    // Логирование ошибки парсера
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



/* ==================== RED LED ==================== */
static scpi_result_t cmd_led_red(scpi_t *context) {
    scpi_bool_t val;
    // TRUE в третьем параметре делает аргумент обязательным
    if (!SCPI_ParamBool(context, &val, TRUE)) {
        return SCPI_RES_ERR;
    }
    state_r = val;
    if (gpio_is_ready_dt(&led_r)) {
        gpio_pin_set_dt(&led_r, state_r ? 1 : 0);
    }
    return SCPI_RES_OK;
}

static scpi_result_t cmd_led_red_q(scpi_t *context) {
    SCPI_ResultBool(context, state_r);
    return SCPI_RES_OK;
}

/* ==================== GREEN LED ==================== */
static scpi_result_t cmd_led_green(scpi_t *context) {
    scpi_bool_t val;
    if (!SCPI_ParamBool(context, &val, TRUE)) {
        return SCPI_RES_ERR;
    }
    state_g = val;
    if (gpio_is_ready_dt(&led_g)) {
        gpio_pin_set_dt(&led_g, state_g ? 1 : 0);
    }
    return SCPI_RES_OK;
}

static scpi_result_t cmd_led_green_q(scpi_t *context) {
    SCPI_ResultBool(context, state_g);
    return SCPI_RES_OK;
}

/* ==================== BLUE LED ==================== */
static scpi_result_t cmd_led_blue(scpi_t *context) {
    scpi_bool_t val;
    if (!SCPI_ParamBool(context, &val, TRUE)) {
        return SCPI_RES_ERR;
    }
    state_b = val;
    if (gpio_is_ready_dt(&led_b)) {
        gpio_pin_set_dt(&led_b, state_b ? 1 : 0);
    }
    return SCPI_RES_OK;
}

static scpi_result_t cmd_led_blue_q(scpi_t *context) {
    SCPI_ResultBool(context, state_b);
    return SCPI_RES_OK;
}


// Таблица команд стенда
static const scpi_command_t scpi_commands[] = {
    /* Обязательные IEEE 488.2 команды (поддерживаются библиотекой из коробки) */
    { .pattern = "*IDN?", .callback = scpi_cmd_idn_q, },
    { .pattern = "*RST",  .callback = scpi_cmd_rst, },
    { .pattern = "*CLS",  .callback = scpi_cmd_cls, },

        /* Управление светодиодами */
    { .pattern = "LED:RED",     .callback = cmd_led_red, },
    { .pattern = "LED:RED?",    .callback = cmd_led_red_q, },
    { .pattern = "LED:GREen",   .callback = cmd_led_green, },
    { .pattern = "LED:GREen?",  .callback = cmd_led_green_q, },
    { .pattern = "LED:BLUe",    .callback = cmd_led_blue, },
    { .pattern = "LED:BLUe?",   .callback = cmd_led_blue_q, },
        

    SCPI_CMD_LIST_END
};

#define SCPI_INPUT_BUF_LEN 256
static char scpi_input_buffer[SCPI_INPUT_BUF_LEN];

static scpi_t scpi_context;

void scpi_server_thread(void)
{
    // Инициализация SCPI контекста
     SCPI_Init(&scpi_context,
              scpi_commands,
              &scpi_interface,
              scpi_units_def,
              "MyCompany",            /* Производитель (IDN1) */
              "TestBench-01",         /* Модель        (IDN2) */
              "SN123456",             /* Серийник      (IDN3) */
              "v1.0.0",               /* Прошивка      (IDN4) */
              scpi_input_buffer,
              SCPI_INPUT_BUF_LEN,
              scpi_error_queue_data,
              SCPI_ERROR_QUEUE_SIZE);

    // Цикл приема данных (например, через опрос UART или ring_buf)
    char rx_char;
    while (1) {
        if (uart_poll_in(uart_dev, (unsigned char *)&rx_char) == 0) {
            // Скармливаем байты парсеру.
            // Он сам определит конец строки (\r, \n) или разделители (;)
            SCPI_Input(&scpi_context, &rx_char, 1);
        } else {
            k_sleep(K_MSEC(2));
        }
    }
}

K_THREAD_DEFINE(scpi_tid, 2048, scpi_server_thread, NULL, NULL, NULL, 5, 0, 0);
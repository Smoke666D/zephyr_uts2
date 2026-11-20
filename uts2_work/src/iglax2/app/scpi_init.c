/**
 *  @file       spci_uts2.c
 *  @headerfile spci_uts2.h
 *
 *  @date       2026.11.11
 *  @author     Dymov Igor
 *
 *  @brief      Сервер парсера SCPI-команд для контрольно-измерительного стенда UTS2 (SEGGER RTT)
 *  @details    Обеспечивает прием и отправку команд через SEGGER RTT (Channel 0) 
 *              и выполнение задач в едином потоке воркера (APPLICATION_WORKER).
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <SEGGER_RTT.h>

#include "app_worker.h"
#include "scpi/scpi.h"
#include "scpi/ieee488.h"

#include "spci_iglax2.h"
#include "system_bus_model.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
#define SCPI_RTT_CHANNEL        0       // Канал 0 (стандартный терминал J-Link RTT)
#define SCPI_INPUT_BUF_LEN      256
#define SCPI_ERROR_QUEUE_SIZE   16

#define RTT_POLL_FAST_MS        1U      // Период опроса во время активного приема данных
#define RTT_POLL_IDLE_MS        5U      // Период опроса в режиме ожидания

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static size_t        _scpi_write(scpi_t *_context, const char *_data, size_t _len);
static int           _scpi_error(scpi_t *_context, int_fast16_t _err);
static scpi_result_t _scpi_reset(scpi_t *_context);
static void          _scpi_rx_work_handler(struct k_work *_work);
static int           _scpi_server_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static char                    scpi_input_buffer[SCPI_INPUT_BUF_LEN];
static scpi_error_t            scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];
static scpi_t                  scpi_context;

/* Отложенная задача для опроса RTT буфера в контексте APPLICATION_WORKER */
static struct k_work_delayable scpi_rx_work;

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

/**
 *  @brief      Отправка данных клиенту напрямую через буфер SEGGER RTT
 */
static size_t _scpi_write(scpi_t *_context, const char *_data, size_t _len) 
{
    ARG_UNUSED(_context);

    return SEGGER_RTT_Write(SCPI_RTT_CHANNEL, _data, _len);
}

static int _scpi_error(scpi_t *_context, int_fast16_t _err) 
{
    if (_err == 0) 
    {
        return 0;
    }

    const char *err_desc = SCPI_ErrorTranslate(_err);

    // 1. Системный лог (уйдет в UART консоль)
    printk("[SCPI ERROR] Code: %d, Description: %s\r\n", (int)_err, err_desc);

    // 2. Ответ клиенту в RTT
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
    ARG_UNUSED(_context);
    return SCPI_RES_OK;
}

/**
 *  @brief      Обработчик воркера: вычитывает принятые байты из RTT и скармливает парсеру
 *  @note       Выполняется в контексте APPLICATION_WORKER
 */
static void _scpi_rx_work_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    uint8_t buffer[64];
    unsigned int bytes_read;
    bool has_data = false;

    bytes_read = SEGGER_RTT_Read(SCPI_RTT_CHANNEL, buffer, sizeof(buffer));
    // Выгребаем все доступные байты напрямую из Down-буфера RTT
    if (bytes_read  > 0) 
    {
        has_data = true;
        SCPI_Input(&scpi_context, (char *)buffer, bytes_read);
    }

    // Если шли данные — опрашиваем чаще (1 мс), если простой — каждые 5 мс
    uint32_t next_delay_ms = has_data ? RTT_POLL_FAST_MS : RTT_POLL_IDLE_MS;

    app_worker_reschedule_submit(&scpi_rx_work, K_MSEC(next_delay_ms), APPLICATION_WORKER);
}

/**
 *  @brief      Инициализация сервера SCPI поверх RTT и запуск поллера в воркере
 */
static int _scpi_server_init(void)
{
    // 1. Инициализация SEGGER RTT
    SEGGER_RTT_Init();

    // 2. Инициализация контекста библиотеки libscpi
    SCPI_Init(&scpi_context,
              scpi_commands,
              &scpi_interface,
              scpi_units_def,
              "Author-ALARM",           // Производитель (IDN1)
              "IGLAX2",                   // Модель        (IDN2)
              "SN123456",               // Серийный номер(IDN3)
              "v1.0.0",                 // Прошивка      (IDN4)
              scpi_input_buffer,
              SCPI_INPUT_BUF_LEN,
              scpi_error_queue_data,
              SCPI_ERROR_QUEUE_SIZE);


    SCPI_RegSet(&scpi_context, SCPI_REG_ESR, ESR_PON);

    // 3. Запуск периодического опроса RTT в очереди APPLICATION_WORKER
    k_work_init_delayable(&scpi_rx_work, _scpi_rx_work_handler);
    app_worker_reschedule_submit(&scpi_rx_work, K_NO_WAIT, APPLICATION_WORKER);

    return 0;
}

//SYS_INIT(_scpi_server_init, APPLICATION, 80);
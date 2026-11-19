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
#include <zephyr/init.h>
#include "app_worker.h"
#include "scpi/scpi.h"
#include "scpi/ieee488.h"
#include "spci_uts2.h"
#include "system_bus_model.h"
#include <SEGGER_RTT.h>

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
#define SCPI_INPUT_BUF_LEN      256
#define SCPI_ERROR_QUEUE_SIZE   16
#define SCPI_RTT_CHANNEL        0      // Стандартный RTT терминал (канал 0)

#define RTT_POLL_ACTIVE_MS      0      // Если приходят байты — читаем мгновенно
#define RTT_POLL_IDLE_MS        5      // Когда в RTT пусто — опрос каждые 5 мс
/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static size_t        _scpi_write(scpi_t *_context, const char *_data, size_t _len);
static int           _scpi_error(scpi_t *_context, int_fast16_t _err);
static scpi_result_t _scpi_reset(scpi_t *_context);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/

static char          scpi_input_buffer[SCPI_INPUT_BUF_LEN];
static scpi_error_t  scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];
static scpi_t        scpi_context;

/* Таймер опроса RTT в очереди воркера */
static struct k_work_delayable scpi_rtt_work;

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
   
    return SCPI_RES_OK;
}


/**
 *  @brief  Адаптивный опрос входящего RTT-буфера в контексте APPLICATION_WORKER
 */
static void _scpi_rtt_poll_handler(struct k_work *_work)
{
    uint8_t buffer[64];
    unsigned int bytes_read;
    bool had_traffic = false;

    // Вычитываем все байты из Down-буфера RTT, поступившие от хоста через J-Link
    while ((bytes_read = SEGGER_RTT_Read(SCPI_RTT_CHANNEL, buffer, sizeof(buffer))) > 0) 
    {
        had_traffic = true;
        SCPI_Input(&scpi_context, (char *)buffer, bytes_read);
    }

    // Адаптивное планирование следующего опроса:
    // Если идет поток данных — проверяем немедленно (0 мс), если пусто — спим 5 мс
    uint32_t next_delay = had_traffic ? RTT_POLL_ACTIVE_MS : RTT_POLL_IDLE_MS;

    app_worker_reschedule_submit(&scpi_rtt_work, K_MSEC(next_delay), APPLICATION_WORKER);
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
              "IGLA_X2",                   // Модель        (IDN2)
              "SN123456",               // Серийный номер(IDN3)
              "v1.0.0",                 // Прошивка      (IDN4)
              scpi_input_buffer,
              SCPI_INPUT_BUF_LEN,
              scpi_error_queue_data,
              SCPI_ERROR_QUEUE_SIZE);

    SCPI_RegSet(&scpi_context, SCPI_REG_ESR, ESR_PON);

    k_work_init_delayable(&scpi_rtt_work, _scpi_rtt_poll_handler);
    app_worker_reschedule_submit(&scpi_rtt_work, K_NO_WAIT, APPLICATION_WORKER);

    return 0;
}

SYS_INIT(_scpi_server_init, APPLICATION, 80);
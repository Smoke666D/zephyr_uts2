#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "sense.h"
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"


#define SCPI_READ_CHUNK_MAX 100

K_MSGQ_DEFINE(uart_detect_rx_queue, sizeof(uint8_t), SCPI_READ_CHUNK_MAX, 4);


static void _uart_detect_rx_listener_cb(const struct zbus_channel *_chan)
{
    const uint32_t *val = zbus_chan_const_msg(_chan);
    uint8_t _msg = (uint8_t)(*val);  
    
    k_msgq_put(&uart_detect_rx_queue, &_msg, K_NO_WAIT);
    
}

ZBUS_LISTENER_DEFINE(uart_detect_rx_listener, _uart_detect_rx_listener_cb);


static int init_uart_detect_worker(void)
{
    bus_listener_attach(&uart_detect_rx_listener, UART_DETECT_RX);
    return 0;
}
SYS_INIT(init_uart_detect_worker, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);


/* Буфер для формирования ответа */
static uint8_t scpi_rx_temp_buf[SCPI_READ_CHUNK_MAX];

/* SENSe:UART:DATA? [<max_bytes>]
 * Вычитывает накопленные байты из буфера.
 * Если буфер пуст -> возвращает пустую строку "".
 */
scpi_result_t cmd_sense_uart_data_q(scpi_t *context)
{
    int32_t req_bytes = 0;
    uint32_t to_read = SCPI_READ_CHUNK_MAX;

    // 1. Читаем опциональный параметр: ограничение размера
    if (SCPI_ParamInt32(context, &req_bytes, FALSE)) 
    {
        if (req_bytes <= 0) 
        {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }
        if ((uint32_t)req_bytes < to_read) 
        {
            to_read = (uint32_t)req_bytes;
        }
    }

    // 2. Вычитываем байты из очереди k_msgq без блокировки (K_NO_WAIT)
    uint32_t actual_read = 0;
    while (actual_read < to_read) 
    {
        uint8_t byte;
        // Извлекаем по 1 байту
        if (k_msgq_get(&uart_detect_rx_queue, &byte, K_NO_WAIT) != 0) 
        {
            break; // Очередь опустела, выходим
        }
        scpi_rx_temp_buf[actual_read++] = byte;
    }

    // 3. Отдаем результат в SCPI
    if (actual_read > 0) 
    {
        SCPI_ResultCharacters(context, (const char *)scpi_rx_temp_buf, actual_read);
    } 
    else 
    {
        // Если очередь была пуста — возвращаем пустую строку ""
        SCPI_ResultCharacters(context, "", 0);
    }

    return SCPI_RES_OK;
}

/* SENSe:UART:COUNt?
 * Возвращает количество байт, накопленных в буфере на данный момент.
 */
scpi_result_t cmd_sense_uart_count_q(scpi_t *context)
{
    uint32_t count = k_msgq_num_used_get(&uart_detect_rx_queue);
    SCPI_ResultUInt32(context, count);
    return SCPI_RES_OK;
}

/* SENSe:UART:CLEar
 * Сбрасывает накопленный буфер (очистка перед началом теста).
 */
scpi_result_t cmd_sense_uart_clear(scpi_t *context)
{
     (void)context;

    // Сбрасываем очередь сообщений
    k_msgq_purge(&uart_detect_rx_queue);
    return SCPI_RES_OK;
}

/* SENSe:UART:BAUDrate <baud>
 * Динамическая установка скорости порта прослушивания.
 */
scpi_result_t cmd_sense_uart_baud(scpi_t *context)
{
    int32_t baud;
    if (!SCPI_ParamInt32(context, &baud, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (baud < 1200 || baud > 4000000) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

   // cfg.baudrate = (uint32_t)baud;

    return SCPI_RES_OK;
}

/* SENSe:UART:BAUDrate?
 * Запрос текущей установленной скорости.
 */
scpi_result_t cmd_sense_uart_baud_q(scpi_t *context)
{
//    struct uart_config cfg;

  //  SCPI_ResultUInt32(context, cfg.baudrate);
    return SCPI_RES_OK;
}

/* Дополнение в функцию сброса *RST */
void sense_uart_reset(void)
{
   // ring_buf_reset(&dut_rx_ringbuf);
}
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/sys/util.h>
#include "scpi/scpi.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include "system_define.h"
#include "can_fd.h"
#include "system_bus_model.h"

#define CAN_CH_COUNT 3

#define CAN_RX_MSGQ_DEPTH 32 // Буфер на 32 входящих кадра CAN FD

/* Устройства CAN FD из Devicetree (например, FDCAN1, FDCAN2, FDCAN3 на STM32H7) */

K_MSGQ_DEFINE(canfd1_rx_msgq, sizeof(system_can_message_t), CAN_RX_MSGQ_DEPTH, 4);
K_MSGQ_DEFINE(canfd2_rx_msgq, sizeof(system_can_message_t), CAN_RX_MSGQ_DEPTH, 4);
K_MSGQ_DEFINE(canfd3_rx_msgq, sizeof(system_can_message_t), CAN_RX_MSGQ_DEPTH, 4);


/* Локальное хранилище текущих настроенных скоростей для ответа на запрос */
/*static struct {
    uint32_t nominal_baud;
    uint32_t data_baud;
} can_cfg[CAN_CH_COUNT] = {
    {500000, 2000000},
    {500000, 2000000},
    {500000, 2000000}
};

/* Режимы работы */
/*static const scpi_choice_def_t can_mode_choices[] = {
    {"FD",       CAN_MODE_FD},
    {"LISTEN",   CAN_MODE_FD | CAN_MODE_LISTENONLY},
    {"LOOPback", CAN_MODE_FD | CAN_MODE_LOOPBACK},
    {"CLASSIC",  CAN_MODE_NORMAL},
    SCPI_CHOICE_LIST_END
};
*/


static size_t parse_space_separated_hex(const char *str, size_t str_len, uint8_t *out, size_t max_bytes)
{
    size_t count = 0;
    const char *p = str;
    const char *end = str + str_len;

    while (p < end && count < max_bytes) 
    {
        // Пропускаем ведущие пробелы и символы табуляции
        while (p < end && (*p == ' ' || *p == '\t')) {
            p++;
        }
        if (p >= end) {
            break;
        }

        // Считываем 1 байт в шестнадцатеричном виде
        char *next_ptr;
        unsigned long val = strtoul(p, &next_ptr, 16);

        // Если символ не был числом или значение больше 1 байта (0xFF)
        if (next_ptr == p || val > 0xFF) {
            return 0; // Ошибка синтаксиса
        }

        out[count++] = (uint8_t)val;
        p = next_ptr;
    }

    return count;
}
/* Парсинг длинных HEX-строк (до 64 байт / 128 символов) */
static size_t hex_to_bytes(const char *hex, uint8_t *out, size_t max_len)
{
    size_t hex_len = strlen(hex);
    size_t byte_count = hex_len / 2;
    if (byte_count > max_len) byte_count = max_len;

    for (size_t i = 0; i < byte_count; i++) {
        unsigned int byte_val;
        if (sscanf(&hex[i * 2], "%02x", &byte_val) != 1) {
            return 0;
        }
        out[i] = (uint8_t)byte_val;
    }
    return byte_count;
}



/* ========================================================================= */
/* 1. БИТРЕЙТ: CAN:BAUDrate <1..3>, <nominal>[, <data_phase>]                */
/* ========================================================================= */
scpi_result_t cmd_can_baud(scpi_t *context)
{
    /*int32_t ch, nominal_baud, data_baud;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    // 1. Номинальная скорость арбитража (например, 500000)
    if (!SCPI_ParamInt32(context, &nominal_baud, TRUE)) return SCPI_RES_ERR;
    if (nominal_baud < 20000 || nominal_baud > 1000000) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    // 2. Опциональная скорость фазы данных CAN FD (например, 2000000, 4000000, 5000000)
    bool has_data_baud = SCPI_ParamInt32(context, &data_baud, FALSE);
    if (has_data_baud) {
        if (data_baud < nominal_baud || data_baud > 8000000) {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }
    }

    const struct device *dev = can_devs[ch - 1];
    if (dev == NULL || !device_is_ready(dev)) {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR;
    }

    can_stop(dev);
    if (can_set_bitrate(dev, (uint32_t)nominal_baud) != 0) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    can_cfg[ch - 1].nominal_baud = (uint32_t)nominal_baud;

    if (has_data_baud) {
        if (can_set_bitrate_data(dev, (uint32_t)data_baud) != 0) {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
        can_cfg[ch - 1].data_baud = (uint32_t)data_baud;
    }

    can_start(dev);*/
    return SCPI_RES_OK;
}

// CAN:BAUDrate? <1..3> -> вернет "500000,2000000"
scpi_result_t cmd_can_baud_q(scpi_t *context)
{
    /*int32_t ch;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, can_cfg[ch - 1].nominal_baud);
    SCPI_ResultUInt32(context, can_cfg[ch - 1].data_baud);*/
    return SCPI_RES_OK;
}

/* ========================================================================= */
/* 2. РЕЖИМ РАБОТЫ: CAN:MODE <1..3>, <FD|LISTEN|LOOPBACK|CLASSIC>            */
/* ========================================================================= */
scpi_result_t cmd_can_mode(scpi_t *context)
{
    /*int32_t ch, mode;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamChoice(context, can_mode_choices, &mode, TRUE)) return SCPI_RES_ERR;

    const struct device *dev = can_devs[ch - 1];
    if (dev == NULL || !device_is_ready(dev)) {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR;
    }

    can_stop(dev);
    can_set_mode(dev, (can_mode_t)mode);
    can_start(dev);
*/
    return SCPI_RES_OK;
}

/* ========================================================================= */
/* 3. ВКЛ/ВЫКЛ: CAN[:STATe] <1..3>, <ON|OFF>                                 */
/* ========================================================================= */
scpi_result_t cmd_can_state(scpi_t *context)
{
  /*  int32_t ch;
    scpi_bool_t state;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamBool(context, &state, TRUE)) return SCPI_RES_ERR;

    const struct device *dev = can_devs[ch - 1];
    if (dev == NULL || !device_is_ready(dev)) {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR;
    }

    int err = state ? can_start(dev) : can_stop(dev);
    if (err != 0 && err != -EALREADY) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
*/
    return SCPI_RES_OK;
}

/* ========================================================================= */
/* 4. ПЕРЕДАЧА CAN FD: CAN:TRANsmit <1..3>, <id>, <hex_data>[, <brs>]        */
/* ========================================================================= */
scpi_result_t cmd_can_transmit(scpi_t *context)
{
    int32_t ch;
    uint32_t can_id;
    const char *data_str;
    size_t str_len;
    scpi_bool_t brs = TRUE; // По умолчанию BRS включен
    

    // 1. Канал (1..3)
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT)
    {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    // 2. CAN ID
    if (!SCPI_ParamUInt32(context, &can_id, TRUE)) return SCPI_RES_ERR;

// 3. Данные через пробел: "11 22 33 44 ..."    
    if (!SCPI_ParamCharacters(context, &data_str, &str_len, TRUE)) 
    {
        return SCPI_RES_ERR;                    
    }

     char raw_buf[256];
    if (str_len >= sizeof(raw_buf)) {
        SCPI_ErrorPush(context, SCPI_ERROR_TOO_MUCH_DATA); // -223
        return SCPI_RES_ERR;
    }
    memcpy(raw_buf, data_str, str_len);
    raw_buf[str_len] = '\0';
    system_can_message_t msg;
    // Разбираем строку байтов
    size_t byte_count = parse_space_separated_hex(raw_buf, str_len, msg.data, sizeof(msg.data));

   
   



    // Флаги CAN FD
    msg.flags = 0;
    
    if (can_id > 0x7FF) 
    {
        msg.flags |= CAN_FRAME_IDE; // 29-битный расширенный ID
    }

    msg.id = can_id;    
    msg.dlc = byte_count;

    if  (ch == 1)
    {
        SYSTEM_BUS_SET_P(CAN1_TX,&msg);
    }
    else if (ch == 2)
    {
        SYSTEM_BUS_SET_P(CAN2_TX,&msg);
    }
    else
    {
        SYSTEM_BUS_SET_P(CAN3_TX,&msg);
    }   
    return SCPI_RES_OK;
}




static struct k_msgq *const can_rx_queues[CAN_CH_COUNT] = {
    &canfd1_rx_msgq, &canfd2_rx_msgq, &canfd3_rx_msgq
};
/* ========================================================================= */
/* 5. ВЫЧИТКА КАДРА CAN FD: CAN:RECEive? <1..3>                              */
/* ========================================================================= */
scpi_result_t cmd_can_receive_q(scpi_t *context)
{
    int32_t ch;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) 
    return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    system_can_message_t _msg;
    if (k_msgq_get(can_rx_queues[ch - 1], &_msg, K_NO_WAIT) == 0) 
    {        
        char resp_buf[256];
        int offset = 0;
        // Печатаем CAN_ID и длину (DLC): "0x123, 8, "
        offset += snprintf(resp_buf + offset, sizeof(resp_buf) - offset, 
                           "0x%X, %u, ",_msg.id, (uint32_t)_msg.dlc);

        // Печатаем байты данных через пробел: "AA BB CC DD ..."
        for (size_t i = 0; i < _msg.dlc; i++) 
        {
            offset += snprintf(resp_buf + offset, sizeof(resp_buf) - offset,
                               (i == 0) ? "%02X" : " %02X", _msg.data[i]);

            // Защита от переполнения буфера
            if (offset >= (int)sizeof(resp_buf) - 2) 
            {
                break;
            }
        }

        SCPI_ResultCharacters(context, resp_buf, strlen(resp_buf));
    } else {
        SCPI_ResultCharacters(context, "NONE", 4);
    }

    return SCPI_RES_OK;
}

/* ========================================================================= */
/* 6. СЧЕТЧИК, ОЧИСТКА И СТАТУС                                              */
/* ========================================================================= */
scpi_result_t cmd_can_count_q(scpi_t *context)
{
    int32_t ch;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    uint32_t count = k_msgq_num_used_get(can_rx_queues[ch - 1]);
    SCPI_ResultUInt32(context, count);
    return SCPI_RES_OK;
}

scpi_result_t cmd_can_clear(scpi_t *context)
{
    int32_t ch;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    k_msgq_purge(can_rx_queues[ch - 1]);
    return SCPI_RES_OK;
}

scpi_result_t cmd_can_status_q(scpi_t *context)
{
    /*int32_t ch;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 1 || ch > CAN_CH_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    const struct device *dev = can_devs[ch - 1];
    enum can_state state;
    struct can_bus_err_cnt err_cnt;

    if (dev == NULL || !device_is_ready(dev)) {
        SCPI_ResultMnemonic(context, "OFFLINE");
        return SCPI_RES_OK;
    }

    if (can_get_state(dev, &state, &err_cnt) != 0) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    switch (state) {
        case CAN_STATE_ERROR_ACTIVE:  SCPI_ResultMnemonic(context, "ACTIVE"); break;
        case CAN_STATE_ERROR_WARNING: SCPI_ResultMnemonic(context, "WARNING"); break;
        case CAN_STATE_ERROR_PASSIVE: SCPI_ResultMnemonic(context, "PASSIVE"); break;
        case CAN_STATE_BUS_OFF:       SCPI_ResultMnemonic(context, "BUS_OFF"); break;
        case CAN_STATE_STOPPED:       SCPI_ResultMnemonic(context, "STOPPED"); break;
        default:                      SCPI_ResultMnemonic(context, "UNKNOWN"); break;
    }*/
    return SCPI_RES_OK;
}



/* Очереди сообщений под кадры CAN FD (размер структуры автоматически 64 байта при CONFIG_CAN_FD_MODE=y) */

static void _can1_rx_listener_cb(const struct zbus_channel *_chan)
{
    system_can_message_t _msg;
    SYSTEM_BUS_GET_P(CAN1_RX,&_msg);    
    k_msgq_put(&canfd1_rx_msgq,&_msg,K_NO_WAIT);
}

static void _can2_rx_listener_cb(const struct zbus_channel *_chan)
{
    system_can_message_t _msg;
    SYSTEM_BUS_GET_P(CAN2_RX,&_msg);    
    k_msgq_put(&canfd2_rx_msgq,&_msg,K_NO_WAIT);
}

static void _can3_rx_listener_cb(const struct zbus_channel *_chan)
{
    system_can_message_t  _msg;
    SYSTEM_BUS_GET_P(CAN3_RX,&_msg);        
    k_msgq_put(&canfd3_rx_msgq,&_msg,K_NO_WAIT);
}



ZBUS_LISTENER_DEFINE(can1_rx_listener, _can1_rx_listener_cb);
ZBUS_LISTENER_DEFINE(can2_rx_listener, _can2_rx_listener_cb);
ZBUS_LISTENER_DEFINE(can3_rx_listener, _can3_rx_listener_cb);

static int _spci_fd_can_init(void)
{

    bus_listener_attach(&can1_rx_listener,CAN1_RX);
    bus_listener_attach(&can2_rx_listener,CAN2_RX);
    bus_listener_attach(&can3_rx_listener,CAN3_RX);

    return 0;
}

SYS_INIT(_spci_fd_can_init, SPCI_CAN_INIT_LEVEL, SPCI_CAN_INIT_PRIOR);
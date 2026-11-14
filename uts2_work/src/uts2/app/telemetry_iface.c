#include <stdbool.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>


#include "system_bus_model.h"
#include "telemetry.h"

LOG_MODULE_REGISTER(telemetry_iface, LOG_LEVEL_INF);



#define TELEMETRY_THREAD_STACK_SIZE   4000
#define TELEMETRY_THREAD_PRIORITY     0    

static void     _telemetry_thread_entry(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(telemetry_thread_id, 
                TELEMETRY_THREAD_STACK_SIZE,
                _telemetry_thread_entry, 
                NULL, NULL, NULL,
                TELEMETRY_THREAD_PRIORITY, 
                0, 
                0);


#define SLEEP_TIME_MS   1


static const struct device *const telemetry_uart = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

void send_can_message_binary(const struct device *uart_dev, uint8_t can_num, const system_can_message_t *msg) {
    if (!uart_dev || !device_is_ready(uart_dev) || !msg) {
        return;
    }

    struct can_rx_bin_packet pkt;
    pkt.magic = CAN_MAGIC_BYTE;
    pkt.can_num = can_num;
    pkt.id = msg->id;
    pkt.dlc = msg->dlc;

    // Определяем реальную длину данных в байтах (если dlc хранит код длины CAN-FD или байты)
    // Если у тебя в dlc уже лежит реальное количество байт (0-64), то просто:
    size_t bytes_len = msg->dlc;
    
    // Ели же это стандартный CAN DLC (где 9-15 это кодированные длины для CAN-FD), 
    // то лучше использовать функцию Zephyr: can_dlc_to_bytes(msg->dlc)
    // size_t bytes_len = can_dlc_to_bytes(msg->dlc);

    if (bytes_len > 64) {
        bytes_len = 64;
    }

    memset(pkt.data, 0, sizeof(pkt.data));
    memcpy(pkt.data, msg->data, bytes_len);

    // Размер пакета: заголовок (7 байт) + полезная нагрузка
    size_t total_size = 7 + bytes_len;

    // Отправляем байты в UART
    const uint8_t *ptr = (const uint8_t *)&pkt;
    for (size_t i = 0; i < total_size; i++) {
        uart_poll_out(uart_dev, ptr[i]);
    }
}

  struct telemetry_packet pkt;

K_MSGQ_DEFINE(telemetry_queue, sizeof(struct telemetry_packet), 4, 4);

void send_binary_telemetry(const struct device *uart_dev) {
  
    pkt.magic = TELEMETRY_MAGIC_BYTE;

    pkt.timestamp_ms = k_uptime_get_32();

    // Заполняем данными из твоего системного буфера
    pkt.rf_power   = server_get_float(RF_POWER);
    pkt.ntc        = server_get_float(NTC);
    pkt.ext_vsense = server_get_float(EXT_VSENSE);
    pkt.brd_detect = server_get_float(BRD_DETECT);

    pkt.temp_gnd   = server_get_float(I2C1_TEMP1);
    pkt.temp_vdd   = server_get_float(I2C1_TEMP2);
    pkt.light_gnd  = server_get_float(I2C1_LUX1);
    pkt.light_vdd  = server_get_float(I2C1_LUX2);

    // AO 1-18
    pkt.ao[0]  = server_get_float(AIN_AO1);
    pkt.ao[1]  = server_get_float(AIN_AO2);
    pkt.ao[2]  = server_get_float(AIN_AO3);
    pkt.ao[3]  = server_get_float(AIN_AO4);
    pkt.ao[4]  = server_get_float(AIN_AO5);
    pkt.ao[5]  = server_get_float(AIN_AO6);

    pkt.ao[6]  = server_get_float(AIN_AO7);
    pkt.ao[7]  = server_get_float(AIN_AO8);
    pkt.ao[8]  = server_get_float(AIN_AO9);
    pkt.ao[9]  = server_get_float(AIN_AO10);
    pkt.ao[10] = server_get_float(AIN_AO11);
    pkt.ao[11] = server_get_float(AIN_AO12);

    pkt.ao[12] = server_get_float(AIN_AO13);
    pkt.ao[13] = server_get_float(AIN_AO14);
    pkt.ao[14] = server_get_float(AIN_AO15);
    pkt.ao[15] = server_get_float(AIN_AO16);
    pkt.ao[16] = server_get_float(AIN_AO17);
    pkt.ao[17] = server_get_float(AIN_AO18);

    // AIN_VSense 1-6
    pkt.vsense[0] = server_get_float(AIN_AVsense1);
    pkt.vsense[1] = server_get_float(AIN_AVsense2);
    pkt.vsense[2] = server_get_float(AIN_AVsense3);
    pkt.vsense[3] = server_get_float(AIN_AVsense4);
    pkt.vsense[4] = server_get_float(AIN_AVsense5);
    pkt.vsense[5] = server_get_float(AIN_AVsense6);

    // Env (8 шт)
    pkt.env[0] = server_get_float(ENV_P3V3);
    pkt.env[1] = server_get_float(ENV_P5V0);
    pkt.env[2] = server_get_float(ENV_VIN);
    pkt.env[3] = server_get_float(ENV_VDOUT1);
    pkt.env[4] = server_get_float(ENV_P40V);
    pkt.env[5] = server_get_float(ENV_USB);
    pkt.env[6] = server_get_float(ENV_VDOUT2);
    pkt.env[7] = server_get_float(ENV_VDOUT3);

    // Current sensors (24 шт)
    pkt.current_sensors[1]  = server_get_float(SENS_BRD_LOW_CURRENT);
    pkt.current_sensors[0]  = server_get_float(SENS_BRD_LOW_VOLTAGE);
    pkt.current_sensors[2]  = server_get_float(SENS_I2C_POWER_BRD_LOW);

    pkt.current_sensors[4]  = server_get_float(SENS_BRD_HIGH_CURRENT);
    pkt.current_sensors[3]  = server_get_float(SENS_BRD_HIGH_VOLTAGE);
    pkt.current_sensors[5]  = server_get_float(SENS_I2C_POWER_BRD_HIGH);

    pkt.current_sensors[7]  = server_get_float(SENS_VDUT2_CURRENT);
    pkt.current_sensors[6]  = server_get_float(SENS_VDUT2_VOLTAGE);
    pkt.current_sensors[8]  = server_get_float(SENS_I2C_POWER_VDUT2);

    pkt.current_sensors[10] = server_get_float(SENS_VDUT3_CURRENT);
    pkt.current_sensors[9]  = server_get_float(SENS_VDUT3_VOLTAGE);
    pkt.current_sensors[11] = server_get_float(SENS_I2C_POWER_VDUT3);

    pkt.current_sensors[13] = server_get_float(SENS_VIN_CURRENT);
    pkt.current_sensors[12] = server_get_float(SENS_VIN_VOLTAGE);
    pkt.current_sensors[14] = server_get_float(SENS_I2C_POWER_VIN);

    pkt.current_sensors[16] = server_get_float(SENS_DCDC_3_3_CURRENT);
    pkt.current_sensors[15] = server_get_float(SENS_DCDC_3_3_VOLTAGE);
    pkt.current_sensors[17] = server_get_float(SENS_I2C_POWER_DCDC_3_3);

    pkt.current_sensors[19] = server_get_float(SENS_VDOUT_PWR_CURRENT);
    pkt.current_sensors[18] = server_get_float(SENS_VDOUT_PWR_VOLTAGE);
    pkt.current_sensors[20] = server_get_float(SENS_I2C_POWER_VDOUT_PWR);

    pkt.current_sensors[22] = server_get_float(SENS_VD5_CURRENT);
    pkt.current_sensors[21] = server_get_float(SENS_VD5_VOLTAGE);
    pkt.current_sensors[23] = server_get_float(SENS_I2C_POWER_VD5);

    // LIN voltages (4 шт)
    pkt.lin_voltages[0] = server_get_float(LIN_AO1);
    pkt.lin_voltages[1] = server_get_float(LIN_AO2);
    pkt.lin_voltages[2] = server_get_float(LIN_AO3);
    pkt.lin_voltages[3] = server_get_float(LIN_AO4);

    k_msgq_put(&telemetry_queue,&pkt,K_NO_WAIT);

}


void send_uart_stream_to_dashboard(uint8_t channel, const uint8_t *data, size_t size) {
    if (!telemetry_uart || !device_is_ready(telemetry_uart) || !data || size == 0) {
        return;
    }
    
    // Проверяем диапазон каналов (1-4)
    if (channel < 1 || channel > 4) {
        return;
    }

    // Ограничиваем размер одной порции до 255 байт (так как длина передается в 1 байт uint8_t)
    if (size > 255) {
        size = 255;
    }

    // Формируем байт маркера канала: 0xC1 для канала 1, 0xC2 для 2, 0xC3 для 3, 0xC4 для 4
    uint8_t magic_channel = 0xC0 | (channel & 0x0F);

    // 1. Отправляем маркер канала
    uart_poll_out(telemetry_uart, magic_channel);
    
    // 2. Отправляем размер данных
    uart_poll_out(telemetry_uart, (uint8_t)size);

    // 3. Отправляем сами данные байт за байтом
    for (size_t i = 0; i < size; i++) {
        uart_poll_out(telemetry_uart, data[i]);
    }
}

typedef struct 
{
    system_can_message_t  msg;
    uint8_t               iface_id;
} can_queue_message_t;

typedef struct 
{
    uint8_t               msg;
    uint8_t               iface_id;
} lin_queue_message_t;




K_MSGQ_DEFINE(all_lin_rx_queue, sizeof(lin_queue_message_t), 10, 4);
K_MSGQ_DEFINE(all_can_rx_queue, sizeof(can_queue_message_t), 10, 4);

static void _lin1_rx_listener_cb(const struct zbus_channel *_chan)
{
    uint32_t _temp;
    SYSTEM_BUS_GET(LIN1_RX,&_temp);
    lin_queue_message_t _msg;
    _msg.msg = (uint8_t) _temp;
    _msg.iface_id = 1;
    k_msgq_put(&all_lin_rx_queue,&_msg,K_NO_WAIT);
}

static void _lin2_rx_listener_cb(const struct zbus_channel *_chan)
{
    uint32_t _temp;
    SYSTEM_BUS_GET(LIN2_RX,&_temp);
    lin_queue_message_t _msg;
    _msg.msg = (uint8_t) _temp;
    _msg.iface_id = 2;
    k_msgq_put(&all_lin_rx_queue,&_msg,K_NO_WAIT);
}

static void _lin3_rx_listener_cb(const struct zbus_channel *_chan)
{
    uint32_t _temp;
    SYSTEM_BUS_GET(LIN3_RX,&_temp);
    lin_queue_message_t _msg;
    _msg.msg = (uint8_t) _temp;
    _msg.iface_id = 3;
    k_msgq_put(&all_lin_rx_queue,&_msg,K_NO_WAIT);
}

static void _lin4_rx_listener_cb(const struct zbus_channel *_chan)
{
    uint32_t _temp;
    SYSTEM_BUS_GET(LIN4_RX,&_temp);
    lin_queue_message_t _msg;
    _msg.msg =  (uint8_t)_temp;
    _msg.iface_id = 4;
    k_msgq_put(&all_lin_rx_queue,&_msg,K_NO_WAIT);
}




static void _can1_rx_listener_cb(const struct zbus_channel *_chan)
{
    system_can_message_t _temp;
    SYSTEM_BUS_GET_P(CAN1_RX,&_temp);
    can_queue_message_t _msg;
    _msg.msg =  _temp;
    _msg.iface_id = 1;
    k_msgq_put(&all_can_rx_queue,&_msg,K_NO_WAIT);
}

static void _can2_rx_listener_cb(const struct zbus_channel *_chan)
{
    system_can_message_t _temp;
    SYSTEM_BUS_GET_P(CAN2_RX,&_temp);
    can_queue_message_t _msg;
    _msg.msg =  _temp;
    _msg.iface_id = 2;
    k_msgq_put(&all_can_rx_queue,&_msg,K_NO_WAIT);
}

static void _can3_rx_listener_cb(const struct zbus_channel *_chan)
{
    system_can_message_t _temp;
    SYSTEM_BUS_GET_P(CAN3_RX,&_temp);
    can_queue_message_t _msg;
    _msg.msg =  _temp;
    _msg.iface_id = 3;
    k_msgq_put(&all_can_rx_queue,&_msg,K_NO_WAIT);
}



static void telemetry_timer_expiry(struct k_timer *timer)
{

    send_binary_telemetry(telemetry_uart);  
}

// Инициализация периодического таймера на 1 кГц (1 мс)
static K_TIMER_DEFINE(telemetry_timer, telemetry_timer_expiry, NULL);

ZBUS_LISTENER_DEFINE(can1_rx_listener, _can1_rx_listener_cb);
ZBUS_LISTENER_DEFINE(can2_rx_listener, _can2_rx_listener_cb);
ZBUS_LISTENER_DEFINE(can3_rx_listener, _can3_rx_listener_cb);
ZBUS_LISTENER_DEFINE(lin1_rx_listener, _lin1_rx_listener_cb);
ZBUS_LISTENER_DEFINE(lin2_rx_listener, _lin2_rx_listener_cb);
ZBUS_LISTENER_DEFINE(lin3_rx_listener, _lin3_rx_listener_cb);
ZBUS_LISTENER_DEFINE(lin4_rx_listener, _lin4_rx_listener_cb);

#define PCF_NODE DT_NODELABEL(pcf_test)

static void     _telemetry_thread_entry(void *p1, void *p2, void *p3)
{

    LOG_INF("SYSTETM START");	
    struct telemetry_packet _packet_to_send;

    bus_listener_attach(&can1_rx_listener,CAN1_RX);
    bus_listener_attach(&can2_rx_listener,CAN2_RX);
    bus_listener_attach(&can3_rx_listener,CAN3_RX);
    bus_listener_attach(&lin1_rx_listener,LIN1_RX);
    bus_listener_attach(&lin2_rx_listener,LIN2_RX);
    bus_listener_attach(&lin3_rx_listener,LIN3_RX);
    bus_listener_attach(&lin4_rx_listener,LIN4_RX);

    k_timer_start(&telemetry_timer, K_MSEC(1), K_MSEC(1));
    while (1) 
	{          
        can_queue_message_t rx_msg;       
        while (k_msgq_get(&all_can_rx_queue, &rx_msg, K_NO_WAIT) == 0) 
        {
            send_can_message_binary(telemetry_uart, rx_msg.iface_id, &rx_msg.msg);        
        }
  
        lin_queue_message_t lin_msg;
        while (k_msgq_get(&all_lin_rx_queue, &lin_msg, K_NO_WAIT) == 0) 
        {
            uint8_t data;
            data = lin_msg.msg;
            send_uart_stream_to_dashboard(lin_msg.iface_id,&data,1);            
        }
        if (k_msgq_get(&telemetry_queue,&_packet_to_send, K_FOREVER)==0)
        {            
            uint8_t *ptr = (uint8_t *)&_packet_to_send;
            for (size_t i = 0; i < sizeof(_packet_to_send); i++) 
            {
                uart_poll_out(telemetry_uart , ptr[i]);
            }
        }      
    }
}
                
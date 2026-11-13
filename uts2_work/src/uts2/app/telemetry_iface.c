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
#define TELEMETRY_THREAD_PRIORITY     5    

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

    float _temp;
    // Заполняем данными из твоего системного буфера
    SYSTEM_BUS_GET(RF_POWER, &_temp);   pkt.rf_power = _temp;
    SYSTEM_BUS_GET(NTC, &_temp);        pkt.ntc = _temp;
    SYSTEM_BUS_GET(EXT_VSENSE, &_temp); pkt.ext_vsense = _temp;
    SYSTEM_BUS_GET(BRD_DETECT, &_temp); pkt.brd_detect =  _temp;

    SYSTEM_BUS_GET(I2C1_TEMP1, &_temp); pkt.temp_gnd = _temp;
    SYSTEM_BUS_GET(I2C1_TEMP2, &_temp); pkt.temp_vdd = _temp;
    SYSTEM_BUS_GET(I2C1_LUX1, &_temp); pkt.light_gnd = _temp;
    SYSTEM_BUS_GET(I2C1_LUX2, &_temp); pkt.light_vdd = _temp;

   // AO 1-18
    SYSTEM_BUS_GET(AIN_AO1, &_temp);   pkt.ao[0] = _temp;    
    SYSTEM_BUS_GET(AIN_AO2, &_temp);   pkt.ao[1] = _temp;    
    SYSTEM_BUS_GET(AIN_AO3, &_temp);   pkt.ao[2] = _temp;
    SYSTEM_BUS_GET(AIN_AO4, &_temp);   pkt.ao[3] = _temp;
    SYSTEM_BUS_GET(AIN_AO5, &_temp);   pkt.ao[4] = _temp;
    SYSTEM_BUS_GET(AIN_AO6, &_temp);   pkt.ao[5] = _temp;
    
    SYSTEM_BUS_GET(AIN_AO7, &_temp);   pkt.ao[6] = _temp;
    SYSTEM_BUS_GET(AIN_AO8, &_temp);   pkt.ao[7] = _temp;
    SYSTEM_BUS_GET(AIN_AO9, &_temp);   pkt.ao[8] = _temp;
    SYSTEM_BUS_GET(AIN_AO10, &_temp);  pkt.ao[9] = _temp;
    SYSTEM_BUS_GET(AIN_AO11, &_temp);  pkt.ao[10] = _temp;
    SYSTEM_BUS_GET(AIN_AO12, &_temp);  pkt.ao[11] = _temp;
    
    SYSTEM_BUS_GET(AIN_AO13, &_temp);  pkt.ao[12] = _temp;
    SYSTEM_BUS_GET(AIN_AO14, &_temp);  pkt.ao[13] = _temp;
    SYSTEM_BUS_GET(AIN_AO15, &_temp);  pkt.ao[14] = _temp;
    SYSTEM_BUS_GET(AIN_AO16, &_temp);  pkt.ao[15] = _temp;
    SYSTEM_BUS_GET(AIN_AO17, &_temp);  pkt.ao[16] = _temp;
    SYSTEM_BUS_GET(AIN_AO18, &_temp);  pkt.ao[17] = _temp;

    // AIN_VSense 1-6
    SYSTEM_BUS_GET(AIN_AVsense1, &_temp); pkt.vsense[0] = _temp;
    SYSTEM_BUS_GET(AIN_AVsense2, &_temp); pkt.vsense[1] = _temp;
    SYSTEM_BUS_GET(AIN_AVsense3, &_temp); pkt.vsense[2] = _temp;
    SYSTEM_BUS_GET(AIN_AVsense4, &_temp); pkt.vsense[3] = _temp;
    SYSTEM_BUS_GET(AIN_AVsense5, &_temp); pkt.vsense[4] = _temp;
    SYSTEM_BUS_GET(AIN_AVsense6, &_temp); pkt.vsense[5] = _temp;

    // Env (8 шт)
    SYSTEM_BUS_GET(ENV_P3V3,  &_temp);    pkt.env[0] = _temp;
    SYSTEM_BUS_GET(ENV_P5V0,  &_temp);    pkt.env[1] = _temp;
    SYSTEM_BUS_GET(ENV_VIN,   &_temp);    pkt.env[2] = _temp;
    SYSTEM_BUS_GET(ENV_VDOUT1, &_temp);   pkt.env[3] = _temp;
    SYSTEM_BUS_GET(ENV_P40V,  &_temp);    pkt.env[4] = _temp;
    SYSTEM_BUS_GET(ENV_USB,   &_temp);    pkt.env[5] = _temp;
    SYSTEM_BUS_GET(ENV_VDOUT2, &_temp);   pkt.env[6] = _temp;
    SYSTEM_BUS_GET(ENV_VDOUT3, &_temp);   pkt.env[7] = _temp;

    // Current sensors (24 шт)
    SYSTEM_BUS_GET(SENS_BRD_LOW_CURRENT,     &_temp); pkt.current_sensors[1] = _temp;
    SYSTEM_BUS_GET(SENS_BRD_LOW_VOLTAGE,    &_temp);  pkt.current_sensors[0] = _temp;
    SYSTEM_BUS_GET(SENS_I2C_POWER_BRD_LOW,   &_temp); pkt.current_sensors[2] = _temp;

    SYSTEM_BUS_GET(SENS_BRD_HIGH_CURRENT,   &_temp);  pkt.current_sensors[4] = _temp;
    SYSTEM_BUS_GET(SENS_BRD_HIGH_VOLTAGE,   &_temp);  pkt.current_sensors[3] = _temp;
    SYSTEM_BUS_GET(SENS_I2C_POWER_BRD_HIGH, &_temp);  pkt.current_sensors[5] = _temp;

    SYSTEM_BUS_GET(SENS_VDUT2_CURRENT,      &_temp);  pkt.current_sensors[7] = _temp;
    SYSTEM_BUS_GET(SENS_VDUT2_VOLTAGE,     &_temp);   pkt.current_sensors[6] = _temp;
    SYSTEM_BUS_GET(SENS_I2C_POWER_VDUT2,   &_temp);   pkt.current_sensors[8] = _temp;

    SYSTEM_BUS_GET(SENS_VDUT3_CURRENT,     &_temp);   pkt.current_sensors[10] = _temp;
    SYSTEM_BUS_GET(SENS_VDUT3_VOLTAGE,     &_temp);   pkt.current_sensors[9] = _temp;
    SYSTEM_BUS_GET(SENS_I2C_POWER_VDUT3,   &_temp);   pkt.current_sensors[11] = _temp;

    SYSTEM_BUS_GET(SENS_VIN_CURRENT,       &_temp);   pkt.current_sensors[13] = _temp;
    SYSTEM_BUS_GET(SENS_VIN_VOLTAGE,       &_temp);   pkt.current_sensors[12] = _temp;
    SYSTEM_BUS_GET(SENS_I2C_POWER_VIN,     &_temp);   pkt.current_sensors[14] = _temp;

    SYSTEM_BUS_GET(SENS_DCDC_3_3_CURRENT,   &_temp);  pkt.current_sensors[16] = _temp;
    SYSTEM_BUS_GET(SENS_DCDC_3_3_VOLTAGE,  &_temp);  pkt.current_sensors[15] = _temp;
    SYSTEM_BUS_GET(SENS_I2C_POWER_DCDC_3_3, &_temp);  pkt.current_sensors[17] = _temp;

    SYSTEM_BUS_GET(SENS_VDOUT_PWR_CURRENT, &_temp);   pkt.current_sensors[19] = _temp;
    SYSTEM_BUS_GET(SENS_VDOUT_PWR_VOLTAGE, &_temp);   pkt.current_sensors[18] = _temp;
    SYSTEM_BUS_GET(SENS_I2C_POWER_VDOUT_PWR, &_temp); pkt.current_sensors[20] = _temp;

    SYSTEM_BUS_GET(SENS_VD5_CURRENT,       &_temp);   pkt.current_sensors[22] = _temp;
    SYSTEM_BUS_GET(SENS_VD5_VOLTAGE,       &_temp);   pkt.current_sensors[21] = _temp;                    
    SYSTEM_BUS_GET(SENS_I2C_POWER_VD5,     &_temp);   pkt.current_sensors[23] = _temp;

    // LIN voltages (4 шт)
    SYSTEM_BUS_GET(LIN_AO1, &_temp); pkt.lin_voltages[0] = _temp;
    SYSTEM_BUS_GET(LIN_AO2, &_temp); pkt.lin_voltages[1] = _temp;
    SYSTEM_BUS_GET(LIN_AO3, &_temp); pkt.lin_voltages[2] = _temp;
    SYSTEM_BUS_GET(LIN_AO4, &_temp); pkt.lin_voltages[3] = _temp;

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
                
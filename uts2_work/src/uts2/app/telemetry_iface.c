#include <stdbool.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>


#include "system_bus_model.h"
#include "telemetry.h"
#include "system_define.h"

LOG_MODULE_REGISTER(telemetry_iface, LOG_LEVEL_INF);

#define TELEMETRY_THREAD_STACK_SIZE   4000

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



struct telemetry_packet pkt __attribute__((aligned(4)));

K_MSGQ_DEFINE(telemetry_queue, sizeof(struct telemetry_packet), 4, 4);

void send_binary_telemetry(const struct device *uart_dev) {
  
    pkt.magic = TELEMETRY_MAGIC_BYTE;

    pkt.timestamp_ms = k_uptime_get_32();

    server_get_real_fast(RF_POWER, &pkt.rf_power);
    server_get_real_fast(NTC, &pkt.ntc);
    server_get_real_fast(EXT_VSENSE, &pkt.ext_vsense);
    server_get_real_fast(BRD_DETECT, &pkt.brd_detect);

    server_get_real_fast(I2C1_TEMP1, &pkt.temp_gnd);
    server_get_real_fast(I2C1_TEMP2, &pkt.temp_vdd);
    server_get_real_fast(I2C1_LUX1, &pkt.light_gnd);
    server_get_real_fast(I2C1_LUX2, &pkt.light_vdd);
  
    // AO 1-6, 7-12, 13-18
    server_get_real_fast(AIN_AO1, &pkt.ao[0]);
    server_get_real_fast(AIN_AO2, &pkt.ao[1]);
    server_get_real_fast(AIN_AO3, &pkt.ao[2]);
    server_get_real_fast(AIN_AO4, &pkt.ao[3]);
    server_get_real_fast(AIN_AO5, &pkt.ao[4]);
    server_get_real_fast(AIN_AO6, &pkt.ao[5]);

    server_get_real_fast(AIN_AO7, &pkt.ao[6]);
    server_get_real_fast(AIN_AO8, &pkt.ao[7]);
    server_get_real_fast(AIN_AO9, &pkt.ao[8]);
    server_get_real_fast(AIN_AO10, &pkt.ao[9]);
    server_get_real_fast(AIN_AO11, &pkt.ao[10]);
    server_get_real_fast(AIN_AO12, &pkt.ao[11]);

    server_get_real_fast(AIN_AO13, &pkt.ao[12]);
    server_get_real_fast(AIN_AO14, &pkt.ao[13]);
    server_get_real_fast(AIN_AO15, &pkt.ao[14]);
    server_get_real_fast(AIN_AO16, &pkt.ao[15]);
    server_get_real_fast(AIN_AO17, &pkt.ao[16]);
    server_get_real_fast(AIN_AO18, &pkt.ao[17]);

    server_get_real_fast(AIN_AVsense1, &pkt.vsense[0]);
    server_get_real_fast(AIN_AVsense2, &pkt.vsense[1]);
    server_get_real_fast(AIN_AVsense3, &pkt.vsense[2]);
    server_get_real_fast(AIN_AVsense4, &pkt.vsense[3]);
    server_get_real_fast(AIN_AVsense5, &pkt.vsense[4]);
    server_get_real_fast(AIN_AVsense6, &pkt.vsense[5]);
    server_get_real_fast(ENV_P3V3,  &pkt.env[0]);
    server_get_real_fast(ENV_P5V0,  &pkt.env[1]);
    server_get_real_fast(ENV_VIN,   &pkt.env[2]);
    server_get_real_fast(ENV_VDOUT1, &pkt.env[3]);
    server_get_real_fast(ENV_P40V,  &pkt.env[4]);
    server_get_real_fast(ENV_USB,   &pkt.env[5]);
    server_get_real_fast(ENV_VDOUT2, &pkt.env[6]);
    server_get_real_fast(ENV_VDOUT3, &pkt.env[7]);


    server_get_real_fast(SENS_BRD_LOW_CURRENT,   &pkt.current_sensors[1]);
    server_get_real_fast(SENS_BRD_LOW_VOLTAGE,   &pkt.current_sensors[0]);
    server_get_real_fast(SENS_I2C_POWER_BRD_LOW, &pkt.current_sensors[2]);

    server_get_real_fast(SENS_BRD_HIGH_CURRENT,   &pkt.current_sensors[4]);
    server_get_real_fast(SENS_BRD_HIGH_VOLTAGE,   &pkt.current_sensors[3]);
    server_get_real_fast(SENS_I2C_POWER_BRD_HIGH, &pkt.current_sensors[5]);

    server_get_real_fast(SENS_VDUT2_CURRENT,     &pkt.current_sensors[7]);
    server_get_real_fast(SENS_VDUT2_VOLTAGE,     &pkt.current_sensors[6]);
    server_get_real_fast(SENS_I2C_POWER_VDUT2,   &pkt.current_sensors[8]);

    server_get_real_fast(SENS_VDUT3_CURRENT,    &pkt.current_sensors[10]);
    server_get_real_fast(SENS_VDUT3_VOLTAGE,    &pkt.current_sensors[9]);
    server_get_real_fast(SENS_I2C_POWER_VDUT3,  &pkt.current_sensors[11]);

    server_get_real_fast(SENS_VIN_CURRENT,     &pkt.current_sensors[13]);
    server_get_real_fast(SENS_VIN_VOLTAGE,     &pkt.current_sensors[12]);
    server_get_real_fast(SENS_I2C_POWER_VIN,   &pkt.current_sensors[14]);

    server_get_real_fast(SENS_DCDC_3_3_CURRENT,   &pkt.current_sensors[16]);
    server_get_real_fast(SENS_DCDC_3_3_VOLTAGE,   &pkt.current_sensors[15]);
    server_get_real_fast(SENS_I2C_POWER_DCDC_3_3, &pkt.current_sensors[17]);

    server_get_real_fast(SENS_VDOUT_PWR_CURRENT, &pkt.current_sensors[19]);
    server_get_real_fast(SENS_VDOUT_PWR_VOLTAGE, &pkt.current_sensors[18]);
    server_get_real_fast(SENS_I2C_POWER_VDOUT_PWR, &pkt.current_sensors[20]);

    server_get_real_fast(SENS_VD5_CURRENT,       &pkt.current_sensors[22]);
    server_get_real_fast(SENS_VD5_VOLTAGE,       &pkt.current_sensors[21]);                    
    server_get_real_fast(SENS_I2C_POWER_VD5,     &pkt.current_sensors[23]);

    server_get_real_fast(LIN_AO1,       &pkt.lin_voltages[0]);
    server_get_real_fast(LIN_AO2,       &pkt.lin_voltages[1]);
    server_get_real_fast(LIN_AO3,       &pkt.lin_voltages[2]);
    server_get_real_fast(LIN_AO4,       &pkt.lin_voltages[3]);

    pkt.crc32 = crc32_ieee((const uint8_t *)&pkt, sizeof(struct telemetry_packet) - sizeof(uint32_t));

    k_msgq_put(&telemetry_queue,&pkt,K_NO_WAIT);

}


static void telemetry_timer_expiry(struct k_timer *timer)
{
    send_binary_telemetry(telemetry_uart);  
}

// Инициализация периодического таймера на 1 кГц (1 мс)
static K_TIMER_DEFINE(telemetry_timer, telemetry_timer_expiry, NULL);




static void     _telemetry_thread_entry(void *p1, void *p2, void *p3)
{

    LOG_INF("SYSTETM START");	
    struct telemetry_packet _packet_to_send;

   
    k_timer_start(&telemetry_timer, K_MSEC(1), K_MSEC(1));

    while (1) 
	{                   
        if (k_msgq_get(&telemetry_queue,&_packet_to_send, K_FOREVER)==0)
        {            
           const uint8_t *ptr = (const uint8_t *)&_packet_to_send;
            size_t rem = sizeof(_packet_to_send);
            int retry_cnt = 0;

            while (rem > 0) 
            {
                int sent = uart_fifo_fill(telemetry_uart, ptr, rem);
                if (sent > 0) {
                    ptr += sent;
                    rem -= (size_t)sent;
                    retry_cnt = 0;
                } 
                else 
                {
                    // Буфер USB временно полон — ждём 100 мкс, пока уйдёт USB IN фрейм
                    retry_cnt++;
                    if (retry_cnt > 200) 
                    {
                        // Если ПК вообще закрыл порт (> 20 мс простоя) — сбрасываем, чтобы не висеть
                        break;
                    }
                    k_usleep(100);
                }
            }
        }      
    }
}
                
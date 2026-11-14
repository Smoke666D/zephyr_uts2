#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "sense.h"
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"
#include "spci_helpers.h"

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

#define TEMP_SENS_COUNT 2

scpi_result_t cmd_sense_temp_q(scpi_t *context)
{
    int32_t channel;
    float temp_val;
    bool _error_bit;
    // 1. Запрос конкретного датчика: SENS:TEMP? 1
    if (SCPI_ParamInt32(context, &channel, FALSE)) 
    {
        if (channel < 1 || channel > TEMP_SENS_COUNT) {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // -222
            return SCPI_RES_ERR;
        }
     
        SERVER_CH_GET_BIT(I2C_SENS_ERROR_CH, channel == 1 ? TMP112_MU_ERROR : TMP112_IU_ERROR,&_error_bit);
        if (_error_bit)
         {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); // -200 (сбой шины датчика)
            return SCPI_RES_ERR;
        }
        SYSTEM_BUS_GET(I2C1_TEMP1 + channel - 1,&temp_val);
        // Возвращаем число с плавающей точкой в градусах Цельсия
        SCPI_ResultFloat(context, temp_val);
    }
    // 2. Запрос всех датчиков без параметров: SENS:TEMP? -> "24.5,28.1"
    else 
    {
        for (int ch = 1; ch <= TEMP_SENS_COUNT; ch++) 
        {
            SERVER_CH_GET_BIT(I2C_SENS_ERROR_CH, channel == 1 ? TMP112_MU_ERROR : TMP112_IU_ERROR,&_error_bit);
            if (_error_bit)
            {
                SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); // -200 (сбой шины датчика)
                return SCPI_RES_ERR;
            }
            SYSTEM_BUS_GET(I2C1_TEMP1 + ch - 1,&temp_val);            
            SCPI_ResultFloat(context, temp_val);
        }
    }

    return SCPI_RES_OK;
}

#define LIGHT_SENS_COUNT 2

scpi_result_t cmd_sense_light_q(scpi_t *context)
{
    int32_t channel;
    float lux_val;
    bool _error_bit;
    // 1. Запрос конкретного датчика: SENS:LIG? 1
    if (SCPI_ParamInt32(context, &channel, FALSE))
     {
        if (channel < 1 || channel > LIGHT_SENS_COUNT) {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // -222
            return SCPI_RES_ERR;
        }

        SERVER_CH_GET_BIT(I2C_SENS_ERROR_CH, channel == 1 ? BH1750_1_IU_ERROR : BH1750_2_IU_ERROR,&_error_bit);
        if (_error_bit)
         {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); // -200 (сбой шины датчика)
            return SCPI_RES_ERR;
        }
        SYSTEM_BUS_GET(I2C1_LUX1 + channel - 1,&lux_val);

        // Возвращаем результат в формате float
        SCPI_ResultFloat(context, lux_val);
    }
    // 2. Запрос обоих датчиков: SENS:LIG? -> "350.5,420.0"
    else {
        for (int ch = 1; ch <= LIGHT_SENS_COUNT; ch++) {
            SERVER_CH_GET_BIT(I2C_SENS_ERROR_CH, channel == 1 ? BH1750_1_IU_ERROR : BH1750_2_IU_ERROR,&_error_bit);
            if (_error_bit)
            {
                SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); // -200 (сбой шины датчика)
                return SCPI_RES_ERR;
            }
            SYSTEM_BUS_GET(I2C1_LUX1 + ch - 1,&lux_val);
            SCPI_ResultFloat(context, lux_val);
        }
    }

    return SCPI_RES_OK;
}

static const scpi_choice_def_t adc_channel_choices[] = {
    {"AIN_AO1",        AIN_AO1},
    {"AIN_AO2",        AIN_AO2},
    {"AIN_AO3",        AIN_AO3},
    {"AIN_AO4",        AIN_AO4},
    {"AIN_AO5",        AIN_AO5},
    {"AIN_AO6",        AIN_AO6},
    {"AIN_AO7",        AIN_AO7},
    {"AIN_AO8",        AIN_AO8},
    {"AIN_AO9",        AIN_AO9},
    {"AIN_AO10",       AIN_AO10},
    {"AIN_AO11",       AIN_AO11},
    {"AIN_AO12",       AIN_AO12},
    {"AIN_AO13",       AIN_AO13},
    {"AIN_AO14",       AIN_AO14},
    {"AIN_AO15",       AIN_AO15},
    {"AIN_AO16",       AIN_AO16},
    {"AIN_AO17",       AIN_AO17},
    {"AIN_AO18",       AIN_AO18},

    {"AIN_AVsense1",   AIN_AVsense1},
    {"AIN_AVsense2",   AIN_AVsense2},
    {"AIN_AVsense3",   AIN_AVsense3},
    {"AIN_AVsense4",   AIN_AVsense4},
    {"AIN_AVsense5",   AIN_AVsense5},
    {"AIN_AVsense6",   AIN_AVsense6},

    {"LIN_AO1",        LIN_AO1},
    {"LIN_AO2",        LIN_AO2},
    {"LIN_AO3",        LIN_AO3},
    {"LIN_AO4",        LIN_AO4},

    {"DA41_GND1",      DA41_GND1},
    {"DA41_GND2",      DA41_GND2},

    {"AIN_DA11_test1", AIN_DA11_test1},
    {"AIN_DA20_test1", AIN_DA20_test1},
    {"AIN_DA33_test1", AIN_DA33_test1},
    {"AIN_DA44_test1", AIN_DA44_test1},
    {"AIN_DA41_test1", AIN_DA41_test1},

    {"AIN_DA11_test2", AIN_DA11_test2},
    {"AIN_DA20_test2", AIN_DA20_test2},
    {"AIN_DA33_test2", AIN_DA33_test2},
    {"AIN_DA44_test2", AIN_DA44_test2},
    {"AIN_DA41_test2", AIN_DA41_test2},

    {"ENV_P3V3",       ENV_P3V3},
    {"ENV_P5V0",       ENV_P5V0},
    {"ENV_VIN",        ENV_VIN},
    {"ENV_VDOUT1",     ENV_VDOUT1},
    {"ENV_P40V",       ENV_P40V},
    {"ENV_USB",        ENV_USB},
    {"ENV_VDOUT2",     ENV_VDOUT2},
    {"ENV_VDOUT3",     ENV_VDOUT3},

    SCPI_CHOICE_LIST_END
};

scpi_result_t cmd_sense_voltage_q(scpi_t *context)
{
    return get_server_float_ch_to_spci(context, adc_channel_choices,ARRAY_SIZE(adc_channel_choices) -1);
}

/* 8 датчиков мониторинга питания */
enum pmon_sensor_id {
    PMON_BRD_LOW = 0,
    PMON_BRD_HIGH,
    PMON_VDUT2,
    PMON_VDUT3,
    PMON_VIN,
    PMON_DCDC_3_3,
    PMON_VDOUT_PWR,
    PMON_VD5,
    PMON_COUNT
};

/* Словарь имен для парсера SCPI */
static const scpi_choice_def_t pmon_sensor_choices[] = {
    {"BRD_LOW",   PMON_BRD_LOW},
    {"BRD_HIGH",  PMON_BRD_HIGH},
    {"VDUT2",     PMON_VDUT2},
    {"VDUT3",     PMON_VDUT3},
    {"VIN",       PMON_VIN},
    {"DCDC_3_3",  PMON_DCDC_3_3},
    {"VDOUT_PWR", PMON_VDOUT_PWR},
    {"VD5",       PMON_VD5},
    SCPI_CHOICE_LIST_END
};

/* Таблица привязки датчиков к каналам SYSTEM_BUS */
struct pmon_channel_map {
    uint32_t current_id;
    uint32_t voltage_id;
    uint32_t power_id;
};

static const struct pmon_channel_map pmon_map[PMON_COUNT] = {
    [PMON_BRD_LOW]   = { SENS_BRD_LOW_CURRENT,   SENS_BRD_LOW_VOLTAGE,   SENS_I2C_POWER_BRD_LOW },
    [PMON_BRD_HIGH]  = { SENS_BRD_HIGH_CURRENT,  SENS_BRD_HIGH_VOLTAGE,  SENS_I2C_POWER_BRD_HIGH },
    [PMON_VDUT2]     = { SENS_VDUT2_CURRENT,     SENS_VDUT2_VOLTAGE,     SENS_I2C_POWER_VDUT2 },
    [PMON_VDUT3]     = { SENS_VDUT3_CURRENT,     SENS_VDUT3_VOLTAGE,     SENS_I2C_POWER_VDUT3 },
    [PMON_VIN]       = { SENS_VIN_CURRENT,       SENS_VIN_VOLTAGE,       SENS_I2C_POWER_VIN },
    [PMON_DCDC_3_3]  = { SENS_DCDC_3_3_CURRENT,  SENS_DCDC_3_3_VOLTAGE,  SENS_I2C_POWER_DCDC_3_3 },
    [PMON_VDOUT_PWR] = { SENS_VDOUT_PWR_CURRENT, SENS_VDOUT_PWR_VOLTAGE, SENS_I2C_POWER_VDOUT_PWR },
    [PMON_VD5]       = { SENS_VD5_CURRENT,       SENS_VD5_VOLTAGE,       SENS_I2C_POWER_VD5 },
};

/* ========================================================================= */
/* 1. ИЗМЕРЕНИЕ ТОКА: SENSe:CURRent[:DC]? [<sensor1>[, <sensor2>, ...]]      */
/* ========================================================================= */
scpi_result_t cmd_sense_current_q(scpi_t *context)
{
    int32_t sensor_idx;
    float current_val;
    int count = 0;

    // Вычитываем запрошенные сенсоры по очереди
    while (SCPI_ParamChoice(context, pmon_sensor_choices, &sensor_idx, FALSE)) {
        SYSTEM_BUS_GET(pmon_map[sensor_idx].current_id, &current_val);
        SCPI_ResultFloat(context, current_val);
        count++;
    }

    // Если параметров не было — отдаем ток всех 8 датчиков
    if (count == 0) {
        for (int i = 0; i < PMON_COUNT; i++) {
            SYSTEM_BUS_GET(pmon_map[i].current_id, &current_val);
            SCPI_ResultFloat(context, current_val);
        }
    }

    return SCPI_RES_OK;
}

/* ========================================================================= */
/* 2. ИЗМЕРЕНИЕ МОЩНОСТИ: SENSe:POWer[:DC]? [<sensor1>[, <sensor2>, ...]]    */
/* ========================================================================= */
scpi_result_t cmd_sense_power_q(scpi_t *context)
{
    int32_t sensor_idx;
    float power_val;
    int count = 0;

    while (SCPI_ParamChoice(context, pmon_sensor_choices, &sensor_idx, FALSE)) {
        SYSTEM_BUS_GET(pmon_map[sensor_idx].power_id, &power_val);
        SCPI_ResultFloat(context, power_val);
        count++;
    }

    if (count == 0) {
        for (int i = 0; i < PMON_COUNT; i++) {
            SYSTEM_BUS_GET(pmon_map[i].power_id, &power_val);
            SCPI_ResultFloat(context, power_val);
        }
    }

    return SCPI_RES_OK;
}

/* ========================================================================= */
/* 3. НАПРЯЖЕНИЕ НА ДАТЧИКЕ: SENSe:POWer:VOLTage? [<sensor1>[, ...]]         */
/* ========================================================================= */
scpi_result_t cmd_sense_power_voltage_q(scpi_t *context)
{
    int32_t sensor_idx;
    float volt_val;
    int count = 0;

    while (SCPI_ParamChoice(context, pmon_sensor_choices, &sensor_idx, FALSE)) {
        SYSTEM_BUS_GET(pmon_map[sensor_idx].voltage_id, &volt_val);
        SCPI_ResultFloat(context, volt_val);
        count++;
    }

    if (count == 0) {
        for (int i = 0; i < PMON_COUNT; i++) {
            SYSTEM_BUS_GET(pmon_map[i].voltage_id, &volt_val);
            SCPI_ResultFloat(context, volt_val);
        }
    }

    return SCPI_RES_OK;
}

/* ========================================================================= */
/* 4. КОМПЛЕКСНЫЙ ЗАМЕР: SENSe:POWer:ALL? <sensor> (Напряжение, Ток, Ватты)  */
/* ========================================================================= */
scpi_result_t cmd_sense_power_all_q(scpi_t *context)
{
    int32_t sensor_idx;
    float v = 0.0f, i = 0.0f, p = 0.0f;

    // Для комплексного среза имя сенсора обязательно
    if (!SCPI_ParamChoice(context, pmon_sensor_choices, &sensor_idx, TRUE)) {
        return SCPI_RES_ERR;
    }

    SYSTEM_BUS_GET(pmon_map[sensor_idx].voltage_id, &v);
    SYSTEM_BUS_GET(pmon_map[sensor_idx].current_id, &i);
    SYSTEM_BUS_GET(pmon_map[sensor_idx].power_id, &p);

    // Отдаем триплет: Напряжение (В), Ток (А), Мощность (Вт)
    SCPI_ResultFloat(context, v);
    SCPI_ResultFloat(context, i);
    SCPI_ResultFloat(context, p);

    return SCPI_RES_OK;
}
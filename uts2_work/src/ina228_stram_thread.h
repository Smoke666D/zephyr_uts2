#ifndef INA228_STREAM_THREAD_H
#define INA228_STREAM_THREAD_H

/**********************Каналы контроля тока, датчики IDC**************************/


/* Названия датчиков для понятного логирования */
typedef enum
 {
    BRD_LOW     = 0,
    BRD_HIGH    = 1,
    VDUT2       = 2,
    VDUT3       = 3,
    VIN         = 4,
    DCDC_3_3    = 5,
    VDOUT_PWR   = 6,
    VD5         = 7,
    NUM_SENSORS  = 8,
} CUR_SENS_CHANEL;

struct sensor_reading 
{
    double voltage;
    double current;
};

struct ina_batch_msg 
{
    struct sensor_reading sensors[NUM_SENSORS];
};




#endif
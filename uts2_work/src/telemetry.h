#include <math.h> // для isnan (если нужно)

// 1. Описываем структуру пакета телеметрии (выравнивание по байтам __packed)
struct telemetry_packet {
    uint8_t  magic;          // Байт синхронизации, например 0xBE (Binary End)
    
    // ADS1115
    float    rf_power;
    float    ntc;
    float    ext_vsense;
    float    brd_detect;

    // Температура и свет
    float    temp_gnd;
    float    temp_vdd;
    float    light_gnd;
    float    light_vdd;

    // AO (1-18)
    float    ao[18];

    // VSense (1-6)
    float    vsense[6];

    // Step tests (DA tests)
    float    step7[5];
    float    step8[5];

    // Environment (P3V3, P5V0, VIN, VDOUT1, P40V, USB, VDOUT2, VDOUT3)
    float    env[8];

    float    current_sensors[16]; 
} __packed;
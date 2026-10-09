#include <math.h> // для isnan (если нужно)

#define TELEMETRY_MAGIC_BYTE 0xBE
#define CAN_MAGIC_BYTE       0xCB

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
        // --- Напряжения LIN интерфейсов (4 шт) ---
    // lin_voltages[0] -> LIN 1, lin_voltages[1] -> LIN 2, и т.д.
    float    lin_voltages[4];
} __packed;


struct can_rx_bin_packet {
    uint8_t  magic;      // 0xCB
    uint8_t  can_num;    // Номер интерфейса (1, 2 или 3)
    uint32_t id;         // CAN ID
    uint8_t  dlc;        // Длина данных (0-64)
    uint8_t  data[64];   // Полезная нагрузка
} __packed;
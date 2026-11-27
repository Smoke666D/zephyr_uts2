#pragma once

#define OUT_CTL_INIT_LEVEL              APPLICATION
#define OUT_CTL_INIT_PRIOR              40
#define DINPUT_DEAMON_INIT_LEVEL        APPLICATION
#define DINPUT_DEAMON_INIT_PRIOR        50
#define SELF_TEST_INIT_LEVEL            APPLICATION
#define SELF_TEST_INIT_PRIOR            90
#define DIN_DEAMON_INIT_LEVEL           APPLICATION
#define DIN_DEAMON_INIT_PRIOR           40
#define SPCI_CAN_INIT_LEVEL             APPLICATION
#define SPCI_CAN_INIT_PRIOR             51
#define SERVER_INIT_LEVEL               POST_KERNEL
#define SERVER_INIT_PRIOR               90
#define ACC_DEAMON_INIT_LEVEL           APPLICATION
#define ACC_DEAMON_INIT_PRIOR           80

#define DINPUT_K_WORK                   COMMON_WORKER  
#define OUT_CTL_K_WORK                  COMMON_WORKER

#define APP_WORKER_PRIORITY             7
#define COMMON_WORKER_PRIORITY          4
#define REAL_TIME_WORKER_PRIORITY       -1  

/**************WORKER*********************************/
#define SCPI_MONITOR_K_WORK             COMMON_WORKER
//APPLICATION_WORKER

typedef enum
{  
    HSE_STATUS,
    LSE_STATUS,
    GPIO_A_STATUS,
    GPIO_B_STATUS,
    GPIO_C_STATUS,
    GPIO_D_STATUS,
    GPIO_E_STATUS,
    ACC_X,
    ACC_Y,
    ACC_Z,
    OD1,
    OD2,
    OD3,
    OD4,
    OD5,
    OD6,
    OD7,
    OD8,
    OD9,
    OD10,
    OD11,
    IN3_PU_ON,
    IN4_PU_ON,
    OUT_P,
    LIN1_PU,
    LIN2_PU,
    LIN_BREAK,
    LIN_SLEEP,
    CAN1_STB,
    CAN2_STB,
    CAN3_STB,
    IN_GREY,
    IN_IGN,
    IN3,  
    IN4,  
    IN_GREY_ON_GF,  
    IN_GREY_OFF_GF,  
    IN_IGN_ON_GF,  
    IN_IGN_OFF_GF,  
    IN_3_ON_GF,  
    IN_3_OFF_GF,  
    IN_4_ON_GF,  
    IN_4_OFF_GF,  
    LED1,
    LED2,
    LED3,
    HARDWARE_ERROR_REGISTER1,
    HARDWARE_ERROR_REGISTER2,
    HARDWARE_ERROR_REGISTER3,
    CAN1_TX,
    CAN2_TX,
    CAN3_TX,
    CAN1_RX,
    CAN2_RX,
    CAN3_RX,
    LIN1_TX,
    LIN1_RX,
    LIN2_TX,
    LIN2_RX,
    /* Сюда в будущем можно добавлять любые другие параметры других модулей */
    SYSTEM_BUS_COUNT
} SYSTEM_BUS_ID;

/* Статусы диагностики кварца */
typedef enum {
    HSE_STATUS_NOT_TESTED = 0,
    HSE_STATUS_OK,              /* Кварц запустился, система работает от HSE */
    HSE_STATUS_ERR_TIMEOUT,     /* Кварц не завелся! Откат на внутренний HSI */
    HSE_STATUS_ERR_PLL_LOCK,     /* Кварц завелся, но PLL не смог захватить частоту */
    HSE_STATUS_ERR_FREQ,
} hse_diag_status_t;

typedef enum {
    LSE_STATUS_NOT_TESTED = 0,
    LSE_STATUS_OK,              /* Кварц запустился, система работает от HSE */
    LSE_STATUS_ERR_PRESENT,     /* Кварц не завелся! Откат на внутренний HSI */
    LSE_STATUS_ERR_FREQ,     /* Кварц завелся, но PLL не смог захватить частоту */
} lse_diag_status_t;
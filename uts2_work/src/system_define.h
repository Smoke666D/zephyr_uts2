#pragma once


#define ADS1115_DEAMON_INIT_LEVEL       APPLICATION
#define ADS1115_DEAMON_INIT_PRIOR       84
#define EEPROM_DEAMON_INIT_LEVEL        APPLICATION
#define EEPROM_DEAMON_INIT_PRIOR        85
#define DAC_DEAMON_INIT_LEVEL           APPLICATION
#define DAC_DEAMON_INIT_PRIOR           50
#define LED_DEAMON_INIT_LEVEL           APPLICATION
#define LED_DEAMON_INIT_PRIOR           80
#define PWR_CTL_INIT_LEVEL              APPLICATION
#define PWR_CTL_INIT_PRIOR              40
#define I2C_SENS_DEAMON_INIT_LEVEL      APPLICATION
#define I2C_SENS_DEAMON_INIT_PRIOR      83
#define ADC_PIPELINE_DEAMON_INIT_LEVEL  APPLICATION 
#define ADC_PIPELINE_DEAMON_INIT_PRIOR  40 
#define LIN_DEAMON_INIT_LEVEL           APPLICATION 
#define LIN_DEAMON_INIT_PRIOR           50 
#define CAN_DEAMON_INIT_LEVEL           APPLICATION 
#define CAN_DEAMON_INIT_PRIOR           50 
#define UART_DETECT_DEAMON_INIT_LEVEL   APPLICATION
#define UART_DETECT_DEAMON_INIT_PRIOR   50
#define OUT_CTL_DEAMON_INIT_LEVEL       APPLICATION
#define OUT_CTL_DEAMON_INIT_PRIOR       40
#define MCP_CTL_DEAMON_INIT_LEVEL       APPLICATION
#define MCP_CTL_DEAMON_INIT_PRIOR       70
#define SPCI_CAN_INIT_LEVEL             APPLICATION
#define SPCI_CAN_INIT_PRIOR             51
#define SERVER_INIT_LEVEL               POST_KERNEL
#define SERVER_INIT_PRIOR               90


#define TELEMETRY_THREAD_PRIORITY       0    
#define INA228_THREAD_PRIORITY          4
#define EEPROM_THREAD_PRIORITY          5   
#define FRAM_THREAD_PRIORITY            6    
#define APP_WORKER_PRIORITY             7
#define COMMON_WORKER_PRIORITY          4
#define REAL_TIME_WORKER_PRIORITY       -1  


/**************WORKER*********************************/
#define SCPI_MONITOR_K_WORK             COMMON_WORKER
//APPLICATION_WORKER
#define LIN_K_WORK                      REAL_TIME_WORKER
#define CAN_K_WORK                      REAL_TIME_WORKER
#define UART_DETECT_K_WORK              COMMON_WORKER
#define POWER_CTL_K_WORK                COMMON_WORKER
#define OUT_CTL_K_WORK                  REAL_TIME_WORKER
#define MCP_CTL_K_WORK                  COMMON_WORKER
#define LED_K_WORK                      COMMON_WORKER
#define I2C_SENS_K_WORK                 COMMON_WORKER
#define DAC_K_WORK                      COMMON_WORKER
#define INT_DAC_K_WORK                  REAL_TIME_WORKER
#define ASS1115_K_WORK                  COMMON_WORKER
#define ADC_PIPELINE_K_WORK             REAL_TIME_WORKER
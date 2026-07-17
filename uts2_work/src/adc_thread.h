#ifndef ADC_THREAD_H
#define ADC_THREAD_H


#define AIN_TASK_STACK_SIZE 2024
#define AIN_TASK_PRIORITY 10



#define COMBINATIONS_COUNT 8

#define ADC_NUM_CHANNELS   4
#define ADC_SAMPLES_PER_CH 8
#define ADC_BUF_SIZE       (ADC_NUM_CHANNELS * ADC_SAMPLES_PER_CH)

int adc_scanner_start(void);

int StartDmaGPIO(void);
int ain_thread_start(void);

#endif
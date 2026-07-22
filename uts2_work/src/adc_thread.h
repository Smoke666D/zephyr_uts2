#ifndef ADC_THREAD_H
#define ADC_THREAD_H


#define AIN_TASK_STACK_SIZE 2024
#define AIN_TASK_PRIORITY 10

typedef enum
{
    AO1 = 0,
    AO7 = 1,
    AO13 = 2,
    AVsense1 = 3,
    AO2 = 4,
    AO8 = 5,
    AO14 = 6,
    AVsense2 = 7,    
    AO3 = 8,
    AO9 = 9,
    AO15 = 10,
    AVsense3 = 11,        
    AO4 = 12,
    AO10 = 13,
    AO16 = 14,
    AVsense4 = 15,
    AO5 = 16,
    AO11 = 17,
    AO17 = 18,
    AVsense5 = 19,
    AO6 = 20,                    
    AO12 = 21,                
    AO18 = 22,
    AVsense6 = 23,
    DA11_test1 = 24,
    DA20_test1 = 25,
    DA33_test1 = 26,
    DA44_test1 = 27,
    DA11_test2 = 28,
    DA20_test2 = 29,
    DA33_test2 = 30,
    DA44_test2 = 31,
    TOTAL_CHANNEL_COUNT = 32,
} AIN_MUX_CHANNEL_NUMBER;

#define COMBINATIONS_COUNT 8

#define ADC_NUM_CHANNELS   4
#define ADC_SAMPLES_PER_CH 8
#define ADC_BUF_SIZE       (ADC_NUM_CHANNELS * ADC_SAMPLES_PER_CH)

int ain_thread_start(void);



#endif
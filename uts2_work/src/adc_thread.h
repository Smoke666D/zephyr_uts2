#ifndef ADC_THREAD_H
#define ADC_THREAD_H






#define COMBINATIONS_COUNT 8

#define ADC_NUM_CHANNELS   4
#define ADC_SAMPLES_PER_CH 8
#define ADC_BUF_SIZE       (ADC_NUM_CHANNELS * ADC_SAMPLES_PER_CH)

int adc_scanner_start(void);

int StartDmaGPIO(void);


#endif
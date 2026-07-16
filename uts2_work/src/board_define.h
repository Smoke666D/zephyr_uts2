#ifndef BOARD_DEFINE_H
#define BOARD_DEFINE_H


/*Описание портов управления аналоговыми мультиплексорами */
#define MUX_AIN_GPIO_PORT_CHAR 'B' //< Символ порта для макроса STM32_PINMUX в DTS
#define MUX_AIN_GPIO_PORT_NODELABEL    gpiob
/* Номера пинов, которые будут использоваться и в DTS, и в C-коде */
#define MUX_AO_PIN  0
#define MUX_A1_PIN  13
#define MUX_A2_PIN  14

/* Настройки Таймера */
#define SEQ_TIMER_NODELABEL timers3

/* 
 * Ярлык (лейбл) для дочернего счетчика. 
 * Он будет объявлен в app.overlay и использован в C-коде.
 */
#define SEQ_COUNTER_NODELABEL seq_timer_counter

/* DMAMUX Request ID */
#define SEQ_TIMER_DMAMUX_SLOT 27


/* ================== НАСТРОЙКИ АЦП (Аналоговые входы) ================== */
/* 
 * Сопоставляем ваши псевдонимы Vsense с реальными АЦП-входами STM32H723.
 * Все эти каналы физически подключены к первому АЦП (ADC1):
 */
#define MUX1_ADC_VSENSE_PORT 'C'
#define MUX1_ADC_VSENSE_PIN  0   // PC0 -> Канал ADC_INP10

#define MUX2_ADC_VSENSE_PORT 'C'
#define MUX2_ADC_VSENSE_PIN  1   // PC1 -> Канал ADC_INP11

#define MUX3_ADC_VSENSE_PORT 'C'
#define MUX3_ADC_VSENSE_PIN  3   // PC3 -> Канал ADC_INP13

#define MUX4_ADC_VSENSE_PORT 'A'
#define MUX4_ADC_VSENSE_PIN  3   // PA3 -> Канал ADC_INP15

#define MUX5_ADC_VSENSE_PORT 'B'
#define MUX5_ADC_VSENSE_PIN  1   // PB1 -> Канал ADC_INP5

#endif /* APP_PINS_H */
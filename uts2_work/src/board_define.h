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


#endif /* APP_PINS_H */
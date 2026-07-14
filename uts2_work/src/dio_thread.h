#ifndef DIO_THREAD_H
#define DIO_THREAD_H

#define STACK_SIZE 1024
#define PRIORITY 5

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)


int dio_thread_start(void);

#endif
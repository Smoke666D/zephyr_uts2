#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define STACK_SIZE 1024
#define PRIORITY 5


#define MAX_INPUT_STRING  10

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

int usb_thread_start(void);

#ifdef __cplusplus
}
#endif
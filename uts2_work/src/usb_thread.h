#ifndef USB_THREAD_H
#define USB_THREAD_H

#define STACK_SIZE 1024
#define PRIORITY 5

/* The devicetree node identifier for the "led0" alias. */
#define LED1_NODE DT_ALIAS(led1)

int usb_thread_start(void);
#endif
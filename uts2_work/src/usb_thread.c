
#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include "usb_thread.h"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <unistd.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <zephyr/console/console.h>


const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static struct usbd_context *cdc_usbd;

K_SEM_DEFINE(dtr_sem, 0, 1);

static void sample_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{	
	if (usbd_can_detect_vbus(ctx)) 
    {
        switch (msg->type)
        {
            case USBD_MSG_VBUS_READY:
                    usbd_enable(ctx);
                    break;    
            case USBD_MSG_VBUS_REMOVED:
                    usbd_disable(ctx);
                    break;
            default:
                    break;
        }		
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			k_sem_give(&dtr_sem);
		}
	}	
}

static bool enable_usb_device_next(void)
{
	int err;

    // Для инициализации используем библиотечный sample OS.
	cdc_usbd = sample_usbd_init_device(sample_msg_cb);

	if (cdc_usbd == NULL) 
    {		
		return false;
	}

	if (!usbd_can_detect_vbus(cdc_usbd)) 
    {
		err = usbd_enable(cdc_usbd);
		if (err)
        {	
			return false;
		}
	}
	return true;
}

int usb_thread_start(void)
{
    if (false
        || !device_is_ready(uart_dev)
        || !enable_usb_device_next()
    )    
    {		
		return 0;
	}
	
    // Ждем события подключения терминала к CDC и ставим таймаут на 100 мс, что бы ПК успел отработать
    k_sem_take(&dtr_sem, K_FOREVER);	
	k_msleep(100);

    return 0;
}


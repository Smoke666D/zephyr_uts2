
#include <sample_usbd.h>
#include <zephyr/smf.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include "usb_thread.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <unistd.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <zephyr/console/console.h>
#include "system_status.h"

/*static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);
*/
const char *main_menu[] =
{
    "Main testing menu, select the desired menu item",
    "1 - LED tests",
    "2 - I2C bus device tests",
    "3 - SPI bus device tests",
    "A - Automatic test",
    "h - Display help",
    0,
};

const char *led_menu[] =
{
    "LED testing menu, select the desired menu item",
    "1 - Toggle led1",
    "2 - Toggle led2",
    "3 - Toggle led3",
    "O - ALL switch on",
    "F - ALL switch off",
    "q - Back to main menu"
    "h - Display help",
    0,
};


static void set_led_status(int led_index, bool on, bool blinking, int period_ms)
{
    struct leds_state state;

    /* Читаем текущее состояние */
    zbus_chan_read(&leds_chan, &state, K_NO_WAIT);

    /* Меняем нужный светодиод */
    state.led[led_index].on = on;
    state.led[led_index].blinking = blinking;
    state.led[led_index].period_ms = period_ms;

    /* Публикуем новое состояние */
    int ret = zbus_chan_pub(&leds_chan, &state, K_NO_WAIT);
    if (ret != 0) {
    printk("[MENU] Publish failed: %d\n", ret);
} else {
    printk("[MENU] Published LED state successfully\n");
}

    printf("[Menu] LED%d: on=%d blink=%d period=%d\n",
           led_index, on, blinking, period_ms);
}


/*******************************************************************************************************/
static const struct smf_state menu_states[];

enum menu_state { PARENT,  LED_STATE, S0, S1, S2 };

// Дискриптор конечного автомата 
struct s_object 
{
        // Состояние конечного автомата, должно быть первым членом структуры
        struct smf_ctx state;
        // Строка с текстовым вводом в меню
        char          cmd[MAX_INPUT_STRING];        
} s_obj;

static void parent_entry(void *o)
{
    for (uint8_t i=0; main_menu[i] != 0; i++)
    {
        printf("%s\r\n",main_menu[i]);
    }
}

static enum smf_state_result parent_run(void *o)
{   
    switch(s_obj.cmd[0])
    {
        case '1':
            smf_set_initial(SMF_CTX(&s_obj), &menu_states[LED_STATE]);
            break;
        case 'h':
        case 'H':
            smf_set_initial(SMF_CTX(&s_obj), &menu_states[PARENT]);
            break;
        default:
            break;

    }
    s_obj.cmd[0] = 0;

    return SMF_EVENT_HANDLED;
}


static void led_entry(void *o)
{
    for (uint8_t i=0; led_menu[i] != 0; i++)
    {
        printf("%s\r\n", led_menu[i]);
    }
}

static enum smf_state_result led_run(void *o)
{   
    switch(s_obj.cmd[0])
    {
        case '1':
           // gpio_pin_toggle_dt(&led0);
            break;
        case '2':
           // gpio_pin_toggle_dt(&led1);
            break;
        case '3':
           // gpio_pin_toggle_dt(&led2);
            break;
        case 'o':
        case 'O':
            for (int i = 0; i < 3; i++) {
                set_led_status(i, false, false, 0);
            }
            break;
        case 'f':
        case 'F':
         for (int i = 0; i < 3; i++) {
                set_led_status(i, true, false, 0);
            }
            break;
        case 'q':
        case 'Q':
            smf_set_initial(SMF_CTX(&s_obj), &menu_states[PARENT]);
            break;
        case 'h':
        case 'H':
            smf_set_initial(SMF_CTX(&s_obj), &menu_states[LED_STATE]);
            break;
        default:
            break;

    }
    s_obj.cmd[0] = 0;

    return SMF_EVENT_HANDLED;
}

/* Populate state table */
static const struct smf_state menu_states[] = 
{
        /* Parent state does not have a run action */
        [PARENT]    = SMF_CREATE_STATE(parent_entry, parent_run,NULL, NULL, NULL),  
        [LED_STATE] = SMF_CREATE_STATE(led_entry,    led_run,   NULL, NULL, NULL),       
};



LOG_MODULE_REGISTER(cdc_acm_echo, LOG_LEVEL_INF);

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






K_THREAD_STACK_DEFINE(stack, STACK_SIZE);
static struct k_thread thread_data;

static void func(void *arg1, void *arg2, void *arg3)
{
    const struct device *cdc = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
    unsigned char c;
    int i = 0;
    char buf[MAX_INPUT_STRING];
	

    while (1) 
    {
        		
	    

        while (uart_poll_in(cdc, &c) == 0) 
        {
            if (c == '\r' || c == '\n') 
            {
                if (i > 0) 
                {
                    buf[i] = '\0';
                    strcpy(s_obj.cmd,buf);
                    i = 0;
                }
            } 
            else 
            {
                buf[i++] = c;
                if (i >= sizeof(buf) - 1) 
                {                    
                    i = 0;
                }
            }
        }
        k_msleep(100);

        //Выполняем конечный автомат
        smf_run_state(SMF_CTX(&s_obj));
      
    }
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

    // Создаем поток для работы с меню
    k_thread_create(&thread_data, stack, STACK_SIZE,
                    func, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
   
    //Инициализируем конечный автомат
   smf_set_initial(SMF_CTX(&s_obj), &menu_states[PARENT]);

    return 0;
}
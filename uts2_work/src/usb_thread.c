
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
#include "memomory_link_macro.h"


static void on_action(struct smf_ctx *, const struct smf_state *, enum smf_action_type);
static void on_transition(struct smf_ctx *,const struct smf_state *,const struct smf_state *);




static const struct smf_hooks hookss = {
        .on_action = on_action,
        .on_transition = on_transition,
        /* .on_error = NULL — any member may be NULL */
};

const char *main_menu[] =
{
    "Main testing menu, select the desired menu item",
    "1 - LED tests",
    "2 - I2C bus device tests",
    "3 - SPI bus device tests",
    "4 - ADC device test",
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
    "q - Back to main menu",
    "h - Display help",
    0,
};

const char *adc_menu[] =
{
    "ADC testing menu, select the desired menu item",
    "1 -Show all channel",    
    "q - Back to main menu",
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


}

static void set_led_toggle(int led_index)
{
    struct leds_state state;
    zbus_chan_read(&leds_chan, &state, K_NO_WAIT);
    state.led[led_index].on = !state.led[led_index].on;
 /* Публикуем новое состояние */
    int ret = zbus_chan_pub(&leds_chan, &state, K_NO_WAIT);

}


/*******************************************************************************************************/
static const struct smf_state menu_states[];

enum menu_state {COMMON_ACTION, PARENT,  LED_STATE, ADC_STATE, S1, S2 };

// Дискриптор конечного автомата 
struct s_object 
{
        // Состояние конечного автомата, должно быть первым членом структуры
        struct smf_ctx state;
        // Строка с текстовым вводом в меню
        char          cmd[MAX_INPUT_STRING];        
} s_obj;


static enum smf_state_result parent_run(void *o)
{   
    enum smf_state_result res = SMF_EVENT_HANDLED;
    if (s_obj.cmd[0])
    {
        switch(s_obj.cmd[0])
        {
            case '1':
                smf_set_state(SMF_CTX(&s_obj), &menu_states[LED_STATE]);
                break;
            case '4':
                smf_set_state(SMF_CTX(&s_obj), &menu_states[ADC_STATE]);
                break;
            case 'h':
            case 'H':
                smf_set_state(SMF_CTX(&s_obj), &menu_states[PARENT]);
                break;
            default:
                break;
        }
       
        res =  SMF_EVENT_PROPAGATE;
    }
      s_obj.cmd[0] = 0;
    return res;
    
}

/** 
* @brief Функция отображает текущие мени
* @details Должна быть присвоена параметру on_entry при объявлении состояния конечного автомата
*/
static void menu_print(void * obj)
{
    const char **menu_text_p;

    struct smf_ctx *ctx = obj;
    const struct smf_state *current = smf_get_current_executing_state(ctx);

    if (current == &menu_states[PARENT])
    {
            menu_text_p = main_menu;
    }
    else if (current == &menu_states[LED_STATE])
    {
            menu_text_p = led_menu;
    }
    else if (current == &menu_states[ADC_STATE])
    {
            menu_text_p = adc_menu;
    }
    for (uint8_t i=0; menu_text_p[i] != 0; i++)
    {
        printf("%s\r\n", menu_text_p[i]);
    }
    

}


static enum smf_state_result led_run(void *o)
{   
    switch(s_obj.cmd[0])
    {
        case '1':
            set_led_toggle(0);           
            break;
        case '2':
           set_led_toggle(1);
            break;
        case '3':
           set_led_toggle(2);
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
            smf_set_state(SMF_CTX(&s_obj), &menu_states[PARENT]);
            break;
        case 'h':
        case 'H':
            smf_set_state(SMF_CTX(&s_obj), &menu_states[LED_STATE]);
            break;
        default:
            break;

    }
    
  s_obj.cmd[0] = 0;
    return SMF_EVENT_HANDLED;
}

static enum smf_state_result adc_run(void *o)
{   
    switch(s_obj.cmd[0])
    {
        case '1':
                  
            break;
        case '2':
           
            break;
        case '3':
           
            break;
       
        case 'q':
        case 'Q':
            smf_set_state(SMF_CTX(&s_obj), &menu_states[PARENT]);
            break;
        case 'h':
        case 'H':
            smf_set_state(SMF_CTX(&s_obj), &menu_states[ADC_STATE]);
            break;
        default:
            break;

    }
    
  s_obj.cmd[0] = 0;
    return SMF_EVENT_HANDLED;
}


static enum smf_state_result common_run(void *o)
{   
   printk("Commnad run\r");
    

    return SMF_EVENT_HANDLED;
}

static void common_exit(void *o)
{
       printk("Commnad exit\r");
}

/* Populate state table */
static const struct smf_state menu_states[] = 
{
        [COMMON_ACTION]    = SMF_CREATE_STATE(NULL, common_run,common_exit, NULL, NULL),  
        [PARENT]           = SMF_CREATE_STATE(menu_print, parent_run,NULL, &menu_states[COMMON_ACTION], NULL),  
        [LED_STATE]        = SMF_CREATE_STATE(menu_print,    led_run,   NULL, &menu_states[COMMON_ACTION], NULL),       
        [ADC_STATE]        = SMF_CREATE_STATE(menu_print,    adc_run,   NULL, NULL, NULL),       
};




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
                    //Выполняем конечный автомат
                    
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

      //  smf_run_state(SMF_CTX(&s_obj));        
      
    }
}


static uint8_t __attribute__((__section__("DTCM"))) stack[STACK_SIZE];
static struct k_thread thread_data;

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
   // k_thread_create(&thread_data, (k_thread_stack_t *)stack, STACK_SIZE,
     //               func, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
   
    //Инициализируем конечный автомат
   //smf_set_initial(SMF_CTX(&s_obj), &menu_states[PARENT]);
   
  // smf_set_hooks(SMF_CTX(&s_obj), &hookss);

    return 0;
}

static void on_action(struct smf_ctx *ctx,
                      const struct smf_state *state,
                      enum smf_action_type action_type)
{
      //  s_obj.cmd[0] = 0;
}

static void on_transition(struct smf_ctx *ctx,
                          const struct smf_state *source,
                          const struct smf_state *dest)
{
        /* Log or record the transition */
}
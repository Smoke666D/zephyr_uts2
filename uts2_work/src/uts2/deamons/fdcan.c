#include <zephyr/shell/shell.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

#include "app_worker.h"
#include "system_bus_model.h"
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/iterable_sections.h>
#include "system_define.h"

LOG_MODULE_REGISTER(dev_can_init, LOG_LEVEL_INF);

#define DT_DRV_COMPAT zephyr_can_instance

/* Структура Си-конфигурации для каждого CAN-канала */
struct can_instance_config {
    const struct device *can_device; /* Ссылка на физический драйвер FDCAN */
};


static const struct can_instance_config can_channels[] = {
    { .can_device = DEVICE_DT_GET(DT_NODELABEL(fdcan1)) },
    { .can_device = DEVICE_DT_GET(DT_NODELABEL(fdcan2)) },
    { .can_device = DEVICE_DT_GET(DT_NODELABEL(fdcan3)) },
};

#define NUM_CAN_CHANNELS ARRAY_SIZE(can_channels)


typedef struct canfd_state_t
{
    bool value[NUM_CAN_CHANNELS];

}canfd_state_t;


typedef enum 
{
    FD_CAN1 = 0x01,
    FD_CAN2 = 0x02,
    FD_CAN3 = 0x03,
} CAN_SYSTEM_BUS_ID;

/* Очереди сообщений для каждого канала */
K_MSGQ_DEFINE(can1_tx_msgq, sizeof(system_can_message_t), 10, 4);
K_MSGQ_DEFINE(can2_tx_msgq, sizeof(system_can_message_t), 10, 4);
K_MSGQ_DEFINE(can3_tx_msgq, sizeof(system_can_message_t), 10, 4);
K_MSGQ_DEFINE(can1_rx_msgq, sizeof(system_can_message_t), 10, 4);
K_MSGQ_DEFINE(can2_rx_msgq, sizeof(system_can_message_t), 10, 4);
K_MSGQ_DEFINE(can3_rx_msgq, sizeof(system_can_message_t), 10, 4);

/* 1. СНАЧАЛА ОБЪЯВЛЯЕМ СТРУКТУРУ КОНТЕКСТА */
struct can_tx_context {
    const struct device *can_dev;
    struct k_msgq *tx_msgq;
    struct k_msgq *rx_msgq;
    CAN_SYSTEM_BUS_ID bus_id;
};

/* 2. ЗАТЕМ ОБЪЯВЛЯЕМ МАССИВ КОНТЕКСТОВ */
static struct can_tx_context can_contexts[] = {
    { .can_dev = NULL, .tx_msgq = &can1_tx_msgq, .rx_msgq = &can1_rx_msgq, .bus_id = FD_CAN1 },
    { .can_dev = NULL, .tx_msgq = &can2_tx_msgq, .rx_msgq = &can2_rx_msgq, .bus_id = FD_CAN2 },
    { .can_dev = NULL, .tx_msgq = &can3_tx_msgq, .rx_msgq = &can3_rx_msgq, .bus_id = FD_CAN3 },
};

static struct k_work can_rx_task                    __attribute__((section("DTCM"))); 
static struct k_poll_event can_tx_poll_events[3]    __attribute__((section("DTCM")));
static struct k_work_poll can_task                  __attribute__((section("DTCM"))); 

/* Прототипы функций теперь видят структуру правильно */
static void _can_tx_handler(struct k_work *_work);
static void _can_state_listener_cb(const struct zbus_channel *_chan);

static void can1_tx_callback(const struct device *dev, int error, void *user_data);
static void can2_tx_callback(const struct device *dev, int error, void *user_data);
static void can3_tx_callback(const struct device *dev, int error, void *user_data);


ZBUS_LISTENER_DEFINE(can_state_listener, _can_state_listener_cb);

ZBUS_CHAN_DEFINE(can_state_chan,
                 canfd_state_t,
                 NULL, 
                 NULL, 
                 can_state_listener, 
                 ZBUS_MSG_INIT(0) 
);




ZBUS_CHAN_DEFINE(can1_rx_chan,
                 system_can_message_t,
                 NULL, 
                 NULL, 
                 ZBUS_OBSERVERS_EMPTY, 
                 ZBUS_MSG_INIT(0) 
);
ZBUS_CHAN_DEFINE(can2_rx_chan,
                 system_can_message_t,
                 NULL, 
                 NULL, 
                 ZBUS_OBSERVERS_EMPTY, 
                 ZBUS_MSG_INIT(0) 
);
ZBUS_CHAN_DEFINE(can3_rx_chan,
                 system_can_message_t,
                 NULL, 
                 NULL, 
                 ZBUS_OBSERVERS_EMPTY, 
                 ZBUS_MSG_INIT(0) 
);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

 static void _can_state_listener_cb(const struct zbus_channel *_chan)
 {
        canfd_state_t * msg = zbus_chan_msg(&can_state_chan);

        for (uint8_t i = 0; i < NUM_CAN_CHANNELS; i++)
        {                            
            if (msg->value[i] == true)
            {
                can_start(can_channels[i].can_device);
            } 
            else 
            {
                can_stop(can_channels[i].can_device);
            }                            
        }
 }


/* TX Callback от CAN драйвера: вызывается, когда фрейм успешно ушел в шину */
static void can1_tx_callback(const struct device *dev, int error, void *user_data)
{
    system_can_message_t msg;
    if (k_msgq_get(&can1_tx_msgq, &msg, K_NO_WAIT) == 0) 
    {        
        struct can_frame frame = 
        {
            .id = msg.id,
            .flags = msg.flags,
            .dlc = msg.dlc,
        };
        memcpy(frame.data, msg.data, sizeof(frame.data));

        // Отправляем в CAN контроллер. Передаем ctx в качестве user_data для callback.
        can_send(can_contexts[0].can_dev, &frame, K_NO_WAIT, can1_tx_callback, 0 );     
    }
}

static void can2_tx_callback(const struct device *dev, int error, void *user_data)
{
    system_can_message_t msg;
    if (k_msgq_get(&can2_tx_msgq, &msg, K_NO_WAIT) == 0) 
    {        
        struct can_frame frame = 
        {
            .id = msg.id,
            .flags = msg.flags,
            .dlc = msg.dlc,
        };
        memcpy(frame.data, msg.data, sizeof(frame.data));

        // Отправляем в CAN контроллер. Передаем ctx в качестве user_data для callback.
        can_send(can_contexts[1].can_dev, &frame, K_NO_WAIT, can2_tx_callback, 0 );     
    }
}

static void can3_tx_callback(const struct device *dev, int error, void *user_data)
{
    system_can_message_t msg;
    if (k_msgq_get(&can3_tx_msgq, &msg, K_NO_WAIT) == 0) 
    {        
        struct can_frame frame = 
        {
            .id = msg.id,
            .flags = msg.flags,
            .dlc = msg.dlc,
        };
        memcpy(frame.data, msg.data, sizeof(frame.data));

        // Отправляем в CAN контроллер. Передаем ctx в качестве user_data для callback.
        can_send(can_contexts[2].can_dev, &frame, K_NO_WAIT, can3_tx_callback, 0 );     
    }
}

/* Обработчик Workqueue: проверяет очереди всех каналов */
static void _can_tx_handler(struct k_work *_work)
{    
    system_can_message_t msg;
    if (can_tx_poll_events[0].state != K_POLL_STATE_NOT_READY)
    {
        if (k_msgq_get(&can1_tx_msgq, &msg, K_NO_WAIT) == 0) 
        {
        
            struct can_frame frame = {
            .id = msg.id,
            .flags = msg.flags,
            .dlc = msg.dlc,
            };
            memcpy(frame.data, msg.data, sizeof(frame.data));

            // Отправляем в CAN контроллер. Передаем ctx в качестве user_data для callback.
            int ret = can_send(can_contexts[0].can_dev, &frame, K_NO_WAIT, can1_tx_callback, 0 );
            if (ret != 0) 
            {            
                k_msgq_put(&can1_tx_msgq, &msg, K_NO_WAIT);            
            }
        }
        can_tx_poll_events[0].state = K_POLL_STATE_NOT_READY;
    } 
    else if (can_tx_poll_events[1].state != K_POLL_STATE_NOT_READY)
    {
        if (k_msgq_get(&can2_tx_msgq, &msg, K_NO_WAIT) == 0) 
        {
        
            struct can_frame frame = {
            .id = msg.id,
            .flags = msg.flags,
            .dlc = msg.dlc,
            };
            memcpy(frame.data, msg.data, sizeof(frame.data));

            // Отправляем в CAN контроллер. Передаем ctx в качестве user_data для callback.
            int ret = can_send(can_contexts[1].can_dev, &frame, K_NO_WAIT, can2_tx_callback, 0 );
            if (ret != 0) 
            {            
                k_msgq_put(&can1_tx_msgq, &msg, K_NO_WAIT);            
            }
        }
        can_tx_poll_events[1].state = K_POLL_STATE_NOT_READY;
    } 
    else if (can_tx_poll_events[2].state != K_POLL_STATE_NOT_READY)
    {
        if (k_msgq_get(&can3_tx_msgq, &msg, K_NO_WAIT) == 0) 
        {
        
            struct can_frame frame = {
            .id = msg.id,
            .flags = msg.flags,
            .dlc = msg.dlc,
            };
            memcpy(frame.data, msg.data, sizeof(frame.data));

            // Отправляем в CAN контроллер. Передаем ctx в качестве user_data для callback.
            int ret = can_send(can_contexts[2].can_dev, &frame, K_NO_WAIT, can3_tx_callback, 0 );
            if (ret != 0) 
            {            
                k_msgq_put(&can1_tx_msgq, &msg, K_NO_WAIT);            
            }
        }
        can_tx_poll_events[2].state = K_POLL_STATE_NOT_READY;
    }

  app_worker_poll_submit(&can_task, can_tx_poll_events,ARRAY_SIZE(can_tx_poll_events), CAN_K_WORK);
    
}




static void _can_rx_handler(struct k_work *_work)
{
    struct can_frame frame;
    system_can_message_t msg;

    // Проверяем RX-очередь CAN 1
    while (k_msgq_get(&can1_rx_msgq, &frame, K_NO_WAIT) == 0) 
    {
        msg.id = frame.id;
        msg.flags = frame.flags;
        msg.dlc = frame.dlc;
        memcpy(msg.data, frame.data, sizeof(msg.data));        
        zbus_chan_pub(&can1_rx_chan, &msg, K_NO_WAIT);
    }

    // Проверяем RX-очередь CAN 2
    while (k_msgq_get(&can2_rx_msgq, &frame, K_NO_WAIT) == 0) 
    {
        msg.id = frame.id;
        msg.flags = frame.flags;
        msg.dlc = frame.dlc;
        memcpy(msg.data, frame.data, sizeof(msg.data));        
        zbus_chan_pub(&can2_rx_chan, &msg, K_NO_WAIT);
    }

    // Проверяем RX-очередь CAN 3
    while (k_msgq_get(&can3_rx_msgq, &frame, K_NO_WAIT) == 0) 
    {
        msg.id = frame.id;
        msg.flags = frame.flags;
        msg.dlc = frame.dlc;
        memcpy(msg.data, frame.data, sizeof(msg.data));        
        zbus_chan_pub(&can3_rx_chan, &msg, K_NO_WAIT);
    }
}

static void can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
    struct can_tx_context *ctx = (struct can_tx_context *)user_data;
    if (ctx == NULL) {
        return;
    }

    // Кладываем сырой фрейм в софтовую очередь приема (безопасно для ISR благодаря K_NO_WAIT)
    if (ctx == &can_contexts[0]) 
    {
        k_msgq_put(&can1_rx_msgq, frame, K_NO_WAIT);
    } 
    else if (ctx == &can_contexts[1]) 
    {
        k_msgq_put(&can2_rx_msgq, frame, K_NO_WAIT);
    } 
    else if (ctx == &can_contexts[2]) 
    {
        k_msgq_put(&can3_rx_msgq, frame, K_NO_WAIT);
    }

    // Передаем задачу на обработку приема в защищенный workqueue
    app_worker_submit(&can_rx_task, CAN_K_WORK);
}



static int dev_can_init(void)
{
    int ret;

    for (int i = 0; i < NUM_CAN_CHANNELS; i++) 
    {
        const struct can_instance_config *cfg = &can_channels[i];
      
        /* ИНИЦИАЛИЗАЦИЯ КОНТЕКСТА: связываем физическое устройство с очередью */
        can_contexts[i].can_dev = cfg->can_device;

        if (!device_is_ready(cfg->can_device)) 
        {
            LOG_ERR("CAN device %s is not ready!", cfg->can_device->name);
            continue;
        }

        if (can_stop(cfg->can_device) != 0) {
            printk("Failed to stop CAN device\n");
        }

        struct can_timing timing = {
            .sjw = 1,
            .prop_seg = 0,
            .phase_seg1 = 11,
            .phase_seg2 = 4,
            .prescaler = 4,
        };

        if (can_set_timing(cfg->can_device, &timing) != 0) {
            LOG_ERR("Failed to set custom CAN timing\n");
        }
        
        ret = can_set_mode(cfg->can_device, CAN_MODE_NORMAL);
        if (ret != 0) {            
            LOG_ERR("Failed to set CAN mode for %s (err %d)", cfg->can_device->name, ret);
            continue;
        }

        struct can_filter std_filter = {
            .flags = 0,
            .id    = 0,
            .mask  = 0,
        };
        
       int filter_id_std = can_add_rx_filter(cfg->can_device, can_rx_callback, &can_contexts[i], &std_filter);
        if (filter_id_std < 0) {
            LOG_ERR("Failed to add standard RX filter for %s (err %d)", cfg->can_device->name, filter_id_std);
        }

        struct can_filter ext_filter = {
            .flags = CAN_FILTER_IDE,
            .id    = 0,
            .mask  = 0,
        };
        
        int filter_id_ext = can_add_rx_filter(cfg->can_device, can_rx_callback, &can_contexts[i], &ext_filter);
        if (filter_id_ext < 0) {
            LOG_ERR("Failed to add extended RX filter for %s (err %d)", cfg->can_device->name, filter_id_ext);
        }
  
        ret = can_start(cfg->can_device);
        if (ret != 0) {            
            LOG_ERR("Failed to start CAN device %s (err %d)", cfg->can_device->name, ret);
            continue;
        }
       
        LOG_INF("CAN device %s successfully initialized and started.", cfg->can_device->name);
    }    

    k_poll_event_init(&can_tx_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &can1_tx_msgq); 
    k_poll_event_init(&can_tx_poll_events[1],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &can2_tx_msgq);
    k_poll_event_init(&can_tx_poll_events[2],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &can3_tx_msgq);

    k_work_poll_init(&can_task, _can_tx_handler);
    app_worker_poll_submit(&can_task, can_tx_poll_events,ARRAY_SIZE(can_tx_poll_events), CAN_K_WORK);
    k_work_init(&can_rx_task, _can_rx_handler);
    return 0;
}

SYS_INIT(dev_can_init, CAN_DEAMON_INIT_LEVEL , CAN_DEAMON_INIT_PRIOR);


SERVER_OUT_QUEUE_ONLY_CHANNEL_DEFINE(CAN1_TX, &can1_tx_msgq, CAN_MSG_DATA);
SERVER_OUT_QUEUE_ONLY_CHANNEL_DEFINE(CAN2_TX, &can2_tx_msgq, CAN_MSG_DATA);
SERVER_OUT_QUEUE_ONLY_CHANNEL_DEFINE(CAN3_TX, &can3_tx_msgq, CAN_MSG_DATA);
//SERVER_IN_CHANNEL_DEFINE(CAN1_TX, &can1_tx_chan, 0, CAN_MSG_DATA);
//SERVER_IN_CHANNEL_DEFINE(CAN2_TX, &can2_tx_chan, 0, CAN_MSG_DATA);
//SERVER_IN_CHANNEL_DEFINE(CAN3_TX, &can3_tx_chan, 0, CAN_MSG_DATA);
SERVER_IN_CHANNEL_DEFINE(CAN1_RX, &can1_rx_chan, 0, CAN_MSG_DATA);
SERVER_IN_CHANNEL_DEFINE(CAN2_RX, &can2_rx_chan, 0, CAN_MSG_DATA);
SERVER_IN_CHANNEL_DEFINE(CAN3_RX, &can3_rx_chan, 0, CAN_MSG_DATA);

SERVER_SETTING_CHANNEL_DEFINE(CAN1_STATE,    &can_state_chan, 0 , BOOL_DATA);
SERVER_SETTING_CHANNEL_DEFINE(CAN2_STATE,    &can_state_chan, 1 , BOOL_DATA);
SERVER_SETTING_CHANNEL_DEFINE(CAN3_STATE,    &can_state_chan, 2 , BOOL_DATA);

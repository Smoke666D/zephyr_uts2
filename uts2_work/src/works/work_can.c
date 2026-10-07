#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

#include "app_worker.h"
#include "system_bus_model.h"
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/iterable_sections.h>

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


static void can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
    // Здесь обрабатываем входящий пакет
    // frame->id  - ID пакета
    // frame->dlc - длина
    // frame->data - данные
}


static void my_tx_callback(const struct device *dev, int error, void *user_data)
{
    // user_data — это произвольный указатель, который вы передали при отправке
    const char *tag = (const char *)user_data;

    if (error != 0) {
        LOG_ERR("Ошибка отправки фрейма [%s] на устройстве %s: err = %d", tag, dev->name, error);
    } else {
        LOG_INF("Фрейм [%s] успешно отправлен на %s!", tag, dev->name);
    }
}

/* 2. Функция, которая отправляет пакет и регистрирует/привязывает этот callback */
int send_my_can_packet(const struct device *can_dev)
{
    struct can_frame frame = {
        .id = 0x123,           // Арбитражный ID
        .flags = 0,            // Стандартный 11-битный ID
        .dlc = 3,              // Длина данных (3 байта)
    };
    
    frame.data[0] = 0xAA;
    frame.data[1] = 0xBB;
    frame.data[2] = 0xCC;

    /* 
     * ЗДЕСЬ ПРОИСХОДИТ "РЕГИСТРАЦИЯ" CALLBACK ДЛЯ ЭТОГО КОНКРЕТНОГО ПАКЕТА:
     * Аргументы can_send:
     * 1. Устройство (can_dev)
     * 2. Указатель на фрейм (&frame)
     * 3. Таймаут ожидания свободного почтового ящика (например, K_MSEC(100))
     * 4. Сам callback (my_tx_callback)
     * 5. Указатель на произвольные пользовательские данные (например, строка "Motor_Data")
     */
    int ret = can_send(can_dev, &frame, K_MSEC(100), my_tx_callback, (void *)"Motor_Data");

    if (ret != 0) {
        LOG_ERR("Не удалось поставить фрейм в очередь TX (err %d)", ret);
        return ret;
    }

    return 0;
}

static int dev_can_init(void)
{
    int ret;

    for (int i = 0; i < NUM_CAN_CHANNELS; i++) 
    {
        const struct can_instance_config *cfg = &can_channels[i];
      
        if (!device_is_ready(cfg->can_device)) 
        {
            LOG_ERR("CAN device %s is not ready!", cfg->can_device->name);
            continue;
        }

        if (can_stop(cfg->can_device) != 0) 
        printk("Failed to stop CAN device\n");
     
    

    /* 2. Задаем свои жесткие кванты и предделитель */
    struct can_timing timing = {
        .sjw = 1,
        .prop_seg = 0,
        .phase_seg1 = 11,
        .phase_seg2 = 4,
        .prescaler = 4, // Подберите под вашу реальную частоту
    };

    if (can_set_timing(cfg->can_device, &timing) != 0) {
        printk("Failed to set custom CAN timing\n");
       
    }
        
        /* Установка режима работы (Normal + One-Shot) */
        ret = can_set_mode(cfg->can_device, CAN_MODE_NORMAL  );
        if (ret != 0)        
        {            
            LOG_ERR("Failed to set CAN mode for %s (err %d)", cfg->can_device->name, ret);
            continue;
        }

   

        struct can_filter std_filter = {
            .flags = 0,             // Без флага EXTENDED означает стандартный ID
            .id    = 0,             // Искомый ID = 0
            .mask  = 0,             // Маска 0 означает "игнорировать все биты ID" (все совпало)
        };
        
        int filter_id_std = can_add_rx_filter(cfg->can_device, can_rx_callback, NULL, &std_filter);
        if (filter_id_std < 0) {
            LOG_ERR("Failed to add standard RX filter for %s (err %d)", cfg->can_device->name, filter_id_std);
        }

        /* Фильтр 2: Прием ВСЕХ расширенных (29-bit) пакетов */
        struct can_filter ext_filter = {
            .flags = CAN_FILTER_IDE, // Взводим флаг Extended ID
            .id    = 0,              // Искомый ID = 0
            .mask  = 0,              // Маска 0 — пропускать всё
        };
        
        int filter_id_ext = can_add_rx_filter(cfg->can_device, can_rx_callback, NULL, &ext_filter);
        if (filter_id_ext < 0) {
            LOG_ERR("Failed to add extended RX filter for %s (err %d)", cfg->can_device->name, filter_id_ext);
        }
  
        ret = can_start(cfg->can_device);
        if (ret != 0)
        {            
            LOG_ERR("Failed to start CAN device %s (err %d)", cfg->can_device->name, ret);
            continue;
        }
       
        LOG_INF("CAN device %s successfully initialized and started.", cfg->can_device->name);
    }    
    return 0;
}

SYS_INIT(dev_can_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);


#define CAN_TASK_STACK_SIZE 2024
#define CAN_TASK_PRIORITY 10

static void can_thread_entry(void *p1, void *p2, void *p3)
{

    k_msleep(2000);
    
   
    
     while (1) 
     {
      
        k_msleep(1000);

        for (int i = 0; i < 1; i++) 
        {
            const struct can_instance_config *cfg = &can_channels[i];
            send_my_can_packet(cfg->can_device);
        }
    }
}

K_THREAD_DEFINE(can_thread, CAN_TASK_STACK_SIZE, can_thread_entry, 
            NULL, NULL, NULL, 
                7, 0, 0);
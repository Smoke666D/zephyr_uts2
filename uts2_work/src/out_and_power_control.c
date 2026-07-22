#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include "system_data_bus.h"
#include <hc595_chain.h>

LOG_MODULE_REGISTER(out_and_power, LOG_LEVEL_INF);

/* ------------------ Настройки порогов защиты ------------------ */

/* Защита по напряжению каналов (АЦП) */
#define FAULT_LOW_THRESHOLD_MV  1500  /* Напряжение выше 1.5В при OUT_LOW считается ошибкой */
#define FAULT_HIGH_THRESHOLD_MV 10000 /* Напряжение ниже 10.0В при OUT_HIGH считается ошибкой */
#define FAULT_UP_STEP           3     
#define FAULT_DOWN_STEP         1     
#define FAULT_THRESHOLD_LIMIT   15    /* Задержка срабатывания защиты каналов: 15 / 3 = 5 мс */

/* Защита по общему току потребления (INA) */
#define FAULT_CURRENT_THRESHOLD_A 2.5  /* Порог отключения по току в Амперах */
#define FAULT_CURRENT_UP_STEP     5    /* Скорость накопления ошибки тока */
#define FAULT_CURRENT_DOWN_STEP   1    /* Скорость остывания */
#define FAULT_CURRENT_LIMIT       30   /* Задержка защиты по току: 30 / 5 = 6 мс (фильтрует пусковые токи) */

/* -------------------------------------------------------------- */

/* Объявление внешних каналов Zbus */
ZBUS_CHAN_DECLARE(adc_data_chan);
ZBUS_CHAN_DECLARE(ina_batch_chan);

/* 1. Объявляем подписчика Zbus для управления выходами */
ZBUS_SUBSCRIBER_DEFINE(hc595_sub, 8);

/* 2. Объявляем канал Zbus для управления выходами */
ZBUS_CHAN_DEFINE(hc595_chan,
                 struct hc595_channels_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(hc595_sub),
                 {0}
);

/* Таблица маппинга каналов АЦП */
static const AIN_MUX_CHANNEL_NUMBER ao_to_adc_map[LOW_CUR_DRIVER_COUNT] = {
    AO1,  AO2,  AO3,  AO4,  AO5,  AO6,
    AO7,  AO8,  AO9,  AO10, AO11, AO12,
    AO13, AO14, AO15, AO16, AO17, AO18
};

/**
 * @brief Функция расчета маски для сдвигового регистра.
 */
static void hc595_calculate_mask(const struct hc595_channels_msg *msg, uint8_t *tx_data, size_t len)
{
    if (len < 5) {
        return;
    }

    memset(tx_data, 0, len);

    /* Заполняем управляющие сигналы LIN */
    tx_data[0] = ((uint8_t)msg->lin_pb[0] << 4) |
                 ((uint8_t)msg->lin_pb[1] << 5) |
                 ((uint8_t)msg->lin_pb[2] << 6) |
                 ((uint8_t)msg->lin_pb[3] << 7);

    static const uint8_t state_to_bits[] = {0, 2, 1};

    /* Заполняем каналы слаботочных драйверов */
    for (int i = 0; i < LOW_CUR_DRIVER_COUNT; i++) {
        uint8_t chip_index = 4 - (i / 4);
        uint8_t bit_indx = (i % 4) * 2;
        LOW_CUR_DRIVER_STATE state = msg->low_cur_driver_channels[i];

        if (state < 3) {
            tx_data[chip_index] |= (state_to_bits[state] << bit_indx);
        }
    }
}

/* Поток периодического диспетчера и диагностики */
void hc595_dispatcher_thread(void *p1, void *p2, void *p3)
{
    const struct device *hc595_dev = DEVICE_DT_GET(DT_NODELABEL(hc595_chain));

    if (!device_is_ready(hc595_dev)) {
        LOG_ERR("74HC595 device not ready in dispatcher thread");
        return;
    }

    /* На старте выходы аппаратно выключены */
    hc595_chain_output_enable(hc595_dev, false);

    struct hc595_channels_msg current_msg = {0};  /* Желаемое состояние из Zbus */
    struct hc595_channels_msg applied_msg = {0};  /* Текущее физически примененное состояние */
    
    uint16_t fault_counters[LOW_CUR_DRIVER_COUNT] = {0};
    uint16_t overcurrent_counter = 0;
    
    uint8_t last_tx_data[5] = {0};
    
    bool system_fault = false;
    bool first_run = true;

    while (1) {
        k_msleep(1);

        /* Если система заблокирована по аварии, игнорируем работу */
        if (system_fault) {
            continue;
        }

        bool diagnostic_fault_detected = false;

        /* ================================================================== */
        /* ЭТАП 1: ЗАЩИТА ПО ТОКУ ПОТРЕБЛЕНИЯ (INA)                          */
        /* ================================================================== */
        struct ina_batch_msg ina_msg;
        int ina_err = zbus_chan_read(&ina_batch_chan, &ina_msg, K_NO_WAIT);

        if (ina_err == 0) {
            /* Извлекаем ток по символьному псевдониму VDOUT_PWR */
            double current_amps = ina_msg.sensors[VDOUT_PWR].current;
            bool overcurrent = (current_amps > FAULT_CURRENT_THRESHOLD_A);

            if (overcurrent) {
                overcurrent_counter += FAULT_CURRENT_UP_STEP;
                if (overcurrent_counter >= FAULT_CURRENT_LIMIT) {
                    overcurrent_counter = FAULT_CURRENT_LIMIT;
                    LOG_ERR("Emergency! Overcurrent detected on VDOUT_PWR. Current: %.3f A (Threshold: %.1f A)", 
                            current_amps, FAULT_CURRENT_THRESHOLD_A);
                    diagnostic_fault_detected = true;
                }
            } else {
                if (overcurrent_counter > FAULT_CURRENT_DOWN_STEP) {
                    overcurrent_counter -= FAULT_CURRENT_DOWN_STEP;
                } else {
                    overcurrent_counter = 0;
                }
            }
        } else {
            LOG_WRN_ONCE("INA sensor channel not ready yet. Skipping current monitoring.");
        }

        /* ================================================================== */
        /* ЭТАП 2: ЗАЩИТА ПО НАПРЯЖЕНИЮ КАНАЛОВ (АЦП)                        */
        /* ================================================================== */
        struct adc_data_msg adc_msg;
        int adc_err = zbus_chan_read(&adc_data_chan, &adc_msg, K_NO_WAIT);

        if (adc_err == 0) {
            /* Выполняем проверку каналов относительно физически примененного состояния */
            for (int i = 0; i < LOW_CUR_DRIVER_COUNT; i++) {
                LOW_CUR_DRIVER_STATE state = applied_msg.low_cur_driver_channels[i];
                AIN_MUX_CHANNEL_NUMBER adc_chan = ao_to_adc_map[i];
                uint32_t voltage_mv = adc_msg.channels_mv[adc_chan];
                
                bool channel_error = false;

                if (state == OUT_LOW) {
                    if (voltage_mv > FAULT_LOW_THRESHOLD_MV) {
                        channel_error = true;
                    }
                } else if (state == OUT_HIGH) {
                    if (voltage_mv < FAULT_HIGH_THRESHOLD_MV) {
                        channel_error = true;
                    }
                }

                /* Интегратор ошибок каналов */
                if (channel_error) {
                    fault_counters[i] += FAULT_UP_STEP;
                    if (fault_counters[i] >= FAULT_THRESHOLD_LIMIT) {
                        fault_counters[i] = FAULT_THRESHOLD_LIMIT;
                        LOG_ERR("Emergency! Channel %d (ADC %d) failed. State: %d, Voltage: %u mV", 
                                i + 1, adc_chan, state, voltage_mv);
                        diagnostic_fault_detected = true;
                    }
                } else {
                    if (fault_counters[i] > FAULT_DOWN_STEP) {
                        fault_counters[i] -= FAULT_DOWN_STEP;
                    } else {
                        fault_counters[i] = 0;
                    }
                }
            }
        } else {
            LOG_WRN_ONCE("ADC data channel not ready yet. Skipping voltage diagnostics.");
        }

        /* ================================================================== */
        /* ЭТАП 3: РЕАКЦИЯ НА АВАРИЮ                                          */
        /* ================================================================== */
        if (diagnostic_fault_detected) {
            system_fault = true;
            
            /* Экстренное физическое отключение OE */
            hc595_chain_output_enable(hc595_dev, false);
            
            /* Записываем безопасное состояние (все нули) в регистры */
            uint8_t safe_tx[5] = {0};
            hc595_chain_write(hc595_dev, safe_tx, sizeof(safe_tx));
            
            LOG_ERR("System globally locked due to hardware protection trip.");
            continue;
        }

        /* ================================================================== */
        /* ЭТАП 4: ПРИМЕНЕНИЕ НОВЫХ СОСТОЯНИЙ                                 */
        /* ================================================================== */
        bool state_changed = false;
        while (zbus_sub_wait(&hc595_sub, &chan, K_NO_WAIT) == 0) {
            if (chan == &hc595_chan) {
                zbus_chan_read(&hc595_chan, &current_msg, K_NO_WAIT);
                state_changed = true;
            }
        }

        if (state_changed || first_run) {
            uint8_t tx_data[5] = {0};

            hc595_calculate_mask(&current_msg, tx_data, sizeof(tx_data));

            /* Отправляем по SPI только при физическом изменении битовой маски */
            if (first_run || memcmp(tx_data, last_tx_data, sizeof(tx_data)) != 0) {
                int err = hc595_chain_write(hc595_dev, tx_data, sizeof(tx_data));
                if (err) {
                    LOG_ERR("Failed to write registers: %d", err);
                } else {
                    memcpy(last_tx_data, tx_data, sizeof(tx_data));
                    
                    /* Фиксируем успешно примененное состояние для будущих проверок */
                    memcpy(&applied_msg, &current_msg, sizeof(applied_msg));
                    
                    if (first_run) {
                        /* После первой успешной инициализации включаем выходы */
                        hc595_chain_output_enable(hc595_dev, true);
                        first_run = false;
                    }
                }
            }
        }
    }
}

/* Регистрация потока диспетчера */
K_THREAD_DEFINE(hc595_dispatcher_thread_id, 2048, hc595_dispatcher_thread, NULL, NULL, NULL,
                K_PRIO_PREEMPT(8), 0, 0);
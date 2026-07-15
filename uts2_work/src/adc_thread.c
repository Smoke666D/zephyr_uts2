#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <stm32h7xx_ll_tim.h>
#include <stm32h7xx_ll_dma.h>
#include <stm32h7xx_ll_adc.h>
#include <stm32h7xx_ll_gpio.h>
#include <stm32h7xx_ll_rcc.h>
#include <stm32h7xx_ll_bus.h>
#include <zephyr/cache.h>

#include "adc_thread.h"


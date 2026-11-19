#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_tim.h>
#include <stm32_ll_gpio.h>
#include "system_bus_model.h"
#include "system_define.h"
#include <zephyr/zbus/zbus.h>



typedef enum{
    HSE_TEST_RESULT = 0,
    LSE_TEST_RESULT = 1
} self_test_reg_add;



/* Структура с результатами заводского теста */
typedef struct 
{
    hse_diag_status_t hse_status;
    lse_diag_status_t lse_status;

    /* Маски битых пинов для портов A, B, C, D (1 бит = 1 пин) */
    uint16_t short_to_gnd[4];      /* Закорочены на землю */
    uint16_t short_to_vdd[4];      /* Закорочены на питание 3.3В */
    uint16_t bridge_with_next[4];   /* Закорочены со следующим соседним выводом */
} factory_diag_report_t;

typedef enum {
    REF_CLOCK_LSE = 0, /* Внешний часовой кварц (32.768 кГц) */
    REF_CLOCK_LSI = 1  /* Внутренний RC-генератор (~32 кГц, неубиваемый) */
} ref_clock_source_t;



static factory_diag_report_t g_diag;





/*
 * Измеряет частоту входного сигнала на TIM5_CH4 относительно таймера.
 * Возвращает количество тиков TIM5 за 1 период эталонного сигнала.
 */
static uint32_t capture_ref_ticks(ref_clock_source_t ref)
{
    /* 1. Включаем выбранный генератор */
    if (ref == REF_CLOCK_LSI) 
    {
        LL_RCC_LSI_Enable();
        uint32_t t = 10000;
        while (!LL_RCC_LSI_IsReady() && --t) {}
        if (t == 0) return 0;
    } 
    else 
    {
        if (!LL_RCC_LSE_IsReady()) return 0;
    }

    /* 2. Включаем тактирование TIM5 */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM5);

    /* 
     * 3. Переключаем мультиплексор TIM5_OR:
     * 0b01 = LSI, 0b10 = LSE
     */
    if (ref == REF_CLOCK_LSI) 
    {
        MODIFY_REG(TIM5->OR, TIM_OR_TI4_RMP, TIM_OR_TI4_RMP_0); /* LSI */
    } 
    else 
    {
        MODIFY_REG(TIM5->OR, TIM_OR_TI4_RMP, TIM_OR_TI4_RMP_1); /* LSE */
    }

    /* 4. Настраиваем канал 4 на захват фронта */
    LL_TIM_IC_SetActiveInput(TIM5, LL_TIM_CHANNEL_CH4, LL_TIM_ACTIVEINPUT_DIRECTTI);
    LL_TIM_IC_SetPrescaler(TIM5, LL_TIM_CHANNEL_CH4, LL_TIM_ICPSC_DIV1);
    LL_TIM_IC_SetPolarity(TIM5, LL_TIM_CHANNEL_CH4, LL_TIM_IC_POLARITY_RISING);
    LL_TIM_CC_EnableChannel(TIM5, LL_TIM_CHANNEL_CH4);
    
    LL_TIM_SetCounter(TIM5, 0);
    LL_TIM_EnableCounter(TIM5);
    LL_TIM_ClearFlag_CC4(TIM5);

    /* Захват 1 */
    uint32_t timeout = 200000;
    while (!LL_TIM_IsActiveFlag_CC4(TIM5) && --timeout) {}
    if (timeout == 0) goto cleanup;

    uint32_t t1 = LL_TIM_IC_GetCaptureCH4(TIM5);
    LL_TIM_ClearFlag_CC4(TIM5);

    /* Захват 2 (ровно через 1 период эталона) */
    timeout = 200000;
    while (!LL_TIM_IsActiveFlag_CC4(TIM5) && --timeout) {}
    if (timeout == 0) goto cleanup;

    uint32_t t2 = LL_TIM_IC_GetCaptureCH4(TIM5);
    LL_TIM_ClearFlag_CC4(TIM5);

    LL_TIM_DisableCounter(TIM5);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM5);

    return (t2 - t1);

cleanup:
    LL_TIM_DisableCounter(TIM5);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM5);
    return 0;
}


/* ============================================================================
 * ТЕСТ ЧАСОВОГО КВАРЦА LSE (Пока процессор работает от HSI 16 МГц):
 * ============================================================================ */
void test_lse_against_hsi(void)
{
    /* Включаем LSE */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_PWR_EnableBkUpAccess();
    LL_RCC_LSE_Enable();

    uint32_t timeout = 500000;
    while (!LL_RCC_LSE_IsReady() && --timeout) {}

    if (timeout == 0) 
    {
        g_diag.lse_status = LSE_STATUS_ERR_PRESENT;
        return;
    }

    /* 
     * Процессор сейчас работает на HSI (частота шины TIM5 = 16 МГц = 16000000 Гц).
     * Ожидаемое число тиков для LSE (32768 Гц):
     * 16000000 / 32768 ≈ 488 тиков.
     */
    uint32_t ticks = capture_ref_ticks(REF_CLOCK_LSE);

    /* Если тики попали в 450 .. 520 (около 32.7 кГц) — часовой кварц живой и точный! */
    if (ticks >= 450 && ticks <= 520) 
    {
        g_diag.lse_status = LSE_STATUS_OK;
    } 
    else 
    {
        g_diag.lse_status = LSE_STATUS_ERR_FREQ; /* Частота сорвана / не тот кварц */
    }
}


hse_diag_status_t verify_hse_nominal(void)
{
    uint32_t ticks = 0;

    /* Выбираем лучший доступный эталон */
    if (g_diag.lse_status == LSE_STATUS_OK ) 
    {
        /* Вариант А: По сверхточному часовому кварцу LSE */
        ticks = capture_ref_ticks(REF_CLOCK_LSE);
        
        /* При 72 МГц от 16 МГц кварца должно быть ~2197 тиков */
        if (ticks >= 2100 && ticks <= 2300) 
        {
            g_diag.hse_status = HSE_STATUS_OK;          
        } 
        else 
        {
            g_diag.hse_status = HSE_STATUS_ERR_FREQ;           
        }         
    } 
    else
     {
        /* Вариант Б (РЕЗЕРВНЫЙ): LSE мертв, используем неубиваемый внутренний LSI! */
        ticks = capture_ref_ticks(REF_CLOCK_LSI);
        /* 
         * LSI ~32 кГц. При 72 МГц тиков будет около 2250 (допуск 1900 .. 2500)
         * А при кварце 8 МГц тиков будет около 1125 (допуск 950 .. 1250)
         */
        if (ticks >= 1900 && ticks <= 2500) 
        {
            g_diag.hse_status = HSE_STATUS_OK;
        } 
        else 
        {
            g_diag.hse_status = HSE_STATUS_ERR_FREQ; 
        }         
    }
    return (g_diag.hse_status);
}


static int early_hse_try_switch(void)
{
    /* 1. Включаем внешний кварц HSE и ждем его готовности с жестким таймаутом */
    LL_RCC_HSE_Enable();

    uint32_t cycles = 0;
    const uint32_t max_cycles = 500000; /* ~30-50 мс ожидания максимум */

    while (!LL_RCC_HSE_IsReady()) 
    {
        cycles++;
        if (cycles >= max_cycles) 
        {
            /* КВАРЦ МЕРТВ! Выключаем HSE, записываем ошибку и остаемся жить на HSI! */
            LL_RCC_HSE_Disable();
            g_diag.hse_status = HSE_STATUS_ERR_TIMEOUT;
            return 0; /* Возвращаем 0, чтобы Zephyr спокойно продолжил загрузку! */
        }
    }

    if (verify_hse_nominal() == HSE_STATUS_OK)
    {
        
        /* 2. Кварц жив! Чтобы переключить вход PLL с HSI на HSE, нужно на миг перейти на прямой HSI */
        LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
        while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {}

        /* 3. Выключаем PLL, меняем его источник на HSE (делители M=8, N=72, P=2, Q=3 уже стоят!) */
        LL_RCC_PLL_Disable();
        while (LL_RCC_PLL_IsReady()) {}

        LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_8, 72, LL_RCC_PLLP_DIV_2);

        /* 4. Включаем PLL обратно от внешнего кварца HSE */
        LL_RCC_PLL_Enable();
        cycles = 0;
        while (!LL_RCC_PLL_IsReady()) 
        {
            if (++cycles >= max_cycles) 
            {
                /* Если вдруг PLL не завелся от кварца — аварийно возвращаем HSI */
                LL_RCC_PLL_Disable();
                LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_8, 72, LL_RCC_PLLP_DIV_2);
                LL_RCC_PLL_Enable();
                while (!LL_RCC_PLL_IsReady()) {}
                LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
                g_diag.hse_status = HSE_STATUS_ERR_PLL_LOCK;
                return 0;
            }
        }

        /* 5. Переключаем ядро обратно на PLL (теперь он работает от внешнего HSE!) */
        LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
        while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {}

        /* В    ключаем аппаратную защиту CSS (на случай, если кварц отвалится позже в работе) */
        LL_RCC_HSE_EnableCSS();

        g_diag.hse_status = HSE_STATUS_OK;
    }
    return 0;
}


/* Регистрируем выполнение функции ДО старта main()! */

static int pre_kernel_factory_test(void)
{
    /* 1. Включаем тактирование всех портов GPIO */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA |
                             LL_AHB1_GRP1_PERIPH_GPIOB |
                             LL_AHB1_GRP1_PERIPH_GPIOC |
                             LL_AHB1_GRP1_PERIPH_GPIOD);

   /* 2. Тестируем кварцы */
    test_lse_against_hsi();
    early_hse_try_switch();

    /* 3. Тестируем ножки портов A, B, C, D */
   // test_gpio_port(GPIOA, 0);
   // test_gpio_port(GPIOB, 1);
   // test_gpio_port(GPIOC, 2);
   // test_gpio_port(GPIOD, 3);

    return 0;
}


//SYS_INIT(pre_kernel_factory_test, PRE_KERNEL_2, 0);



typedef struct selt_test_res_t
{
    uint32_t value[10];
} selt_test_res_t;

/* Канал Zbus */
ZBUS_CHAN_DEFINE(self_test_chan,
                 selt_test_res_t,
                 NULL, 
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);


static int self_test_init(void)
{
    selt_test_res_t test_res;
    test_res.value[LSE_TEST_RESULT] = g_diag.lse_status;
    test_res.value[HSE_TEST_RESULT] = g_diag.hse_status;
    zbus_chan_pub(&self_test_chan, &test_res, K_MSEC(2)); 
    return 0;
}

SYS_INIT(self_test_init,SELF_TEST_INIT_LEVEL,SELF_TEST_INIT_PRIOR);

SERVER_IN_CHANNEL_DEFINE(HSE_STATUS,      &self_test_chan,    HSE_TEST_RESULT,   UINT_DATA);
SERVER_IN_CHANNEL_DEFINE(LSE_STATUS,      &self_test_chan,    LSE_TEST_RESULT,   UINT_DATA);

#include "clock.h"
#include "stm32f4xx_hal.h"

static clock_status_t s_status = CLOCK_OK;

/* Make HSI the system clock (needed before the PLL can be reconfigured: HAL
   refuses to touch the PLL while it is the SYSCLK source, which is exactly the
   state the ST bootloader leaves behind). Register level, bounded wait. */
static bool switch_to_hsi(void)
{
    RCC->CR |= RCC_CR_HSION;
    for (uint32_t i = 0; i < 1000000u && !(RCC->CR & RCC_CR_HSIRDY); i++) {
    }
    if (!(RCC->CR & RCC_CR_HSIRDY)) {
        return false;
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_HSI) {
            /* Prescalers back to /1 so HSI-based timings are sane. */
            RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
            return true;
        }
    }
    return false;
}

static void art_enable(void)
{
    /* Reset then enable the ART accelerator caches + prefetch. Caches may
       only be reset while disabled. */
    FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR |= FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
}

clock_status_t clock_init_96mhz(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
        if (!switch_to_hsi()) {
            s_status = CLOCK_ERR_SWITCH;
            SystemCoreClockUpdate();
            return s_status;
        }
        SystemCoreClockUpdate();
        HAL_InitTick(TICK_INT_PRIORITY);
    }
    /* PLL must be off before HAL can reprogram it. */
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) {
    }

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 4;
    osc.PLL.PLLN = 96;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        s_status = (RCC->CR & RCC_CR_HSERDY) ? CLOCK_ERR_PLL : CLOCK_ERR_HSE;
        SystemCoreClockUpdate();
        HAL_InitTick(TICK_INT_PRIORITY);
        art_enable();
        return s_status;
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) {
        s_status = CLOCK_ERR_SWITCH;
        SystemCoreClockUpdate();
        HAL_InitTick(TICK_INT_PRIORITY);
        art_enable();
        return s_status;
    }

    art_enable();
    SystemCoreClockUpdate();
    s_status = CLOCK_OK;
    return s_status;
}

clock_status_t clock_last_status(void) { return s_status; }

const char *clock_status_str(clock_status_t s)
{
    switch (s) {
    case CLOCK_OK: return "ok";
    case CLOCK_ERR_HSE: return "hse_fail";
    case CLOCK_ERR_PLL: return "pll_fail";
    case CLOCK_ERR_SWITCH: return "switch_fail";
    default: return "?";
    }
}

bool clock_hse_ready(void) { return (RCC->CR & RCC_CR_HSERDY) != 0u; }
bool clock_sysclk_is_pll(void) { return (RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL; }
bool clock_pll_src_is_hse(void) { return (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) != 0u; }
uint32_t clock_pclk1_hz(void) { return HAL_RCC_GetPCLK1Freq(); }
uint32_t clock_pclk2_hz(void) { return HAL_RCC_GetPCLK2Freq(); }

uint32_t clock_tim_apb1_hz(void)
{
    uint32_t p = HAL_RCC_GetPCLK1Freq();
    return ((RCC->CFGR & RCC_CFGR_PPRE1) & RCC_CFGR_PPRE1_2) ? 2u * p : p;
}

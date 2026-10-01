#include "board.h"
#include "stm32_hal.h"
#include "stm32c5xx_hal_iwdg.h"
#include "stm32c5xx_hal_dbgmcu.h"
static hal_tim_handle_t timebase;
static hal_iwdg_handle_t watchdog;

int board_init(void)
{
    /* 当前 CubeMX2 配置 APB1 不分频，TIM2 内核时钟等于 PCLK1。 */
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    if (HAL_RCC_GetPCLK1Prescaler() != HAL_RCC_PCLK_PRESCALER1 || clock % 1000000U != 0U) return -1;
    if (HAL_TIM_Init(&timebase, HAL_TIM2) != HAL_OK) return -2;
    HAL_RCC_TIM2_EnableClock();
    hal_tim_config_t config = {0};
    config.prescaler = clock / 1000000U - 1U;
    config.counter_mode = HAL_TIM_COUNTER_UP;
    config.period = UINT32_MAX;
    config.clock_sel.clock_source = HAL_TIM_CLK_INTERNAL;
    if (HAL_TIM_SetConfig(&timebase, &config) != HAL_OK) return -3;
    if (HAL_TIM_EnableUpdateGeneration(&timebase) != HAL_OK) return -4;
    return HAL_TIM_Start(&timebase) == HAL_OK ? 0 : -5;
}
uint32_t board_micros(void) { return HAL_TIM_GetCounter(&timebase); }
int board_watchdog_start(void)
{
    /* TIM6 尚未开启时初始化，LSI 由 IWDG 硬件自动开启。 */
    if (HAL_IWDG_Init(&watchdog, HAL_IWDG1) != HAL_OK) return -1;
    /* 调试器暂停核心时冻结看门狗，正常运行仍由有效音频块喂狗。 */
    HAL_DBGMCU_IWDG_Freeze();
    return HAL_IWDG_Start(&watchdog, 0U, 2000U, 0U) == HAL_OK ? 0 : -2;
}
void board_watchdog_feed(void) { (void)HAL_IWDG_Refresh(&watchdog); }

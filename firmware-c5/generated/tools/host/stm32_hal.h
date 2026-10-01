/* 仅用于 PC 测试：替换外设调用，不替换 app 中的算法及协议逻辑。 */
#ifndef HOST_HAL_H
#define HOST_HAL_H
#include <stdint.h>
#define HAL_OK 0
#define HAL_DAC_CHANNEL_1 1
typedef int hal_adc_handle_t;
typedef int hal_dac_handle_t;
typedef int hal_tim_handle_t;
typedef int hal_uart_handle_t;
typedef int hal_dac_channel_t;
typedef int hal_uart_rx_event_types_t;
static inline uint32_t __get_PRIMASK(void) { return 0; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t v) { (void)v; }
static inline void __DMB(void) {}
uint32_t HAL_GetTick(void);
int HAL_ADC_Start(hal_adc_handle_t *h);
int HAL_ADC_Calibrate(hal_adc_handle_t *h);
int HAL_ADC_REG_StartConv_DMA(hal_adc_handle_t *h, uint8_t *p, uint32_t n);
int HAL_DAC_StartChannel_DMA(hal_dac_handle_t *h, hal_dac_channel_t c, void *p, uint32_t n);
int HAL_DAC_StopChannel_DMA(hal_dac_handle_t *h, hal_dac_channel_t c);
int HAL_TIM_Start(hal_tim_handle_t *h);
int HAL_TIM_Stop(hal_tim_handle_t *h);
int HAL_UART_Receive_IT(hal_uart_handle_t *h, uint8_t *p, uint32_t n);
int HAL_UART_Transmit_DMA(hal_uart_handle_t *h, uint8_t *p, uint32_t n);
int HAL_UART_AbortTransmit(hal_uart_handle_t *h);
int HAL_UART_AbortReceive(hal_uart_handle_t *h);
hal_adc_handle_t *mx_adc1_gethandle(void);
hal_dac_handle_t *mx_dac1_gethandle(void);
hal_tim_handle_t *mx_tim6_gethandle(void);
hal_uart_handle_t *mx_usart2_uart_gethandle(void);
#endif

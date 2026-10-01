/* 虚拟 48 kHz DMA 和 115200 8N1 UART，用于调用真实主循环代码。 */
#include "stm32_hal.h"
#include "audio.h"
#include "protocol_uart.h"
#include <string.h>
static int handle;
static uint16_t *adc_buffer;
static uint8_t *receive_byte;
static uint32_t tick, tx_end, tx_length;
static uint8_t transmit[1024];
static uint32_t total_bytes, counts[9], feeds, stopped;
void HAL_ADC_REG_DataTransferHalfCallback(hal_adc_handle_t *h);
void HAL_ADC_REG_DataTransferCpltCallback(hal_adc_handle_t *h);
void HAL_DAC_ConvHalfCpltCallback(hal_dac_handle_t *h, hal_dac_channel_t c);
void HAL_DAC_ConvCpltCallback(hal_dac_handle_t *h, hal_dac_channel_t c);
void HAL_UART_TxCpltCallback(hal_uart_handle_t *h);
void HAL_UART_RxCpltCallback(hal_uart_handle_t *h, uint32_t n, hal_uart_rx_event_types_t e);
void HAL_ADC_ErrorCallback(hal_adc_handle_t *h);
hal_adc_handle_t *mx_adc1_gethandle(void) { return &handle; }
hal_dac_handle_t *mx_dac1_gethandle(void) { return &handle; }
hal_tim_handle_t *mx_tim6_gethandle(void) { return &handle; }
hal_uart_handle_t *mx_usart2_uart_gethandle(void) { return &handle; }
int HAL_ADC_Start(hal_adc_handle_t *h) { (void)h; return HAL_OK; }
int HAL_ADC_Calibrate(hal_adc_handle_t *h) { (void)h; return HAL_OK; }
int HAL_ADC_REG_StartConv_DMA(hal_adc_handle_t *h, uint8_t *p, uint32_t n) {
    (void)h; (void)n; adc_buffer = (uint16_t *)p; return HAL_OK;
}
int HAL_DAC_StartChannel_DMA(hal_dac_handle_t *h, hal_dac_channel_t c, void *p, uint32_t n) {
    (void)h; (void)c; (void)p; (void)n; return HAL_OK;
}
int HAL_DAC_StopChannel_DMA(hal_dac_handle_t *h, hal_dac_channel_t c) {
    (void)h; (void)c; ++stopped; return HAL_OK;
}
int HAL_TIM_Start(hal_tim_handle_t *h) { (void)h; return HAL_OK; }
int HAL_TIM_Stop(hal_tim_handle_t *h) { (void)h; return HAL_OK; }
int HAL_UART_Receive_IT(hal_uart_handle_t *h, uint8_t *p, uint32_t n) {
    (void)h; (void)n; receive_byte = p; return HAL_OK;
}
int HAL_UART_Transmit_DMA(hal_uart_handle_t *h, uint8_t *p, uint32_t n) {
    (void)h; memcpy(transmit,p,n); tx_length=n;
    /* 每字节 10 bit，取毫秒上限以模拟真实占线时间。 */
    tx_end=tick+(n*10000U+115199U)/115200U;
    total_bytes+=n; if (p[3]<9) ++counts[p[3]]; return HAL_OK;
}
int HAL_UART_AbortTransmit(hal_uart_handle_t *h) { (void)h; tx_end=0; return HAL_OK; }
int HAL_UART_AbortReceive(hal_uart_handle_t *h) { (void)h; return HAL_OK; }
uint32_t HAL_GetTick(void) { return tick; }
uint32_t board_micros(void) { return tick*1000U; }
void board_watchdog_feed(void) { ++feeds; }
void host_tick(uint32_t t) {
    tick=t;
    if (tx_end && tick>=tx_end) { tx_end=0; HAL_UART_TxCpltCallback(&handle); }
}
void host_block(const uint16_t *samples, uint32_t half) {
    memcpy(adc_buffer+half*512U,samples,1024U);
    if (half==0) {
        HAL_DAC_ConvHalfCpltCallback(&handle,1);
        HAL_ADC_REG_DataTransferHalfCallback(&handle);
    } else {
        HAL_DAC_ConvCpltCallback(&handle,1);
        HAL_ADC_REG_DataTransferCpltCallback(&handle);
    }
}
void host_receive(const uint8_t *bytes, uint32_t n) {
    for (uint32_t i=0;i<n;++i) { *receive_byte=bytes[i]; HAL_UART_RxCpltCallback(&handle,1,0); }
}
uint32_t host_copy_tx(uint8_t *p) { memcpy(p,transmit,tx_length); return tx_length; }
uint32_t host_count(uint32_t type) { return counts[type]; }
uint32_t host_total_bytes(void) { return total_bytes; }
uint32_t host_feeds(void) { return feeds; }
uint32_t host_stopped(void) { return stopped; }
void host_adc_fault(void) { HAL_ADC_ErrorCallback(&handle); }

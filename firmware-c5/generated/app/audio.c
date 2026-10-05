#include "audio.h"
#include "board.h"
#include "generator.h"
#include "dsp.h"
#include "measurement.h"
#include "calibration.h"

#include "mx_adc1.h"
#include "mx_dac1.h"
#include "mx_tim6.h"
#include "stm32_hal.h"
#include <math.h>
#include <string.h>

/* DMA 正在写入的半缓冲不能读取；主循环只处理已完成的另一半。 */
static uint16_t adc_samples[AUDIO_DMA_SAMPLES] __attribute__((aligned(4)));
static uint16_t dac_samples[AUDIO_DMA_SAMPLES] __attribute__((aligned(4)));
static volatile uint32_t completed_seq;
static volatile uint32_t latest_adc_half;
static volatile uint32_t dac_seq;
static volatile uint32_t latest_dac_half;
static volatile uint32_t peripheral_fault;
static volatile uint32_t adc_errors;
static uint32_t handled_dac_seq;
static uint32_t dac_refill_misses;
static uint8_t running;
/* DMA 数据先复制到独立快照，后续 FFT 不再读取可能被覆盖的 DMA 半区。 */
/* 借用FFT区的块暂存，按主循环执行顺序复用，不增加4 KiB独立工作数组。 */
#define adc_block (dsp_adc_workspace())
#define dac_block (dsp_dac_workspace())
static volatile uint32_t dac_errors;
static uint32_t handled_seq;
static uint32_t lost_blocks;
static uint32_t clipped_samples;
static uint32_t worst_block_us;
static float dc_prev_input;
static float dc_prev_output;
static float rms_dbfs = -120.0f;
static float peak_dbfs = -120.0f;
static audio_mode_t current_mode = AUDIO_MODE_IDLE;
static volatile audio_mode_t requested_mode = AUDIO_MODE_IDLE;
static uint16_t waveform_pairs[AUDIO_WAVEFORM_GROUPS * 2U];
static uint32_t waveform_first_sample_index;
static uint8_t waveform_valid;

/* 中断仅记录已完成的半缓冲及其序号，不在中断内做浮点运算。 */
static void mark_half(uint32_t half)
{
    ++completed_seq;
    latest_adc_half = half;
}

/* 生成的 HAL2 配置未启用动态回调注册，因此直接实现同名回调。 */
void HAL_ADC_REG_DataTransferHalfCallback(hal_adc_handle_t *hadc)
{
    (void)hadc;
    mark_half(0);
}

void HAL_ADC_REG_DataTransferCpltCallback(hal_adc_handle_t *hadc)
{
    (void)hadc;
    mark_half(1);
}

void HAL_DAC_ErrorCallback(hal_dac_handle_t *hdac)
{
    (void)hdac;
    ++dac_errors;
    peripheral_fault |= 4U;
}

void HAL_ADC_ErrorCallback(hal_adc_handle_t *hadc)
{
    (void)hadc;
    ++adc_errors;
    peripheral_fault |= 2U;
}
void HAL_DAC_ConvHalfCpltCallback(hal_dac_handle_t *hdac, hal_dac_channel_t channel)
{
    (void)hdac; (void)channel;
    latest_dac_half = 0U;
    ++dac_seq;
}
void HAL_DAC_ConvCpltCallback(hal_dac_handle_t *hdac, hal_dac_channel_t channel)
{
    (void)hdac; (void)channel;
    latest_dac_half = 1U;
    ++dac_seq;
}

static void service_output(void)
{
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    uint32_t seq = dac_seq, half = latest_dac_half;
    __set_PRIMASK(irq_state);
    if (seq == handled_dac_seq) return;
    /* 超过一半区表示输出已重放过期数据，停触发并让 IWDG 重启到关闭状态。 */
    if ((uint32_t)(seq - handled_dac_seq) != 1U) {
        ++dac_refill_misses;
        peripheral_fault |= 8U;
        return;
    }
    generator_render_at(dac_block, AUDIO_HALF_SAMPLES, (seq+1U)*AUDIO_HALF_SAMPLES);
    if (dac_seq != seq) {
        ++dac_refill_misses;
        peripheral_fault |= 8U;
        return;
    }
    memcpy(dac_samples + half * AUDIO_HALF_SAMPLES, dac_block, AUDIO_HALF_SAMPLES*sizeof(uint16_t));
    if (dac_seq != seq) {
        ++dac_refill_misses;
        peripheral_fault |= 8U;
    }
    handled_dac_seq = seq;
}

int audio_start(void)
{
    hal_adc_handle_t *adc = mx_adc1_gethandle();
    hal_dac_handle_t *dac = mx_dac1_gethandle();
    hal_tim_handle_t *timer = mx_tim6_gethandle();

    generator_init();
    measurement_init();
    calibration_init();
    if (dsp_init() != 0) return -6;

    /* 上电预填 12 位 DAC 中点 2048；必须收到独立启音命令才产生交流。 */
    for (uint32_t i = 0; i < AUDIO_DMA_SAMPLES; ++i) {
        dac_samples[i] = 2048U;
    }

    /* 按 ST HAL2 顺序，先激活 ADC 再校准。DMA 长度按字节传入。 */
    if (HAL_ADC_Start(adc) != HAL_OK) return -1;
    if (HAL_ADC_Calibrate(adc) != HAL_OK) return -2;
    if (HAL_DAC_StartChannel_DMA(dac, HAL_DAC_CHANNEL_1, dac_samples,
                                 sizeof(dac_samples)) != HAL_OK) return -3;
    if (HAL_ADC_REG_StartConv_DMA(adc, (uint8_t *)adc_samples,
                                   sizeof(adc_samples)) != HAL_OK) return -4;
    /* 最后启动 TIM6，保证首个触发到来时两路 DMA 都已就绪。 */
    if (HAL_TIM_Start(timer) != HAL_OK) return -5;
    running = 1U;
    return 0;
}

void audio_request_mode(audio_mode_t mode)
{
    /* 仅记录请求，实际切换留到半缓冲边界。 */
    requested_mode = mode;
}

void audio_poll(void)
{
    if (!running) return;
    uint32_t start_us = board_micros();
    service_output();
    if (peripheral_fault) {
        (void)HAL_TIM_Stop(mx_tim6_gethandle());
        /* 关闭 DAC 通道，避免故障期间持续保持任意的最后输出电压。 */
        (void)HAL_DAC_StopChannel_DMA(mx_dac1_gethandle(), HAL_DAC_CHANNEL_1);
        running = 0U;
        dsp_reset();
        measurement_cancel();
        return;
    }
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    uint32_t seq = completed_seq, half = latest_adc_half;
    __set_PRIMASK(irq_state);
    if (seq == handled_seq) return;
    uint32_t delta = seq - handled_seq;
    if (delta > 1U) {
        /* 主循环落后时累计丢块，并重置去直流滤波器状态，避免跨缺口延续。 */
        lost_blocks += delta - 1U;
        measurement_gap();
        dc_prev_input = 0.0f;
        dc_prev_output = 0.0f;
        dsp_reset();
    }
    handled_seq = seq;
    memcpy(adc_block, adc_samples + half * AUDIO_HALF_SAMPLES, AUDIO_HALF_SAMPLES*sizeof(uint16_t));
    __DMB();
    if (completed_seq != seq) {
        ++lost_blocks;
        measurement_gap();
        dc_prev_input = dc_prev_output = 0.0f;
        dsp_reset();
        return;
    }

    /* 模式只在完整 DMA 块边界切换。 */
    if (current_mode != requested_mode) {
        current_mode = requested_mode;
        dc_prev_input = 0.0f;
        dc_prev_output = 0.0f;
        dsp_reset();
    }

    float sum_sq = 0.0f;
    float peak = 0.0f;
    uint32_t clipped = 0;
    uint16_t preview[AUDIO_WAVEFORM_GROUPS * 2U];
    const uint16_t *samples = adc_block;
    for (uint32_t group = 0; group < AUDIO_WAVEFORM_GROUPS; ++group) {
        preview[group * 2U] = 4095U;
        preview[group * 2U + 1U] = 0U;
    }
    for (uint32_t i = 0; i < AUDIO_HALF_SAMPLES; ++i) {
        const uint16_t raw = samples[i];
        /* 每 8 点提取一组 min/max，保留短脉冲而不发送全部原始样本。 */
        const uint32_t pair = (i / AUDIO_WAVEFORM_SAMPLES_PER_GROUP) * 2U;
        if (raw < preview[pair]) preview[pair] = raw;
        if (raw > preview[pair + 1U]) preview[pair + 1U] = raw;
        const float x = ((float)raw - 2048.0f) * (1.0f / 2048.0f);
        /* 48 kHz 采样下约 2 Hz 的一阶去直流高通，便于估算交流电平。 */
        const float y = x - dc_prev_input + 0.9997382f * dc_prev_output;
        dc_prev_input = x;
        dc_prev_output = y;
        sum_sq += y * y;
        const float magnitude = fabsf(y);
        if (magnitude > peak) peak = magnitude;
        if (raw <= 1U || raw >= 4094U) ++clipped;
    }

    clipped_samples += clipped;
    /* 只有整个半缓冲处理有效时才发布包络；UART 在主循环读取，不与 ISR 争用。 */
    memcpy(waveform_pairs, preview, sizeof(waveform_pairs));
    waveform_first_sample_index = (seq - 1U) * AUDIO_HALF_SAMPLES;
    waveform_valid = 1U;
    if (current_mode != AUDIO_MODE_IDLE) {
        /* dBFS 以满幅 1.0 为基准；接近静音时使用 -120 dBFS 作为下限。 */
        float rms = sqrtf(sum_sq / (float)AUDIO_HALF_SAMPLES);
        rms_dbfs = (rms > 0.000001f) ? 20.0f * log10f(rms) : -120.0f;
        peak_dbfs = (peak > 0.000001f) ? 20.0f * log10f(peak) : -120.0f;
        if(current_mode==AUDIO_MODE_RTA) dsp_push_raw(samples, AUDIO_HALF_SAMPLES, seq*AUDIO_HALF_SAMPLES);
        else {
            generator_reference_t reference;
            uint32_t first=(seq-1U)*AUDIO_HALF_SAMPLES;
            int ready=generator_reference(first,&reference);
            measurement_push_raw(samples,AUDIO_HALF_SAMPLES,first,ready ? &reference : 0,clipped);
            if(!measurement_active()) requested_mode=AUDIO_MODE_IDLE;
        }
    }
    service_output();
    /* TIM2 1 MHz 时基包含本次 RMS、FFT 和 DDS 开销，可实测 <8 ms 要求。 */
    uint32_t elapsed_us = board_micros() - start_us;
    if (elapsed_us > worst_block_us) worst_block_us = elapsed_us;
    if (!peripheral_fault) board_watchdog_feed();
}

void audio_get_status(audio_status_t *out)
{
    /* 将内部计数整理为协议层使用的状态快照。 */
    out->sample_index = handled_seq * AUDIO_HALF_SAMPLES;
    out->completed_blocks = completed_seq;
    out->lost_blocks = lost_blocks;
    out->dac_errors = dac_errors;
    out->clipped_samples = clipped_samples;
    out->worst_block_us = worst_block_us;
    out->rms_dbfs = rms_dbfs;
    out->peak_dbfs = peak_dbfs;
    out->mode = current_mode;
    out->main_frequency_hz = dsp_get_frequency();
    out->flags = peripheral_fault | (dsp_frequency_valid() ? 1U : 0U);
    out->adc_errors = adc_errors;
    out->dac_refill_misses = dac_refill_misses;
}

int audio_get_waveform_preview(uint16_t *min_max_pairs, uint32_t *first_sample_index)
{
    if (!waveform_valid) return 0;
    memcpy(min_max_pairs, waveform_pairs, sizeof(waveform_pairs));
    *first_sample_index = waveform_first_sample_index;
    return 1;
}
int audio_is_running(void) { return running; }
int audio_is_idle(void) { return current_mode==AUDIO_MODE_IDLE && requested_mode==AUDIO_MODE_IDLE; }

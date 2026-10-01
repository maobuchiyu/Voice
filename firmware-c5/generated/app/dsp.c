#include "dsp.h"
#include "audio.h"
#include "signal_tables.h"
#include "arm_math.h"
#include <math.h>
#include <string.h>
static arm_rfft_fast_instance_f32 fft;
static float history[DSP_FFT_LENGTH];
static float input[DSP_FFT_LENGTH];
static float output[DSP_FFT_LENGTH];
static float amplitude[DSP_FFT_LENGTH / 2U + 1U];
static int16_t preview[DSP_PREVIEW_POINTS];
static uint32_t head, filled, since_fft, result_index;
static float frequency;
static uint8_t valid, frequency_valid;

int dsp_init(void)
{
    dsp_reset();
    /* 固定尺寸初始化让链接器移除未使用的 FFT 表。 */
    return arm_rfft_fast_init_2048_f32(&fft) == ARM_MATH_SUCCESS ? 0 : -1;
}

void dsp_reset(void)
{
    head = filled = since_fft = 0;
    valid = frequency_valid = 0;
    frequency = 0.0f;
}

static void transform(uint32_t sample_index)
{
    /* 先减参考值再求平均，避免恒定输入累加舍入后形成虚假的低频峰。 */
    const float reference = history[head];
    float mean = 0.0f;
    for (uint32_t i = 0; i < DSP_FFT_LENGTH; ++i) mean += history[i] - reference;
    mean = reference + mean / DSP_FFT_LENGTH;
    for (uint32_t i = 0; i < DSP_FFT_LENGTH; ++i) {
        input[i] = (history[(head + i) % DSP_FFT_LENGTH] - mean) * hann_table[i];
    }
    /* CMSIS RFFT 修改 input；output[0]/[1] 分别是 DC/Nyquist。 */
    arm_rfft_fast_f32(&fft, input, output, 0);
    const float scale = 2.0f / (DSP_FFT_LENGTH * 0.5f);
    amplitude[0] = fabsf(output[0]) * scale * 0.5f;
    amplitude[DSP_FFT_LENGTH / 2U] = fabsf(output[1]) * scale * 0.5f;
    for (uint32_t k = 1; k < DSP_FFT_LENGTH / 2U; ++k) {
        float real = output[k * 2U], imag = output[k * 2U + 1U];
        amplitude[k] = sqrtf(real * real + imag * imag) * scale;
    }
    /* 在完整谱中找 20 Hz～20 kHz 主峰，低于 -100 dBFS 时标记无效。 */
    uint32_t peak_bin = 1;
    uint32_t last_bin = 20000U * DSP_FFT_LENGTH / AUDIO_SAMPLE_RATE;
    for (uint32_t k = 2; k <= last_bin; ++k) {
        if (amplitude[k] > amplitude[peak_bin]) peak_bin = k;
    }
    frequency_valid = amplitude[peak_bin] > 0.00001f;
    float offset = 0.0f;
    if (peak_bin > 1U && peak_bin < last_bin) {
        float a = logf(fmaxf(amplitude[peak_bin - 1U], 1e-12f));
        float b = logf(fmaxf(amplitude[peak_bin], 1e-12f));
        float c = logf(fmaxf(amplitude[peak_bin + 1U], 1e-12f));
        float divisor = a - 2.0f * b + c;
        if (fabsf(divisor) > 1e-9f) offset = fminf(0.5f, fmaxf(-0.5f, 0.5f * (a - c) / divisor));
    }
    frequency = frequency_valid ? ((float)peak_bin + offset) * AUDIO_SAMPLE_RATE / DSP_FFT_LENGTH : 0.0f;
    for (uint32_t i = 0; i < DSP_PREVIEW_POINTS; ++i) {
        uint32_t lo, hi;
        if (i < 128U) lo = hi = i + 1U;
        else {
            uint32_t j = i - 128U;
            lo = 129U + j * (DSP_FFT_LENGTH / 2U - 128U) / 128U;
            hi = 128U + (j + 1U) * (DSP_FFT_LENGTH / 2U - 128U) / 128U;
        }
        float maximum = 0.0f;
        for (uint32_t k = lo; k <= hi; ++k) maximum = fmaxf(maximum, amplitude[k]);
        float db = maximum > 1e-6f ? 20.0f * log10f(maximum) : -120.0f;
        preview[i] = (int16_t)lroundf(fminf(6.0f, fmaxf(-120.0f, db)) * 100.0f);
    }
    result_index = sample_index;
    valid = 1U;
}

void dsp_push(const float *samples, uint32_t count, uint32_t end_sample_index)
{
    for (uint32_t i = 0; i < count; ++i) {
        history[head] = samples[i];
        head = (head + 1U) % DSP_FFT_LENGTH;
        if (filled < DSP_FFT_LENGTH) ++filled;
        ++since_fft;
        if (filled == DSP_FFT_LENGTH && since_fft >= DSP_FFT_LENGTH / 2U) {
            transform(end_sample_index - count + i + 1U);
            since_fft = 0;
        }
    }
}

int dsp_get_spectrum(int16_t *levels, uint32_t *end_sample_index)
{
    if (!valid) return 0;
    memcpy(levels, preview, sizeof(preview));
    *end_sample_index = result_index;
    return 1;
}
float dsp_get_frequency(void) { return frequency; }
int dsp_frequency_valid(void) { return frequency_valid; }

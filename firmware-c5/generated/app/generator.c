#include "generator.h"
#include "audio.h"
#include "signal_tables.h"
#include <math.h>
static generator_config_t active = {1000U, -1200, 0U};
static generator_config_t pending = {1000U, -1200, 0U};
static uint32_t phase;
static uint32_t step;
static float gain;
static float target_gain;
static float gain_step;
static uint32_t ramp_left;

void generator_init(void)
{
    /* 每次初始化均恢复安全的上电状态：1 kHz、-12 dBFS、输出关闭。 */
    active = pending = (generator_config_t){1000U, -1200, 0U};
    phase = 0;
    gain = target_gain = 0.0f;
    ramp_left = 0;
    step = (uint32_t)(((uint64_t)active.frequency_hz << 32) / AUDIO_SAMPLE_RATE);
}

void generator_request(const generator_config_t *config)
{
    pending = *config;
}

void generator_render(uint16_t *out, uint32_t count)
{
    if (pending.enabled != active.enabled || pending.frequency_hz != active.frequency_hz ||
        pending.peak_centidb != active.peak_centidb) {
        active = pending;
        step = (uint32_t)(((uint64_t)active.frequency_hz << 32) / AUDIO_SAMPLE_RATE);
        target_gain = active.enabled ? powf(10.0f, (float)active.peak_centidb / 2000.0f) : 0.0f;
        /* 480 点即 10 ms，连续改变幅度，停用后回到 DAC 中点。 */
        ramp_left = AUDIO_SAMPLE_RATE / 100U;
        gain_step = (target_gain - gain) / (float)ramp_left;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (ramp_left != 0U) {
            gain += gain_step;
            if (--ramp_left == 0U) gain = target_gain;
        }
        /* 高 10 位查 1024 点 Flash 表，低 22 位做线性插值。 */
        uint32_t index = phase >> 22;
        float fraction = (float)(phase & 0x3FFFFFU) * (1.0f / 4194304.0f);
        float sine = sine_table[index] + fraction * (sine_table[index + 1U] - sine_table[index]);
        out[i] = (uint16_t)(2048.0f + 2048.0f * gain * sine + 0.5f);
        phase += step;
    }
}

void generator_get_config(generator_config_t *out) { *out = active; }

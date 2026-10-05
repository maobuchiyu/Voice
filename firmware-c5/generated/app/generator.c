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
static float active_hz=1000, pending_hz=1000;
static uint32_t generation, next_index, reference_slot;
static generator_reference_t references[4];
static uint8_t reference_valid[4];

void generator_init(void)
{
    /* 每次初始化均恢复安全的上电状态：1 kHz、-12 dBFS、输出关闭。 */
    active = pending = (generator_config_t){1000U, -1200, 0U};
    active_hz=pending_hz=1000; generation=next_index=reference_slot=0;
    for(uint32_t i=0;i<4;++i) reference_valid[i]=0;
    phase = 0;
    gain = target_gain = 0.0f;
    ramp_left = 0;
    step = (uint32_t)(((uint64_t)active.frequency_hz << 32) / AUDIO_SAMPLE_RATE);
}

void generator_request(const generator_config_t *config)
{
    pending = *config;
    pending_hz=(float)config->frequency_hz;
}

void generator_sweep_tone(float hz, int16_t level) {
    pending=(generator_config_t){(uint32_t)lroundf(hz),level,1}; pending_hz=hz;
}
float generator_sine(uint32_t value) {
    uint32_t index=value>>22;
    float fraction=(float)(value&0x3FFFFFU)*(1.0f/4194304.0f);
    return sine_table[index]+fraction*(sine_table[index+1]-sine_table[index]);
}
int generator_reference(uint32_t index, generator_reference_t *out) {
    for(uint32_t i=0;i<4;++i) if(reference_valid[i] && (uint32_t)(index-references[i].start)<AUDIO_HALF_SAMPLES) {
        *out=references[i]; out->phase+=(index-out->start)*out->step; out->start=index; return 1;
    }
    return 0;
}

void generator_render(uint16_t *out, uint32_t count)
{
    generator_render_at(out,count,next_index); next_index+=count;
}
void generator_render_at(uint16_t *out, uint32_t count, uint32_t first_sample)
{
    if (pending.enabled != active.enabled || pending.frequency_hz != active.frequency_hz ||
        pending.peak_centidb != active.peak_centidb || pending_hz!=active_hz) {
        active = pending;
        active_hz=pending_hz;
        step=(uint32_t)((double)active_hz*4294967296.0/AUDIO_SAMPLE_RATE);
        ++generation;
        target_gain = active.enabled ? powf(10.0f, (float)active.peak_centidb / 2000.0f) : 0.0f;
        /* 480 点即 10 ms，连续改变幅度，停用后回到 DAC 中点。 */
        ramp_left = AUDIO_SAMPLE_RATE / 100U;
        gain_step = (target_gain - gain) / (float)ramp_left;
    }
    /* 标记实际将输出的采样索引；ADC同步检波不能使用提前生成的未来相位。 */
    references[reference_slot]=(generator_reference_t){first_sample,phase,step,generation,target_gain,active.enabled,ramp_left==0};
    reference_valid[reference_slot]=1; reference_slot=(reference_slot+1)%4;
    for (uint32_t i = 0; i < count; ++i) {
        if (ramp_left != 0U) {
            gain += gain_step;
            if (--ramp_left == 0U) gain = target_gain;
        }
        /* 高 10 位查 1024 点 Flash 表，低 22 位做线性插值。 */
        float sine = generator_sine(phase);
        out[i] = (uint16_t)(2048.0f + 2048.0f * gain * sine + 0.5f);
        phase += step;
    }
}

void generator_get_config(generator_config_t *out) { *out = active; }

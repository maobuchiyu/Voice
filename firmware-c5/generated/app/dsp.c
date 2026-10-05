#include "dsp.h"
#include "audio.h"
#include "signal_tables.h"
#include "arm_math.h"
#include "calibration.h"
#include <math.h>
#include <string.h>
/* 8 KiB整数历史 + 32 KiB FFT工作区，幅度复用已被FFT修改的输入区。 */
static arm_rfft_fast_instance_f32 fft;
static int16_t history[DSP_MAX_LENGTH];
typedef union { float f[DSP_MAX_LENGTH]; uint16_t raw[DSP_MAX_LENGTH*2]; } fft_workspace_t;
static fft_workspace_t input_workspace, output_workspace;
#define input input_workspace.f
#define output output_workspace.f
static float averaged[DSP_PREVIEW_POINTS];
static int16_t preview[DSP_PREVIEW_POINTS];
static uint32_t head, filled, since_fft, result_index;
static uint16_t length = DSP_FFT_LENGTH;
static float frequency;
static uint8_t valid, frequency_valid, frozen, average_valid;
static uint8_t average_enabled=1;
void dsp_set_average(uint8_t enabled) { average_enabled=enabled; average_valid=0; }
static dsp_power_t power_result;
static float db(float value) { return value > 1e-12f ? 10.0f*log10f(value) : -120.0f; }
static int16_t amplitude_db(float value) {
    return (int16_t)lroundf(fminf(6.0f,fmaxf(-120.0f,20.0f*log10f(fmaxf(value,1e-6f))))*100.0f);
}
void dsp_reset(void) {
    head=filled=since_fft=0; valid=frequency_valid=average_valid=frozen=0; frequency=0;
}
int dsp_set_length(uint16_t n) {
    if(n!=2048 && n!=4096) return -1;
    if(frozen) return -2;
    arm_status s=n==2048 ? arm_rfft_fast_init_2048_f32(&fft) : arm_rfft_fast_init_4096_f32(&fft);
    if(s!=ARM_MATH_SUCCESS) return -1;
    length=n; dsp_reset(); return 0;
}
int dsp_init(void) { frozen=0; return dsp_set_length(DSP_FFT_LENGTH); }
uint16_t dsp_get_length(void) { return length; }
/* IEC模拟A/C曲线的频域幅度比，1 kHz归一化；并非声级计等级认证。 */
static float weighting(float hz, int a) {
    float f2=hz*hz, high=12194.0f*12194.0f;
    float ratio=high*f2/((f2+20.6f*20.6f)*(f2+high));
    if(a) ratio*=f2/sqrtf((f2+107.7f*107.7f)*(f2+737.9f*737.9f));
    return ratio*(a ? 1.258925412f : 1.007173f);
}
static void transform(uint32_t index) {
    int32_t sum=0;
    for(uint32_t i=0;i<length;++i) sum+=history[i];
    float mean=(float)sum/length;
    uint32_t stride=DSP_MAX_LENGTH/length;
    for(uint32_t i=0;i<length;++i)
        input[i]=((float)history[(head+i)%length]-mean)*(1.0f/2048.0f)*hann_table[i*stride];
    arm_rfft_fast_f32(&fft,input,output,0);
    float scale=4.0f/length, total=0, a_total=0, c_total=0, bands[8]={0};
    const float centers[8]={160,250,500,1000,2000,4000,8000,16000};
    input[0]=fabsf(output[0])*scale*0.5f;
    input[length/2]=fabsf(output[1])*scale*0.5f;
    uint32_t peak=1, first=(20U*length+47999U)/48000U, last=20000U*length/48000U;
    input[first]=0;
    peak=first;
    for(uint32_t k=1;k<=length/2;++k) {
        float mag2;
        if(k==length/2) mag2=output[1]*output[1];
        else { float re=output[2*k], im=output[2*k+1]; mag2=re*re+im*im; input[k]=sqrtf(mag2)*scale; }
        float hz=(float)k*48000/length;
        /* sum(w²)=3N/8；单边谱普通bin乘2，Nyquist不乘2。 */
        float p=mag2*(k==length/2 ? 1.0f : 2.0f)/(length*length*0.375f);
        /* 麦克风曲线只修正麦克风路径；电气回环表不会套到RTA声学输入。 */
        if(calibration_current_path()==CAL_MIC && calibration_id() && hz>=20 && hz<=20000) {
            float correction=0, phase_unused=0;
            if(calibration_apply(hz,&correction,&phase_unused)) p*=powf(10,correction/10);
        }
        if(hz>=20 && hz<=20000) {
            total+=p; float aw=weighting(hz,1), cw=weighting(hz,0);
            a_total+=p*aw*aw; c_total+=p*cw*cw;
            for(uint32_t b=0;b<8;++b)
                if(hz>=centers[b]*0.70710678f && hz<centers[b]*1.41421356f) bands[b]+=p;
        }
        if(k>=first && k<=last && input[k]>input[peak]) peak=k;
    }
    frequency_valid=input[peak]>1e-5f;
    float offset=0;
    if(peak>first && peak<last) {
        float aa=logf(fmaxf(input[peak-1],1e-12f)), bb=logf(fmaxf(input[peak],1e-12f));
        float cc=logf(fmaxf(input[peak+1],1e-12f)), divisor=aa-2*bb+cc;
        if(fabsf(divisor)>1e-9f) offset=fminf(.5f,fmaxf(-.5f,.5f*(aa-cc)/divisor));
    }
    frequency=frequency_valid ? (peak+offset)*48000/length : 0;
    float alpha=1.0f-expf(-(float)length/(2*48000*.3f));
    for(uint32_t i=0;i<256;++i) {
        uint32_t j=i-128, lo=i<128 ? i+1 : 129+j*(length/2-128)/128;
        uint32_t hi=i<128 ? lo : 128+(j+1)*(length/2-128)/128;
        float maximum=0;
        for(uint32_t k=lo;k<=hi;++k) {
            float amplitude=input[k], correction=0, phase_unused=0;
            if(calibration_current_path()==CAL_MIC && calibration_id() && calibration_apply((float)k*48000/length,&correction,&phase_unused))
                amplitude*=powf(10,correction/20);
            maximum=fmaxf(maximum,amplitude*amplitude);
        }
        averaged[i]=average_valid && average_enabled ? averaged[i]+alpha*(maximum-averaged[i]) : maximum;
        preview[i]=amplitude_db(sqrtf(averaged[i]));
    }
    average_valid=valid=1; result_index=index;
    power_result.z_dbfs=db(total); power_result.a_dbfs=db(a_total); power_result.c_dbfs=db(c_total);
    power_result.index=index; power_result.octave_valid=0;
    for(uint32_t b=0;b<8;++b) {
        /* 至少5个FFT bin才提供该频带的有效功率，避免低分辨率伪精确。 */
        if(centers[b]*.70710678f*length/48000>=5) power_result.octave_valid|=(uint16_t)(1U<<b);
        power_result.octave_dbfs[b]=db(bands[b]);
    }
}
static void push_sample(int16_t value,uint32_t index) {
    if(frozen) return;
    history[head]=value; head=(head+1)%length;
    if(filled<length) ++filled;
    if(++since_fft>=length/2 && filled==length) { transform(index); since_fft=0; }
}
void dsp_push_raw(const uint16_t *samples,uint32_t count,uint32_t index) {
    for(uint32_t i=0;i<count;++i) push_sample((int16_t)((int32_t)samples[i]-2048),index-count+i+1);
}
void dsp_push(const float *samples,uint32_t count,uint32_t index) {
    for(uint32_t i=0;i<count;++i) push_sample((int16_t)lroundf(fminf(32767,fmaxf(-32768,samples[i]*2048))),index-count+i+1);
}
int dsp_get_spectrum(int16_t *values,uint32_t *index) {
    if(!valid || frozen) return 0;
    memcpy(values,preview,sizeof(preview)); *index=result_index; return 1;
}
float dsp_get_frequency(void) { return frequency; }
int dsp_frequency_valid(void) { return frequency_valid; }
uint32_t dsp_result_index(void) { return result_index; }
int dsp_get_power(dsp_power_t *out) { if(!valid || frozen) return 0; *out=power_result; return 1; }
int dsp_freeze(void) { if(!valid || frozen) return -1; frozen=1; return 0; }
int dsp_is_frozen(void) { return frozen; }
uint16_t dsp_snapshot_read(uint16_t offset,int16_t *values,uint16_t count) {
    if(!frozen || offset>=length/2+1) return 0;
    if(count>length/2+1-offset) count=(uint16_t)(length/2+1-offset);
    for(uint32_t i=0;i<count;++i) values[i]=amplitude_db(input[offset+i]);
    return count;
}
void dsp_release(void) { dsp_reset(); }
void *dsp_scratch(void) { dsp_reset(); return input; }
/* 音频主循环串行调用：FFT只在512点块末尾启动，此时ADC快照已全部入环。
 * DAC暂存使用输入区高端，不覆盖冻结谱位于低端的N/2+1个幅度。 */
uint16_t *dsp_adc_workspace(void) { return output_workspace.raw+DSP_MAX_LENGTH*2-512; }
uint16_t *dsp_dac_workspace(void) { return input_workspace.raw+DSP_MAX_LENGTH*2-512; }

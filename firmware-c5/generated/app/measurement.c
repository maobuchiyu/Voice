#include "measurement.h"
#include "calibration.h"
#include "audio.h"
#include <math.h>
#include <string.h>
/* 直流 + H1～H5的sin/cos最小二乘，抵消非整周期窗口的谐波交叉泄漏。 */
static float gram[11][11], rhs[11];
static uint32_t samples_used, target, settling, generation, sequence;
static uint16_t point, reason;
static uint8_t kind, active, pending_result, have_reference;
static int16_t sweep_level;
static measurement_result_t result;
static float requested_frequency;
static void reset_integrator(void) { memset(gram,0,sizeof(gram)); memset(rhs,0,sizeof(rhs)); samples_used=0; reason=0; }
static float to_db(float amplitude) { return 20*log10f(fmaxf(amplitude,1e-6f)); }
static void sweep_tone(void) { generator_sweep_tone(20*powf(1000,(float)point/120),sweep_level); }
void measurement_init(void) { active=pending_result=have_reference=0; sequence=0; reset_integrator(); }
int measurement_start(uint8_t mode,int16_t level) {
    if(active || pending_result || (mode!=MEASURE_THD && mode!=MEASURE_SWEEP)) return -1;
    generator_config_t tone; generator_get_config(&tone);
    if(mode==MEASURE_THD && (!tone.enabled || tone.frequency_hz<100 || tone.frequency_hz>4000)) return -2;
    if(mode==MEASURE_SWEEP && (level<-4000 || level>-600)) return -2;
    kind=mode; active=1; point=0; sweep_level=level; have_reference=0; reset_integrator();
    requested_frequency=mode==MEASURE_THD ? (float)tone.frequency_hz : 0;
    if(kind==MEASURE_SWEEP) sweep_tone();
    return 0;
}
/* 指令可直接启音；等待DAC块边界提交和旧参考排空后才开始积分。 */
int measurement_start_thd(uint32_t hz,int16_t level) {
    if(active || pending_result || hz<100 || hz>4000 || level<-4000 || level>-600) return -1;
    generator_config_t tone={hz,level,1}; generator_request(&tone);
    kind=MEASURE_THD; active=1; point=0; have_reference=0;
    requested_frequency=(float)hz; reset_integrator(); return 0;
}
uint8_t measurement_state(void) {
    if(pending_result) return result.reason ? 4 : 3;
    if(!active) return 0;
    return !have_reference || settling ? 1 : 2;
}
uint16_t measurement_point(void) { return point; }
void measurement_cancel(void) {
    if(active && kind==MEASURE_SWEEP) { generator_config_t tone; generator_get_config(&tone); tone.enabled=0; generator_request(&tone); }
    active=pending_result=have_reference=0; reset_integrator();
}
void measurement_gap(void) { reset_integrator(); have_reference=0; reason=MEASURE_GAP; }
int measurement_active(void) { return active; }
int measurement_busy_result(void) { return pending_result; }
static int solve(uint32_t n,float *coefficient) {
    float matrix[11][12];
    for(uint32_t i=0;i<n;++i) {
        for(uint32_t j=0;j<n;++j) matrix[i][j]=gram[i<j?i:j][i<j?j:i]/samples_used;
        matrix[i][n]=rhs[i]/samples_used;
    }
    for(uint32_t i=0;i<n;++i) {
        uint32_t pivot=i;
        for(uint32_t j=i+1;j<n;++j) if(fabsf(matrix[j][i])>fabsf(matrix[pivot][i])) pivot=j;
        if(fabsf(matrix[pivot][i])<1e-6f) return 0;
        for(uint32_t j=i;j<=n;++j) { float tmp=matrix[i][j]; matrix[i][j]=matrix[pivot][j]; matrix[pivot][j]=tmp; }
        float scale=matrix[i][i];
        for(uint32_t j=i;j<=n;++j) matrix[i][j]/=scale;
        for(uint32_t row=0;row<n;++row) if(row!=i) {
            float scale2=matrix[row][i];
            for(uint32_t j=i;j<=n;++j) matrix[row][j]-=scale2*matrix[i][j];
        }
    }
    for(uint32_t i=0;i<n;++i) coefficient[i]=matrix[i][n];
    return 1;
}
static void complete(const generator_reference_t *ref,uint32_t end) {
    memset(&result,0,sizeof(result)); float coefficients[11]={0};
    uint32_t harmonics=kind==MEASURE_THD ? 5U : 1U;
    if(!solve(1+2*harmonics,coefficients)) reason|=MEASURE_UNSTABLE;
    result.sequence=++sequence; result.end_index=end; result.samples=samples_used;
    result.kind=kind; result.point=point; result.point_count=kind==MEASURE_SWEEP ? 121 : 1;
    result.calibration_id=calibration_id(); result.frequency=(float)((double)ref->step*48000/4294967296.0);
    float distortions=0;
    for(uint32_t h=0;h<harmonics;++h) {
        harmonic_result_t *value=&result.harmonic[h];
        value->frequency=(h+1)*result.frequency;
        if(value->frequency>20000.01f) { reason|=MEASURE_RANGE; continue; }
        result.harmonic_mask|=(uint8_t)(1U<<h);
        float sine=coefficients[1+2*h], cosine=coefficients[2+2*h];
        value->amplitude=sqrtf(sine*sine+cosine*cosine); value->dbfs=to_db(value->amplitude);
        value->phase=atan2f(cosine,sine);
        value->corrected_dbfs=value->dbfs; value->corrected_phase=value->phase;
        if(result.calibration_id && !calibration_apply(value->frequency,&value->corrected_dbfs,&value->corrected_phase)) reason|=MEASURE_CAL_MISSING;
        float fundamental=fmaxf(result.harmonic[0].amplitude,1e-12f);
        value->dbc=20*log10f(fmaxf(value->amplitude/fundamental,1e-6f)); value->percent=100*value->amplitude/fundamental;
        if(h) distortions+=value->amplitude*value->amplitude;
    }
    float fundamental=result.harmonic[0].amplitude;
    if(fundamental<1e-4f) reason|=MEASURE_WEAK;
    result.reason=reason;
    result.transfer_db=to_db(fundamental/fmaxf(ref->gain,1e-12f)); result.transfer_phase=result.harmonic[0].phase;
    float distortion=sqrtf(distortions)/fmaxf(fundamental,1e-12f);
    result.thd_percent=100*distortion; result.thd_db=to_db(distortion);
    pending_result=1;
}
static void push(const float *data,const uint16_t *raw,uint32_t count,uint32_t first,const generator_reference_t *ref,uint32_t clipped) {
    if(!active || pending_result) return;
    if(!ref || !ref->enabled || !ref->steady) { have_reference=0; reset_integrator(); return; }
    float frequency=(float)((double)ref->step*48000/4294967296.0);
    if(kind==MEASURE_THD && fabsf(frequency-requested_frequency)>.02f) { have_reference=0; reset_integrator(); return; }
    if(kind==MEASURE_THD && (frequency<99.99f || frequency>4000.01f)) { measurement_cancel(); return; }
    if(!have_reference || generation!=ref->generation) {
        generation=ref->generation; have_reference=1; reset_integrator();
        settling=(uint32_t)ceilf(fmaxf(.1f,5/frequency)*48000);
        target=(uint32_t)ceilf((kind==MEASURE_THD ? .5f : fmaxf(.2f,20/frequency))*48000);
    }
    if(clipped) reason|=MEASURE_CLIPPED;
    uint32_t harmonics=kind==MEASURE_THD ? 5U : 1U, n=1+2*harmonics;
    uint32_t phase=ref->phase;
    for(uint32_t i=0;i<count;++i,phase+=ref->step) {
        if(settling) { --settling; continue; }
        float basis[11]={1};
        for(uint32_t h=0;h<harmonics;++h) {
            uint32_t harmonic_phase=(h+1)*phase;
            basis[1+2*h]=generator_sine(harmonic_phase);
            basis[2+2*h]=generator_sine(harmonic_phase+0x40000000U);
        }
        for(uint32_t j=0;j<n;++j) {
            float x=raw ? ((float)raw[i]-2048)*(1.0f/2048.0f) : data[i];
            rhs[j]+=x*basis[j];
            for(uint32_t k=j;k<n;++k) gram[j][k]+=basis[j]*basis[k];
        }
        if(++samples_used==target) { complete(ref,first+i+1); break; }
    }
}
void measurement_push(const float *data,uint32_t count,uint32_t first,const generator_reference_t *ref,uint32_t clipped) {
    push(data,0,count,first,ref,clipped);
}
void measurement_push_raw(const uint16_t *data,uint32_t count,uint32_t first,const generator_reference_t *ref,uint32_t clipped) {
    push(0,data,count,first,ref,clipped);
}
int measurement_get(measurement_result_t *out) { if(!pending_result) return 0; *out=result; return 1; }
void measurement_consume(void) {
    if(!pending_result) return;
    pending_result=0; reset_integrator();
    if(kind==MEASURE_SWEEP) {
        if(++point==121) { measurement_cancel(); return; }
        have_reference=0; sweep_tone();
    }
}

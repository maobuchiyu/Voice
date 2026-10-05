#include "calibration.h"
#include "dsp.h"
#include <math.h>
#include <string.h>
static calibration_point_t curve[CAL_MAX_POINTS];
static calibration_point_t *staging;
static uint16_t count, expected, received;
static uint32_t id, gain_id, pending_id;
static uint8_t path=CAL_LOOP, loading, spl_valid;
static float spl_offset;
void calibration_init(void) { count=expected=received=0; id=gain_id=0; path=CAL_LOOP; loading=spl_valid=0; }
void calibration_path(uint8_t value,uint32_t gain) {
    if(value!=path || gain!=gain_id) { count=0; id=0; spl_valid=0; loading=0; }
    path=value; gain_id=gain;
}
int calibration_begin(uint32_t table_id,uint8_t target,uint32_t gain,uint16_t n) {
    if(loading || n<2 || n>CAL_MAX_POINTS || target!=path || gain!=gain_id || table_id==0) return -1;
    staging=dsp_scratch(); expected=n; received=0; pending_id=table_id; loading=1; return 0;
}
int calibration_append(uint16_t offset,const calibration_point_t *points,uint16_t n) {
    if(!loading || offset!=received || n==0 || received+n>expected) return -1;
    /* 先检查整个分块再写入，错误块可由发送方重新提交；旧有效表保持不变。 */
    float previous=received ? staging[received-1].hz : 0;
    for(uint32_t i=0;i<n;++i) {
        if(!isfinite(points[i].hz) || !isfinite(points[i].db) || !isfinite(points[i].phase) ||
           points[i].hz<20 || points[i].hz>20000 || points[i].hz<=previous ||
           fabsf(points[i].db)>80 || fabsf(points[i].phase)>3.141593f) return -1;
        previous=points[i].hz;
    }
    memcpy(staging+received,points,n*sizeof(*points)); received+=n; return 0;
}
int calibration_commit(void) {
    if(!loading || received!=expected) return -1;
    if(path==CAL_MIC && (fabsf(staging[0].hz-20)>.001f || fabsf(staging[expected-1].hz-20000)>.001f)) return -1;
    /* 先展开相位，随后按log(f)插值；仅显式提交才替换当前路径。 */
    for(uint32_t i=1;i<expected;++i) {
        float difference=staging[i].phase-staging[i-1].phase;
        staging[i].phase=staging[i-1].phase+remainderf(difference,6.283185307f);
    }
    memcpy(curve,staging,expected*sizeof(*curve)); count=expected; id=pending_id; loading=0;
    dsp_reset(); return 0;
}
void calibration_abort(void) { loading=0; dsp_reset(); }
int calibration_loading(void) { return loading; }
uint32_t calibration_pending_id(void) { return pending_id; }
uint16_t calibration_received(void) { return received; }
void calibration_clear(void) { count=expected=received=0; id=pending_id=0; loading=spl_valid=0; dsp_reset(); }
int calibration_apply(float hz,float *db,float *phase) {
    if(count<2 || hz<curve[0].hz || hz>curve[count-1].hz) return 0;
    uint32_t upper=1;
    while(upper<count-1 && hz>curve[upper].hz) ++upper;
    calibration_point_t a=curve[upper-1], b=curve[upper];
    float t=logf(hz/a.hz)/logf(b.hz/a.hz);
    *db+=a.db+t*(b.db-a.db); *phase+=a.phase+t*(b.phase-a.phase); return 1;
}
uint32_t calibration_id(void) { return id; }
uint32_t calibration_gain_id(void) { return gain_id; }
uint8_t calibration_current_path(void) { return path; }
int calibration_set_spl(float offset,uint32_t gain) {
    if(path!=CAL_MIC || gain!=gain_id || !isfinite(offset) || offset<0 || offset>200) return -1;
    spl_offset=offset; spl_valid=1; return 0;
}
int calibration_spl(float dbfs,float *spl) { if(!spl_valid || path!=CAL_MIC) return 0; *spl=dbfs+spl_offset; return 1; }

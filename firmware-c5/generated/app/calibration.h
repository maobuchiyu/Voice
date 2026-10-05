#ifndef ACOUSTICLAB_CALIBRATION_H
#define ACOUSTICLAB_CALIBRATION_H
#include <stdint.h>
enum { CAL_MAX_POINTS=121, CAL_LOOP=1, CAL_MIC=2 };
typedef struct { float hz, db, phase; } calibration_point_t;
/* 当前路径/增益标识由客户端明确设置；改变后曲线和SPL均失效。 */
void calibration_init(void);
void calibration_path(uint8_t path,uint32_t gain_id);
int calibration_begin(uint32_t id,uint8_t path,uint32_t gain_id,uint16_t count);
int calibration_append(uint16_t offset,const calibration_point_t *points,uint16_t count);
int calibration_commit(void);
void calibration_abort(void);
int calibration_loading(void);
uint32_t calibration_pending_id(void);
uint16_t calibration_received(void);
void calibration_clear(void);
int calibration_apply(float hz,float *db,float *phase);
uint32_t calibration_id(void);
uint32_t calibration_gain_id(void);
uint8_t calibration_current_path(void);
int calibration_set_spl(float offset,uint32_t gain_id);
int calibration_spl(float dbfs,float *spl);
#endif

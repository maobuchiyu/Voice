#ifndef ACOUSTICLAB_MEASUREMENT_H
#define ACOUSTICLAB_MEASUREMENT_H
#include <stdint.h>
#include "generator.h"
enum { MEASURE_THD=1, MEASURE_SWEEP=2, MEASURE_WEAK=1, MEASURE_CLIPPED=2,
       MEASURE_GAP=4, MEASURE_UNSTABLE=8, MEASURE_RANGE=16, MEASURE_CAL_MISSING=32 };
typedef struct { float frequency, amplitude, dbfs, dbc, percent, phase, corrected_dbfs, corrected_phase; } harmonic_result_t;
typedef struct {
    uint32_t sequence, end_index, samples, calibration_id;
    uint16_t point, point_count, reason;
    uint8_t kind, harmonic_mask;
    float frequency, transfer_db, transfer_phase, thd_percent, thd_db;
    harmonic_result_t harmonic[5];
} measurement_result_t;
void measurement_init(void);
int measurement_start(uint8_t kind,int16_t level);
int measurement_start_thd(uint32_t hz,int16_t level);
uint8_t measurement_state(void);
uint16_t measurement_point(void);
void measurement_cancel(void);
void measurement_gap(void);
int measurement_active(void);
int measurement_busy_result(void);
/* 输入未经流式高通的ADC归一化块和同一采样位置的DAC参考。 */
void measurement_push(const float *samples,uint32_t count,uint32_t first,
                      const generator_reference_t *reference,uint32_t clipped);
void measurement_push_raw(const uint16_t *samples,uint32_t count,uint32_t first,
                          const generator_reference_t *reference,uint32_t clipped);
int measurement_get(measurement_result_t *out);
void measurement_consume(void);
#endif

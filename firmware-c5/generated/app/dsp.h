#ifndef ACOUSTICLAB_DSP_H
#define ACOUSTICLAB_DSP_H
#include <stdint.h>
enum { DSP_FFT_LENGTH = 2048, DSP_MAX_LENGTH = 4096, DSP_PREVIEW_POINTS = 256 };
typedef struct { float z_dbfs, a_dbfs, c_dbfs, octave_dbfs[8]; uint32_t index; uint16_t octave_valid; } dsp_power_t;
int dsp_set_length(uint16_t length);
uint16_t dsp_get_length(void);
void dsp_set_average(uint8_t enabled);
int dsp_freeze(void);
void dsp_release(void);
int dsp_is_frozen(void);
uint16_t dsp_snapshot_read(uint16_t offset, int16_t *values, uint16_t count);
uint32_t dsp_result_index(void);
int dsp_get_power(dsp_power_t *out);
void *dsp_scratch(void); /* 仅IDLE校准上传使用；重置旧谱并借用FFT输入区。 */
void dsp_push_raw(const uint16_t *samples, uint32_t count, uint32_t end_sample_index);
uint16_t *dsp_adc_workspace(void);
uint16_t *dsp_dac_workspace(void);
int dsp_init(void);
void dsp_reset(void);
/* 输入连续的归一化去直流样本；每累计 1024 新样本进行一次 FFT。 */
void dsp_push(const float *samples, uint32_t count, uint32_t end_sample_index);
/* 独立取最新结果，不修改工作区；丢块/模式切换后旧结果立即失效。 */
int dsp_get_spectrum(int16_t *levels, uint32_t *end_sample_index);
float dsp_get_frequency(void);
int dsp_frequency_valid(void);
#endif

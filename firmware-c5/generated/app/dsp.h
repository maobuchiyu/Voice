#ifndef ACOUSTICLAB_DSP_H
#define ACOUSTICLAB_DSP_H
#include <stdint.h>
enum { DSP_FFT_LENGTH = 2048, DSP_PREVIEW_POINTS = 256 };
int dsp_init(void);
void dsp_reset(void);
/* 输入连续的归一化去直流样本；每累计 1024 新样本进行一次 FFT。 */
void dsp_push(const float *samples, uint32_t count, uint32_t end_sample_index);
/* 独立取最新结果，不修改工作区；丢块/模式切换后旧结果立即失效。 */
int dsp_get_spectrum(int16_t *levels, uint32_t *end_sample_index);
float dsp_get_frequency(void);
int dsp_frequency_valid(void);
#endif

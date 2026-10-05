#ifndef ACOUSTICLAB_AUDIO_H
#define ACOUSTICLAB_AUDIO_H

#include <stdint.h>

/* DMA 双半缓冲：每半 512 点，48 kHz 下每块约 10.67 ms。 */
enum {
    AUDIO_SAMPLE_RATE = 48000,
    AUDIO_DMA_SAMPLES = 1024,
    AUDIO_HALF_SAMPLES = 512,
    AUDIO_WAVEFORM_GROUPS = 64,
    AUDIO_WAVEFORM_SAMPLES_PER_GROUP = AUDIO_HALF_SAMPLES / AUDIO_WAVEFORM_GROUPS
};

typedef enum {
    AUDIO_MODE_IDLE = 0, /* 空闲：继续采样及独立测试音，不发布分析预览。 */
    AUDIO_MODE_RTA = 1,  /* 实时分析：计算并发布电平、波形及频谱。 */
    AUDIO_MODE_THD = 2,  /* 稳定的自身测试音，H1～H5同步测量。 */
    AUDIO_MODE_SWEEP = 3 /* 121点逐点扫频，FFT暂停以保证实时预算。 */
} audio_mode_t;

/* 主循环维护的音频状态快照，供 UART 状态帧读取。 */
typedef struct {
    uint32_t sample_index;
    uint32_t completed_blocks;
    uint32_t lost_blocks;
    uint32_t dac_errors;
    uint32_t clipped_samples;
    uint32_t worst_block_us;
    float rms_dbfs;
    float peak_dbfs;
    audio_mode_t mode;
    float main_frequency_hz;
    uint32_t flags;
    uint32_t adc_errors;
    uint32_t dac_refill_misses;
} audio_status_t;

/* 调用前须由 mx_system_init() 创建并关联 HAL2 外设句柄。 */
int audio_start(void);
/* 在主循环中处理最新完成的 ADC 半缓冲。 */
void audio_poll(void);
/* 模式请求在下一块完整 ADC 数据到达时生效。 */
void audio_request_mode(audio_mode_t mode);
/* 读取当前统计值；调用者提供输出结构体。 */
void audio_get_status(audio_status_t *out);
/* 取得最近一个有效半缓冲的 64 组 ADC 最小/最大值及首样本序号。 */
int audio_get_waveform_preview(uint16_t *min_max_pairs, uint32_t *first_sample_index);
/* 采样/输出错误会停 TIM6，随后由看门狗恢复到上电默认关闭状态。 */
int audio_is_running(void);
int audio_is_idle(void); /* 同时检查当前状态和已接受但未提交的模式请求。 */

#endif

#ifndef ACOUSTICLAB_GENERATOR_H
#define ACOUSTICLAB_GENERATOR_H
#include <stdint.h>
typedef struct {
    uint32_t frequency_hz;
    int16_t peak_centidb;
    uint8_t enabled;
} generator_config_t;
/* 上电默认关闭，参数保留 1 kHz / -12 dBFS；配置在 DAC 半区边界提交。 */
void generator_init(void);
void generator_request(const generator_config_t *config);
void generator_render(uint16_t *out, uint32_t count);
void generator_get_config(generator_config_t *out);
#endif

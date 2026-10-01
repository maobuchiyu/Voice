#ifndef ACOUSTICLAB_BOARD_H
#define ACOUSTICLAB_BOARD_H
#include <stdint.h>
/* TIM2 独立微秒时基，不改变 TIM6 的音频采样触发。 */
int board_init(void);
uint32_t board_micros(void);
int board_watchdog_start(void);
void board_watchdog_feed(void);
#endif

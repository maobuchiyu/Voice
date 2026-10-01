/**
  ******************************************************************************
  * 文件           : main.c
  * 简介           : 程序入口；初始化系统、音频与通信后进入主循环。
  ******************************************************************************
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* 头文件 --------------------------------------------------------------------*/
#include "main.h"
#include "audio.h"
#include "protocol_uart.h"
#include "board.h"
#include "version.h"

/* 私有类型 ------------------------------------------------------------------*/
/* 私有常量 ------------------------------------------------------------------*/
/* 私有宏 --------------------------------------------------------------------*/
/* 私有变量 ------------------------------------------------------------------*/
/* 启动错误码供调试器查看：0 表示已进入主循环；负值表示对应初始化步骤失败。 */
volatile int acousticlab_boot_error;
volatile uint32_t acousticlab_firmware_version;
/* 私有函数声明 --------------------------------------------------------------*/

/**
  * 简介：应用程序入口。
  * 返回：按 C99 标准声明为 int；正常运行时不会返回。
  */
int main(void)
{
  acousticlab_firmware_version = ACOUSTICLAB_VERSION_CODE;
  /* 先由 CubeMX2 生成代码初始化时钟、引脚、DMA 和各外设句柄。 */
  /* mx_system_init() 包含 HAL、初始时钟及外设的生成配置。 */
  if (mx_system_init() != SYSTEM_OK)
  {
    acousticlab_boot_error = -100;
    while (1) {}
  }
  /* 独立 TIM2 微秒时基用来测量包含 FFT 的音频块处理耗时。 */
  if (board_init() != 0) {
    acousticlab_boot_error = -300;
    while (1) {}
  }
  /* IWDG 的 LSI 启动等待放在 TIM6 开始之前，避免音频半区超时。 */
  if (board_watchdog_start() != 0) {
    acousticlab_boot_error = -400;
    while (1) {}
  }
  /* 音频模块先准备 DAC/ADC 的循环 DMA，最后启动 TIM6 的 48 kHz 触发。 */
  acousticlab_boot_error = audio_start();
  if (acousticlab_boot_error != 0)
  {
    while (1) {}
  }
  /* 音频就绪后开启 UART 接收，避免对端先收到尚未初始化的状态。 */
  if (protocol_uart_start() != 0)
  {
    acousticlab_boot_error = -200;
    while (1) {}
  }
  /* 耗时计算和协议解析放在主循环，DMA/UART 中断只做轻量通知。 */
  for (;;)
  {
    audio_poll();
    protocol_uart_poll();
  }
} /* main 结束 */

# AcousticLab C5 v0.1.0

本目录是 STM32C532CCT6（LQFP48）的独立 CMake/GCC 固件工程。v0.1 软件已完成采样、测试音、电平、波形、FFT、频谱预览和控制协议；软件验证见 [VALIDATION.md](VALIDATION.md)。交流回环、真实麦克风和长期实物验收仍待完成。扫频、THD、校准、衰减测量属于后续版本。

## 功能与默认状态

- ADC/DAC 共用 TIM6，标称48 kHz；1024点循环DMA，每半区512点、10.667 ms。
- 上电IDLE，测试音关闭；默认参数1 kHz、−12 dBFS峰值。分析与测试音独立控制，停止分析不会自动关闭测试音。
- DDS：32位相位、1024点Flash正弦表及线性插值；20～20000 Hz、−40～−6 dBFS峰值；启停/幅度渐变480点（10 ms），关闭后DAC中点2048。
- RTA：约2 Hz去直流、RMS/峰值dBFS、削顶累计、2048点CMSIS RFFT、周期Hann窗及50%重叠。完整谱找主峰，弱于−100 dBFS标记无效。
- LEVEL、64组波形和256点频谱目标各10 Hz；STATUS 1 Hz。与ESP v0.1.0测试音控制协议匹配，见 [PROTOCOL.md](PROTOCOL.md)。
- TIM2 1 MHz时基记录最坏处理耗时。ADC/DAC故障或DAC补区超时停止触发并关闭DAC，停止喂狗，约2秒复位到默认关闭。看门狗只由成功音频块喂狗，调试器暂停时冻结。

## 引脚与工具链

| 功能 | 配置 |
|---|---|
| 主频 | 144 MHz，APB1不分频 |
| TIM6 | PSC=0、ARR=2999，48 kHz TRGO |
| ADC1 | PA0，12位单端、289周期采样，LPDMA1_CH0半字循环 |
| DAC1 OUT1 | PA4，12位右对齐、输出缓冲开启，LPDMA1_CH1循环 |
| USART2 | PA2 TX、PA3 RX，115200 8N1，TX LPDMA1_CH2 |
| TIM2 / IWDG | 自写Board模块配置，1 MHz计时 / 2秒看门狗 |
| 依赖 | STM32C5 DFP/HAL2 2.1.0、CMSIS-DSP 1.17.0 |
| 本机工具 | CubeMX2 1.1.1、CubeCLT 1.21.0、GCC 14.3.1 |

采样率由144000000/3000得到，内部振荡器绝对精度以实测为准。PA0连接麦克风OUT，或PA4经1 kΩ回环，二选一。双板UART：PA2→ESP GPIO16、ESP GPIO17→PA3、共地；两板分别USB供电，不连接彼此3V3。排针和占用按实际板卡原理图核对。

## 构建与烧录

在本目录PowerShell执行：

```powershell
# 当前保留Debug preset，供VS Code使用
./build.ps1
# 上板建议用优化的Release；切换生成目标需要重新生成
./build.ps1 -Profile Release -Regenerate
# 恢复Debug配置
./build.ps1 -Profile Debug -Regenerate
# PC上执行真实C算法及主循环测试，需MinGW GCC
py tools/test_v01.py
```

CubeMX2配置源在上一级AcousticLab_C5.ioc2。脚本保留main.c、CMakeLists.txt，恢复IWDG启用及4 KiB栈；app、tools不由生成器创建。直接覆盖生成可能覆盖用户入口，请使用此脚本。安装位置可传-CubeExe和-CltRoot，PC测试可传--gcc。

链接输出build/<preset>/AcousticLab_C5.elf/.hex/.bin。固定交付见 [release/v0.1.0](release/v0.1.0/README.md)，优先烧录其中Release HEX。STM32CubeProgrammer/ST-Link下载HEX；BIN起始地址0x08000000。本次没有执行烧录。

## 源码职责

| 文件 | 职责 |
|---|---|
| main.c | 初始化、优先处理音频的主循环、错误码与版本码 |
| app/board.* | 微秒时基、看门狗 |
| app/audio.* | DMA交接、快照、RMS/峰值、波形、丢块与故障 |
| app/generator.* | 块边界配置、DDS、渐变 |
| app/dsp.* | FFT、归一化、主频、预览映射 |
| app/protocol_codec.* | 编解码、CRC、逐字节解析 |
| app/protocol_uart.* | 收发、参数验证、回复优先调度 |
| app/signal_tables.h | Flash常量；tools/generate_tables.py可重建 |

自写模块和main使用中文注释。Measurement/Calibration在后续实际测量/校准阶段新增，v0.1不建立空实现。

HAL2调用按工程内ST官方驱动注释核对，FFT格式依据 [CMSIS RFFT官方文档](https://arm-software.github.io/CMSIS-DSP/main/group__RealFFTF32.html)。

acousticlab_boot_error：0正常、−100系统、−300 TIM2、−400 IWDG、−1～−6音频、−200 UART。acousticlab_firmware_version=0x000100表示v0.1.0。Windows旧上位机在根目录upper_pc，已兼容32/40字节STATUS，可继续查看波形；频谱和测试音完整操作使用ESP v0.1.0网页。

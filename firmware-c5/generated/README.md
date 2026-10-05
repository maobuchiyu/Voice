# AcousticLab C5 v0.3.0

本目录是 STM32C532CCT6（LQFP48）的独立 CMake/GCC 固件工程。v0.3.0 已完成 2048/4096 FFT、0.3 秒功率平均、A/C/Z 相对电平、频带功率、完整频谱冻结导出、121 点 DDS 扫频、同步幅相、H1～H5 THD 和 RAM 校准曲线。数值及协议软件验证见 [VALIDATION.md](VALIDATION.md)。尚未烧录此版本；电气/声学精度、最坏耗时和长期稳定性待实物验收。衰减测量留到 v0.4。

## 功能与默认状态

- ADC/DAC 共用 TIM6，标称48 kHz；1024点循环DMA，每半区512点、10.667 ms。
- 上电IDLE，测试音关闭；默认参数1 kHz、−12 dBFS峰值。分析与测试音独立控制，停止分析不会自动关闭测试音。
- DDS：32位相位、1024点Flash正弦表及线性插值；20～20000 Hz、−40～−6 dBFS峰值；启停/幅度渐变480点（10 ms），关闭后DAC中点2048。
- RTA：约2 Hz去直流、RMS/峰值dBFS、削顶累计、2048/4096点CMSIS RFFT、周期Hann窗及50%重叠。完整谱找主峰，弱于−100 dBFS标记无效。
- LEVEL、64组波形和256点频谱目标各10 Hz；STATUS 1 Hz。保留旧测试音控制协议，并增加v0.3高级指令，见 [PROTOCOL.md](PROTOCOL.md)。
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
py tools/test_v03.py
```

CubeMX2配置源在上一级AcousticLab_C5.ioc2。脚本保留main.c、CMakeLists.txt，恢复IWDG启用及4 KiB栈；app、tools不由生成器创建。直接覆盖生成可能覆盖用户入口，请使用此脚本。安装位置可传-CubeExe和-CltRoot，PC测试可传--gcc。

链接输出build/<preset>/AcousticLab_C5.elf/.hex/.bin。固定交付见 [release/v0.3.0](release/v0.3.0/README.md)，优先烧录其中Release HEX。STM32CubeProgrammer/ST-Link下载HEX；BIN起始地址0x08000000。本次没有执行烧录。

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

自写模块和main使用中文注释。app/measurement.*负责同步幅相与THD，app/calibration.*负责当前路径的校准事务、插值及SPL偏移。

HAL2调用按工程内ST官方驱动注释核对，FFT格式依据 [CMSIS RFFT官方文档](https://arm-software.github.io/CMSIS-DSP/main/group__RealFFTF32.html)。

acousticlab_boot_error：0正常、−100系统、−300 TIM2、−400 IWDG、−1～−6音频、−200 UART。acousticlab_firmware_version=0x000300表示v0.3.0。Windows旧上位机在根目录upper_pc，已兼容32/40字节STATUS，可继续查看波形；高级测量操作需要与PROTOCOL.md匹配的ESP固件。

## v0.3 使用顺序

先烧录优化的 `release/v0.3.0/AcousticLab_C5.hex`，复位仍为 IDLE、测试音关闭。USART2 引脚和波特率保持原配置。旧上位机的查询、RTA、波形和测试音指令继续可用；高级功能需要支持本文协议的 ESP/客户端。

THD：先用命令 3 设置 100～4000 Hz 测试音，等待 STATUS 确认已提交，再发命令 7/kind=1。稳定等待至少 100 ms、随后积分 500 ms；仅测自身 DDS 的 H1～H5，输出 THD 而非 THD+N。改频率前先切 IDLE 再调整参数。

扫频：命令 7/kind=2，固定 20～20000 Hz、121 点对数网格。每点等待 max(100 ms,5 周期)，测量 max(200 ms,20 周期)。扫完自动关闭扫频音并回 IDLE。命令 2/IDLE 可提前停止扫频；独立 THD 音需再用命令 3 关闭。

完整频谱：RTA 有新结果后命令 5 冻结并分块导出；4096 点为 2049 个 bin，含 DC 和 Nyquist。导出暂停频谱/波形预览，电平、ADC 和 DAC 继续；末块 DMA 完成后释放。命令 6 提前中止导出。

校准：IDLE 下命令 12 选择输入路径与增益标识，再按 8→9→10 装载曲线；每块最多 10 点。内部及线上相位用弧度。路径或增益标识改变会使曲线/SPL 失效。曲线只在 RAM 保存，重启后客户端重新装载；麦克风曲线须覆盖 20～20000 Hz。回环修正不套用于麦克风 RTA，未经真实声级校准仍使用 dBFS。

CMSIS 实数 FFT 的输入会被修改，因此本版把完整谱幅度复用到 FFT 输入区低端，DAC 暂存只使用高端。实现依据：[Arm 官方 RFFT 文档](https://arm-software.github.io/CMSIS-DSP/main/group__RealFFTF32.html)。A/C 采用 1 kHz 归一化频域权重，参考：[权重滤波器公式](https://www.mathworks.com/help/audio/ref/weightingfilter-system-object.html)；本项目未进行声级计等级认证。

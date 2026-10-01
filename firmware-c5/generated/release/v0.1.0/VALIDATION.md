# C5 v0.1.0 验证记录

日期：2026-10-01。此记录区分软件检查与实物验收；本次没有烧录或操作开发板。

## 软件完成结果

| 检查 | 结果 |
|---|---|
| CubeMX2 CLI重新生成 | Release及Debug均成功，自写入口、模块、IWDG与4 KiB栈保留 |
| GCC交叉编译 | Debug及Release成功，构建日志无warning/error，自写C代码启用-Werror |
| Release资源 | text=59200 B、data=120 B、bss=45072 B；Flash=text+data=59320 B（57.93 KiB） |
| SRAM预算 | data+bss=45192 B（44.13 KiB），其中链接器预留栈4096 B、堆512 B；低于54 KiB目标和64 KiB物理容量 |
| Debug资源 | text=86952 B、data=120 B、bss=45072 B；Flash=87072 B |
| 栈静态检查 | Release protocol_uart_poll帧1128 B、audio_poll帧304 B、dsp_push帧88 B；预留4 KiB，完整调用链/中断高水位仍需实物检查 |
| PC C算法/主循环测试 | tools/test_v01.py，7项通过，执行同一份app C源码和官方CMSIS FFT，外设使用测试替身 |
| 旧Windows上位机 | 根目录upper_pc，4项解码测试通过；补充40字节STATUS兼容，原32字节帧仍支持 |

7项C测试覆盖：CRC与帧长度边界；DDS电平、启停渐变、20/20000 Hz及−40/−6 dBFS范围；整bin FFT误差<0.2 dB，1000 Hz主峰插值误差<1 Hz；DC静音与重置；2048/1024窗口步长；Nyquist正确缩放及bin1～1024完整映射；RMS误差<0.1 dB；连续命令回复序号及非法参数；真实Audio/Protocol主循环调度；测试音配置提交/STATUS；ADC故障及DAC补区错过后的停输出。

模拟60秒、48 kHz DMA和115200 8N1占线时间：LEVEL/WAVEFORM/SPECTRUM各599帧（约9.983 Hz），总流量8580.4 B/s；理论预算8594 B/s。模拟没有测量ARM上的真实计算耗时、IRQ延迟或模拟电路误差，不代替下面实物验收。

构建/测试日志：build/v01-release-build.log、build/v01-debug-build.log、build/v01-tests.log。实际交付文件和哈希在release/v0.1.0。

## 上板验收步骤（待完成）

1. 用ST-Link烧录Release HEX；查询STATUS，应为IDLE、audio_running=1、48 kHz、N=2048、支持测试音且未开启。关闭输出PA4保持中点，调试版本码0x000100。
2. 拔掉PA0的麦克风/外部电源连接。PA4经1 kΩ接PA0，共地；不要让DAC和外部电源同时驱动PA0。使用ESP v0.1.0网页查询、开始分析、开启1 kHz/−12 dBFS测试音。
3. 等待输入稳定后记录：主频、RMS、Peak、削顶、采样丢块、DAC错误、flags、worst_block_us。理想输出Peak约−12 dBFS、RMS约−15.01 dBFS；实际误差由VDDA/VREF、DAC/ADC和接线决定，不把理论值写成实测值。
4. 改为−18 dBFS，测量电平应下降约6 dB，差值误差目标≤0.5 dB。测试其他频率和边界参数；关闭音后输出回中点，停止分析后STATUS为IDLE。分析与测试音独立，分别关闭。
5. 用示波器观察启停约10 ms渐变与PA4稳态幅度。测独立参考频率或TIM6触发，不能仅用同源DAC→ADC回环校验绝对时钟精度。
6. 统计60秒预览：频谱/波形各目标10 Hz，总流量≤9 KB/s。正常运行30分钟，lost_blocks=0、dac_errors=0、故障flags=0；记录预览跳过数、UART坏帧和最大块耗时。Release最坏路径要求<8000 us。
7. 断开网页、手机重连、ESP复位，确认C5持续采样；单独复位C5应恢复IDLE且测试音关闭。检查网页没有将旧帧当新测量。
8. 关闭测试音，断开DAC回环，改接MAX4466 OUT→PA0；调整并记录增益，以最响预期声音不削顶为准，查看说话、口哨、拍手的波形及频谱。本版只给dBFS，不输出未经校准的dB SPL。

记录表：

| 项目 | 实测值 | 判定 |
|---|---|---|
| 1 kHz/−12 dBFS：主频、RMS、Peak | 待填写 | 待验收 |
| −12→−18 dBFS电平差 | 待填写 | ≤0.5 dB差值误差 |
| 60秒频谱/波形帧数与字节数 | 待填写 | 10 Hz，≤9 KB/s |
| 30分钟丢块/DAC错误/坏帧变化 | 待填写 | 音频丢块/欠载0 |
| Release最坏块耗时及栈高水位 | 待填写 | <8 ms、栈未越界 |
| 麦克风增益/最响输入削顶 | 待填写 | 增益固定、无削顶 |

# C5 v0.3.0 验证记录

日期：2026-10-05。本次仅操作源码、构建和PC软件测试，未烧录或实测v0.3开发板。

| 检查 | 结果 |
|---|---|
| Release构建 | 成功；text=119088、data=136、bss=56712 B，无warning/error，自写源启用-Werror |
| Debug CLI重新生成及构建 | 成功；text=152848、data=128、bss=56712 B；最后保留Debug配置供VS Code使用 |
| Release Flash | 119224 B（116.43 KiB），低于224 KiB规划上限 |
| Release SRAM | 56848 B（55.52 KiB），含4096 B栈及512 B堆预留；低于65536 B物理容量，剩8688 B；比原约54 KiB目标高1.52 KiB |
| 主要静态工作区 | 整数历史8 KiB、FFT输入/输出合计32 KiB；冻结谱复用输入低端，ADC/DAC快照复用工作区高端，无长录音数组 |
| 静态栈报告 | protocol_uart_poll=1344 B、audio_poll=344 B、DSP push_sample=176 B、measurement push=184 B、solve=576 B；这些不是完整调用链高水位，4 KiB栈仍待真机检查 |
| 基线回归 | test_v01.py，7项通过，CRC、DDS、FFT、主循环/故障、控制和吞吐 |
| v0.3算法/协议 | test_v03.py，6项通过，实际同源app C及官方CMSIS执行 |
| 解析互通 | 同一实际C5输出由当前项目ESP的protocol.h及网页protocol.js接受，包含状态、功率、完整谱、扩展状态、THD、扫频和校准回复 |

数值检查：4096点幅度误差<0.2 dB，Z功率与正弦RMS误差<0.1 dB；约1 kHz的A/C与Z差<0.1 dB。100、1000、1001和4000 Hz合成H1～H5，THD相对误差<5%、每阶幅度误差<2%、相位误差<0.002 rad；非整周期窗口采用DC+sin/cos最小二乘消除交叉泄漏。121点扫频全部完成、频率网格误差<1e-5相对值，完成后关闭扫频音。弱基波/削顶/禁用音无效判定、校准事务、log频率插值、相位展开、路径/增益失效及SPL入口通过。

完整4096谱导出：实际UART主循环在900 ms虚拟窗口内发送9块、2049个bin，同一dataset/sample_index，偏移连续且CRC正确；期间成功处理音频块>80且采样仍运行。冻结时不更新FFT，新频谱/波形预览暂停，电平和DAC继续；末块DMA结束才释放。

60秒48 kHz虚拟DMA+115200/8N1线路时间：LEVEL、WAVEFORM、SPECTRUM各599帧（9.983 Hz），总流量8650.4 B/s，低于9 KB/s目标。PC测试不模拟ARM计算时间、真实IRQ抢占或模拟电路误差，不据此宣称真机10 Hz或<8 ms已经通过。

复现：`py tools/test_v01.py`、`py tools/test_v03.py`，需要MinGW GCC/G++和Node。默认使用tools/host内已核对的ESP协议快照，使C5提交可独立复现；PowerShell设置 `$env:C5_LIVE_ESP="1"` 后运行test_v03.py，可直接核对当前esp/videoesp实际代码（该ESP也须支持此v0.3契约）。本次快照及实际代码两种模式都通过；`./build.ps1 -Profile Release -Regenerate`、`./build.ps1 -Profile Debug -Regenerate`。日志位于build/v03-release.log、build/v03-debug.log；本机版本包位于release/v0.3.0。

## v0.3上板验收（待完成）

1. 烧录Release HEX，查询旧STATUS与命令14扩展状态，确认版本/能力、IDLE、48 kHz、2048、音关闭。
2. PA4经1 kΩ接PA0，共地，断开麦克风/可调电源。先1 kHz/−12 dBFS，再−18 dBFS，记录峰值/RMS/THD、6 dB差值误差及回环重复性；不能用同源回环证明绝对采样时钟精度。
3. 检查100 Hz、1 kHz、4 kHz的H1～H5、500 ms积分、无效标记，以及121点扫频从20至20000 Hz、停止/扫完关闭输出。固定电气幅相需记录并生成回环校准，声传播延迟不当成硬件误差删除。
4. IDLE加载回环/麦克风曲线，确认只影响匹配路径；变更增益使旧曲线/SPL失效，C5复位后需ESP重载。真实SPL需可信参考设备，此前保持相对单位。
5. 在2048/4096、校准开/关及THD/扫频路径记录worst_block_us，目标<8000 us；测栈高水位、UART坏帧、采样丢块、DAC错误、完整谱真机导出<1秒和运行30分钟稳定性。
6. 双板网页验收：串口/网络断线重连、暂停、参数生效、文件导出和桌面/窄屏显示；当前未据软件检查勾选这些项目。

用户此前反馈PA0基础输入、麦克风和串口正常；该反馈不作为v0.3高级测量精度验收。下面保留v0.1历史软件和待测记录。

---

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

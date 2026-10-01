# C5 v0.1串口协议

USART2 115200 8N1。整数和IEEE-754 f32均为小端；协议版本仍为1。

## FrameDecoder帧结构

| 帧偏移 | 字段 | 类型 |
|---:|---|---|
| 0～1 | sync | 固定A5 5A；截图AA是示意值 |
| 2 | version | u8=1 |
| 3 | type | u8 |
| 4～5 | seq | u16，REPLY复用命令序号 |
| 6～7 | length | u16，仅负载字节数 |
| 8～11 | sample_index | u32，ADC样本索引，模2³² |
| 12起 | payload | length字节，按type解释 |
| 12+length | crc16 | u16，小端 |

整帧14+length字节，无帧尾。CRC-16/CCITT-FALSE从version到负载末尾：初值FFFF、多项式1021、不反射、不异或输出、低字节先发。COMMAND负载最多128字节，TX总帧最多1024字节。

类型：STATUS=1、LEVEL=2、SPECTRUM_PREVIEW=3、WAVEFORM=4、MEASUREMENT=5、DATA_CHUNK=6、COMMAND=7、REPLY=8。v0.1实现1/2/3/4/7/8。固定长度软件须按类型分别建解析，不能用两个u8表示变长负载。

## STATUS：40字节，1 Hz及命令请求

| 负载偏移 | 类型 | 字段 |
|---:|---|---|
| 0 | u8 | mode：0 IDLE、1 RTA |
| 1 | u8 | audio_running：1运行、0停止 |
| 2 | u16 | flags：bit1 ADC故障、bit2 DAC故障、bit3 DAC补区超时；bit8支持测试音、bit9配置已提交为开启 |
| 4 | u32 | sample_rate=48000 |
| 8 | u16 | fft_length=2048 |
| 10 | u16 | 预览跳过周期累计低16位；不是采样丢块 |
| 12 | u32 | completed_blocks，ADC完成半区累计 |
| 16 | u32 | lost_blocks，未处理或快照不可靠块累计 |
| 20 | u32 | dac_errors，DAC外设/DMA错误累计 |
| 24 | u32 | bad_frames，版本/长度/CRC及RX错误/溢出累计 |
| 28 | u32 | uptime_ms |
| 32 | u32 | tone_frequency_hz |
| 36 | i16 | tone_peak_centidb，−1200=−12.00 dBFS峰值 |
| 38 | u16 | 保留0 |

前32字节保留旧布局，客户端应接受旧32字节及新40字节。帧头索引为最新处理块结束位置（exclusive）。bit9表示DDS块配置提交；物理输出仍有最多一缓冲周期延迟及10 ms渐变。默认IDLE，测试音关闭、1 kHz、−12 dBFS。

## LEVEL：32字节，RTA目标10 Hz

rms_dbfs:f32 | peak_dbfs:f32 | main_frequency_hz:f32 | clipped_samples:u32 | flags:u32 | lost_blocks:u32 | worst_block_us:u32 | uptime_ms:u32

RMS/峰值基于去直流的归一化样本，满幅1.0对应0 dBFS；满幅正弦RMS约−3.01 dBFS，静音下限−120 dBFS。原始ADC≤1或≥4094计削顶。flags bit0主频有效，bit1/2/3为STATUS同义故障；无效主频发0，界面不得将其显示为有效测量。完整FFT在20 Hz～20 kHz找峰，低于−100 dBFS无效，绝对频率需实测校验。worst_block_us由TIM2 1 MHz计时，包含音频、DDS及FFT，累计最大值。帧头为最新处理块结束位置。

## WAVEFORM：260字节，RTA目标10 Hz

source_count:u16=512 | group_count:u16=64 | (min:i16,max:i16)×64

每连续8点取min/max，原码减2048，范围−2048～2047，保留DC。帧头索引是源半区首样本。约10.667 ms包络快照，不是连续原始音频或硬件触发波形。

## SPECTRUM_PREVIEW：520字节，RTA目标10 Hz

sample_rate:u32=48000 | fft_length:u16=2048 | point_count:u16=256 | levels:i16[256]

levels单位0.01 dBFS，范围−12000～600。2048点周期Hann窗，1024点步长，首次42.667 ms后出谱，之后计算46.875次/秒。正频率幅度2|X[k]|/sum(window)，DC/Nyquist不乘2。帧头索引为FFT窗结束位置，不重复发送同一窗。

前128点对应bin1～128；后128点j=0～127，对应lo=129+floor(j*896/128)到hi=128+floor((j+1)*896/128)，组内取最大幅度，覆盖bin129～1024。频率k*48000/2048。预览不显示DC，不能用于等带宽功率积分。

## COMMAND/REPLY

| 命令负载 | 行为 |
|---|---|
| 01 | 查询状态 |
| 02 00 / 02 01 | IDLE / RTA |
| 03 enabled:u8 frequency_hz:u32 peak_centidb:i16 | 测试音，恰好8字节，enabled=0/1、频率20～20000、电平−4000～−600 |

分析与测试音独立；停止分析不关闭测试音，关闭测试音也会验证其参数。块边界提交。DAC中心2048，峰值2048*10^(peak_centidb/2000)。

REPLY固定4字节：command:u8 | result:u8 | current_mode:u8 | reserved:u8=0。result：0接受、1长度错误、2参数错误、3不支持。seq复用命令seq。REPLY不是物理输出完成通知，随后新的STATUS确认提交；立即STATUS可能仍为旧值。连续命令逐条应答，ESP最多一个在途、2秒超时不重发。

| 操作 | 完整帧（sample_index=0） |
|---|---|
| 查询seq1 | A5 5A 01 07 01 00 01 00 00 00 00 00 01 DF 53 |
| IDLE seq2 | A5 5A 01 07 02 00 02 00 00 00 00 00 02 00 7F 99 |
| RTA seq3 | A5 5A 01 07 03 00 02 00 00 00 00 00 02 01 1B E6 |

1 kHz、−12 dBFS开启负载：03 01 E8 03 00 00 50 FB；关闭负载：03 00 E8 03 00 00 50 FB。变更seq/参数须重算CRC。

## 流量与故障

调度REPLY→STATUS→LEVEL→WAVEFORM→SPECTRUM；UART忙时保留最新快照，不阻塞采样。正常预算54×1+46×10+274×10+534×10=8594 B/s，115200 8N1容量11520 B/s。大量命令降低预览速率，漏过预览周期独立计数；主动降采样的FFT窗不算故障。

RX溢出/UART错误会丢弃未完成帧并重新同步；TX超过250 ms未完成在主循环中止恢复。音频故障停止TIM6/DAC，清除主频有效性，停喂狗，约2秒重启到默认关闭。ADC错误和DAC补区次数还可通过调试器查看audio状态。

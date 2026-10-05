# C5 v0.3.0 串口协议

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

类型：STATUS=1、LEVEL=2、SPECTRUM_PREVIEW=3、WAVEFORM=4、MEASUREMENT=5、DATA_CHUNK=6、COMMAND=7、REPLY=8。v0.3实现全部类型；STATUS 保留40字节，新增能力通过 MEASUREMENT/kind=3 查询。固定长度软件须按类型分别建解析，不能用两个u8表示变长负载。

## STATUS：40字节，1 Hz及命令请求

| 负载偏移 | 类型 | 字段 |
|---:|---|---|
| 0 | u8 | mode：0 IDLE、1 RTA、2 THD、3 SWEEP |
| 1 | u8 | audio_running：1运行、0停止 |
| 2 | u16 | flags：bit1 ADC故障、bit2 DAC故障、bit3 DAC补区超时；bit8支持测试音、bit9配置已提交为开启 |
| 4 | u32 | sample_rate=48000 |
| 8 | u16 | fft_length=2048或4096 |
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

## v0.3 高级指令（type=7）

下表长度为负载长度；i16电平单位是centidB。所有校准上传只允许IDLE，模式在音频块边界提交；REPLY成功表示接受，最终配置以STATUS/扩展状态为准。

| ID | 长度 | 负载布局 | 作用 |
|---:|---:|---|---|
| 4 | 3 | cmd:u8, N:u16 | FFT=2048/4096；切换清空旧结果 |
| 5 | 1 | cmd:u8 | 冻结最新完整频谱并导出 |
| 6 | 1 | cmd:u8 | 中止导出，释放FFT工作区 |
| 7 | 4 | cmd:u8, kind:u8, level:i16 | kind=1 THD（先启独立测试音）；2扫频，level −4000～−600 |
| 8 | 12 | cmd:u8, id:u32, path:u8, gain_id:u32, count:u16 | 开始校准表事务，id非0、2～121点 |
| 9 | 4+12n | cmd:u8, offset:u16, n:u8, n组三个f32 | Hz、幅度修正dB、相位修正rad；1～10点，连续偏移 |
| 10 | 1 | cmd:u8 | 提交完整校准表；此前有效表保持不变 |
| 11 | 1 | cmd:u8 | 放弃正在上传的表；保留已有有效表 |
| 12 | 6 | cmd:u8, path:u8, gain_id:u32 | path=1电气回环、2麦克风；改变后旧校准失效 |
| 13 | 9 | cmd:u8, offset:f32, gain_id:u32 | 麦克风单点SPL偏移入口，IDLE、同一增益标识 |
| 14 | 1 | cmd:u8 | 请求高级能力/校准/导出状态 |

REPLY仍4字节，code新增4=当前忙、5=无可用结果/尚未就绪。STATUS.flags新增0x0400支持谐波、0x0800支持扫频、0x1000支持完整频谱；其他高级功能以扩展能力确认。限制状态不发送伪造成功数据。

## MEASUREMENT：type=5

首字节 schema=1；第二字节 kind=1 THD、2扫频、3扩展状态、4功率。不同kind有不同长度。

### kind=1/2：固定204字节

| 偏移 | 类型 | 字段 |
|---:|---|---|
| 0,1 | u8,u8 | schema=1、kind |
| 2 | u16 | 无效原因位图：1弱基波、2削顶、4丢块、8不稳定、16越界、32校准不覆盖；0有效 |
| 4 | u32 | result_sequence |
| 8 | u32 | 实际积分样本数；THD=24000 |
| 12 | u32 | calibration_id，0表示未校准 |
| 16,18 | u16,u16 | point_index/point_count；THD=0/1、扫频0～120/121 |
| 20,21 | u8,u8 | harmonic_valid_mask、path=1/2 |
| 22 | u16 | 保留0 |
| 24 | f32 | DDS实际基波频率Hz |
| 28,32 | f32,f32 | 原始传递幅度dB、相位rad，输入相对于数字DDS |
| 36,40 | f32,f32 | 原始THD百分比、THD dB |
| 44+32h | 8个f32 | h=0～4：频率Hz、峰值幅度、dBFS、dBc、百分比、原始相位rad、修正dBFS、修正相位rad |

THD来自原始H2～H5幅度平方和的平方根除以H1；校准不改写原始THD。扫频只测H1，其余阶mask无效，不能将零填充解释成有效谐波。sample_index是积分结束位置（exclusive），与result_sequence一起标识结果。失去参考/丢块后丢弃旧积分，重新等待并完成完整窗口；等待期间不复用上一结果当新结果。

### kind=3：28字节扩展状态

偏移2 u16=0x0300；4 u32 capabilities=0x3F（bit0 FFT配置、1完整谱、2THD、3扫频、4校准、5功率）；8 u16 FFT长度；10 u8 mode；11 u8 path；12 u32 gain_id；16 u32 calibration_id；20～23 u8分别loading、measurement_active、SPL有效、exporting；24 u32 uptime_ms。

### kind=4：56字节功率，RTA约1 Hz

偏移2 u8 SPL有效、3保留0；4/8/12 f32为Z/A/C dBFS（20 Hz～20 kHz完整谱功率）；16 u16频带有效mask、18 u16=8；20起8个f32频带dBFS，对应160/250/500/1000/2000/4000/8000/16000 Hz；52 f32为SPL值，仅标志有效时可显示。低分辨率频带必须检查mask，不能显示无效的伪精确读数。相对功率以完整Hann谱能量归一化；纯正弦RMS比峰值低约3.01 dB。

## DATA_CHUNK：type=6，22+2n字节

| 偏移 | 类型 | 字段 |
|---:|---|---|
| 0 | u32 | dataset_id，非0 |
| 4,5 | u8,u8 | kind=1频谱、format=1 i16 centidB峰值 |
| 6 | u16 | N=2048/4096 |
| 8 | u32 | Fs=48000 |
| 12,14,16 | u16,u16,u16 | total=N/2+1、offset、count≤236 |
| 18 | u32 | calibration_id（谱值为原始ADC幅度，保留曲线关联） |
| 22 | i16[count] | bin幅度centidB；−12000为本版下限 |

4096点为9块，2048点为5块。只在同一dataset_id、sample_index、Fs、N、calibration_id且偏移完整覆盖时拼接成功，缺块不能导出完整结果。bin k的频率为k*Fs/N。协议不使用独立末块位，offset+count==total就是末块；其DMA完成后才释放冻结谱。预览平均/麦克风修正不能套到原始完整谱文件而不注明。

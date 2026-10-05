# ESP v0.3 接口约定

## 二进制串口

沿用 [C5 协议](../../firmware-c5/generated/PROTOCOL.md)：版本 1，小端，A5 5A，CRC16/CCITT-FALSE。帧最大 1024 字节，原有 STATUS32、LEVEL32、WAVEFORM260 和 REPLY4 均兼容。ESP 缓存每类最新帧，预览至多 10 Hz；合并旧预览计入 coalesced，不当作 ADC 丢块。

### 频谱入口：ESP 与 C5 已实现

类型 3，负载 520 字节：`sample_rate:u32 | fft_length:u16 | count:u16=256 | levels:i16[256]`。幅值单位 0.01 dBFS；N=2048/4096，元数据须与 STATUS 一致。STATUS.fft_length=0 不展示主频或频谱。

前 128 点分别对应 FFT bin 1～128，后 128 点覆盖其余 bin 129～N/2，取每组最大值。零基索引 j，K=N/2：

```text
j<128: lo=hi=j+1
j>=128: lo=129+floor((j-128)*(K-128)/128)
        hi=128+floor((j-127)*(K-128)/128)
中心频率=(lo+hi)*Fs/(2*N)
```

DC 不在预览中；主频必须由 C5 完整 FFT 计算，LEVEL.flags bit0 表示有效，浏览器不从预览反推。

### 测试音扩展：ESP 与 C5 已实现

STATUS 保持前 32 字节并扩展为 40 字节，负载偏移 32 为频率 u32，36 为峰值 centidB i16，38 保留 u16。STATUS.flags 的 0x0100 表示支持，0x0200 表示输出已开启。没有支持标记时，ESP 不发送此命令，网页控件禁用。

COMMAND 3 的 8 字节负载：`03 | enabled:u8 | frequency_hz:u32 | peak_centidb:i16`。enabled=0/1，整数频率 20～20000 Hz，峰值 −4000～−600（−40～−6 dBFS）。REPLY 仍为原格式。ESP 等待成功 REPLY 和新的 STATUS，开启状态/频率/电平均匹配才确认；C5 的 DDS 和渐变仍待实现。

## WebSocket

HTTP 80，WebSocket 81 `/ws`。C5 命令最多一个在途，超时 2 秒；拒绝、超时、重启不乐观改变设备模式。

请求为 JSON：

```json
{"action":"status","id":1}
{"action":"rta","id":2}
{"action":"idle","id":3}
{"action":"tone","id":4,"enabled":true,"frequency":1000,"level":-12}
{"action":"save","id":5,"config":{"view":"scope","input":"mic","gainNote":"固定增益","coupling":"dc","timebase":1.066667,"vertical":0.25,"frequency":1000,"toneLevel":-12,"spectrumMin":-120,"spectrumMax":0}}
```

view=scope/spectrum，input=mic/loop/external（仅记录），coupling=ac/dc（仅显示）；timebase=0.1～1.1 ms/div，vertical=0.05～1 归一化单位/div。gainNote 最大 96 UTF-8 字节。配置整体校验后保存一个 NVS 字符串，未变化不重复写，成功才发送 saved。

文本事件：hello（version/ssid/flash/psram/storage/config/busy）、sent、ack、result、error、saved、device_restarted、stats。命令事件有 id/seq/message，result 带结果：0 确认成功，−1 未确认，1～3 C5 拒绝原因。hello/stats 的 busy 用于重连后延迟查询。

二进制消息保留完整 C5 帧。stats 每秒发送 online、rxBytes、rxRate（字节/秒）、frames、badFrames、coalesced、restarts、freeHeap、minHeap、statusAge（毫秒，−1 无状态）、uptime（ESP 毫秒）、busy。STATUS 超过 3 秒未刷新则离线，预览缓存超过 1 秒不推送。

`GET /api/info` 返回版本、SSID、online、Flash 和 PSRAM。CSV/JSON 由浏览器生成，是当前显示快照，不是连续音频或完整 FFT 导出。

## v0.1.1 诊断补充

stats 新增 resetReason（ESP-IDF 枚举）、maxLoopUs（本次启动最慢循环微秒）、stackFree（ESP-IDF 栈最低余量字节）。首次连接立即发送。`/api/info` 新增 uptime、rxBytes、frames、badFrames 及以上字段，HTTP 不依赖 WebSocket。接收字节和有效帧为本次 ESP 上电累计值；“—”表示未收到诊断，不能视为计数 0。WebSocket 两个槽仅用于刷新交接，新测量连接会关闭旧连接。

## v0.3 二进制对接

以 [C5 v0.3 协议](../../firmware-c5/generated/PROTOCOL.md) 为线格式来源。STATUS 仍是 32/40 字节；模式 0=IDLE、1=RTA、2=THD、3=SWEEP。高级能力由 COMMAND 14 返回的 MEASUREMENT 子类型 3 提供，不能把模式序号或扩展状态当作旧版本的字段。

MEASUREMENT 第一字节 layout=1、第二字节 subtype：1 THD/2扫频的负载 204 B，3扩展状态28 B，4相对功率56 B。204 B 结果含结果序号、积分样本数、校准ID、点索引、路径、原始幅相/THD和五行8个float。原因位1弱输入、2削顶、4断块、8不稳定、16范围、32校准未覆盖；位32单独存在时原始THD仍可用，校准结果不可用。没有校准ID也不声明校准结果有效。

完整谱 DATA_CHUNK 的22 B头：dataset_id:u32、kind:u8=1、format:u8=1、FFT:u16、Fs:u32、total:u16、offset:u16、count:u16、calibration_id:u32，随后count个centidB i16，每块最多236点。数据集ID由C5产生，没有final标志；采样序号与所有元信息一致且全部点已收到才完整。ESP与浏览器均拒绝冲突的重复块，5秒未完成则终止；断线不跨连接拼接。

## v0.3 WebSocket JSON

在原有action/id上增加：

| action | 参数 | C5 指令 |
|---|---|---|
| extended | 无 | 14，等待扩展报告 |
| analysis | fft:2048/4096 | 4 + u16，仅设置FFT；平均/功率在C5固定运行 |
| thd | enabled、frequency:100–4000整数、level:−40～−6整数 | 开始：3测试音确认后7/kind1；停止：2/IDLE |
| sweep | enabled、level | 开始：7/kind2；停止：2/IDLE |
| full | 无 | 5，等待完整分块及ACK/新STATUS |
| cal_save | profile（README中的schema） | ESP显式NVS保存，不影响C5 |
| cal_get | kind:loop/mic | 读取NVS；返回cal_profile |
| cal_apply | kind:loop/mic | 12路径/增益 → 8开始 → 9连续分块 → 10提交 → 14核对ID/路径/增益 |
| cal_clear | kind:loop/mic | 11取消暂存 → 12改变增益ID使旧表失效 → 14确认ID=0 |

校准只能在IDLE加载，单块最多10点，每步使用同一网页请求ID和新的串口seq，匹配ACK和新STATUS后继续；失败停止，不自动重发。总请求允许90秒、每条C5指令仍2秒。增益备注UTF-8与路径使用FNV-1a生成32位gain_id；仅用于配置身份。保存路径/增益与当前C5应用不符时禁止继续按旧表测量，需先停止清除或加载一致的表。加载中断后可在IDLE点击清除，先释放暂存。

hello增加calibrations（两份已保存表的元数据）；cal_profile携带profile；dataset_begin通知完整请求开始，但数据集ID直到第一个C5分块才确定。C5错误码4=BUSY、5=NOT_READY。保持一条串口命令在途，无测量重连自动重启。

浏览器测量导出schema `acousticlab.measurement.v0.3`，附C5来源、帧seq/sampleIndex、接收时间、Fs、累计样本数、输入/增益、校准ID和有效性；完整谱标记complete，扫频部分结果complete=false。currentConfig是导出时界面配置，单帧记录保留测量接收时的输入备注。不会从预览反算谐波、加权或扫频结果。

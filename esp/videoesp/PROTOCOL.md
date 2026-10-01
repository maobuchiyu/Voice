# ESP v0.1 接口约定

## 二进制串口

沿用 [C5 协议](../../firmware-c5/generated/PROTOCOL.md)：版本 1，小端，A5 5A，CRC16/CCITT-FALSE。帧最大 1024 字节，原有 STATUS32、LEVEL32、WAVEFORM260 和 REPLY4 均兼容。ESP 缓存每类最新帧，预览至多 10 Hz；合并旧预览计入 coalesced，不当作 ADC 丢块。

### 频谱入口：ESP 已实现，C5 待实现

类型 3，负载 520 字节：`sample_rate:u32 | fft_length:u16 | count:u16=256 | levels:i16[256]`。幅值单位 0.01 dBFS；N=2048/4096，元数据须与 STATUS 一致。STATUS.fft_length=0 不展示主频或频谱。

前 128 点分别对应 FFT bin 1～128，后 128 点覆盖其余 bin 129～N/2，取每组最大值。零基索引 j，K=N/2：

```text
j<128: lo=hi=j+1
j>=128: lo=129+floor((j-128)*(K-128)/128)
        hi=128+floor((j-127)*(K-128)/128)
中心频率=(lo+hi)*Fs/(2*N)
```

DC 不在预览中；主频必须由 C5 完整 FFT 计算，LEVEL.flags bit0 表示有效，浏览器不从预览反推。

### 测试音扩展：ESP 已实现，C5 待实现

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

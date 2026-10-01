# AcousticLab C5 简易上位机

用 Windows 的 Python 3.10+、Tkinter 和系统串口 API 运行，无需安装第三方包。先烧录包含 `MSG_WAVEFORM` 的新版 C5 固件，再用 USB 转串口连接 USART2：C5 PA2(TX) 接适配器 RX、PA3(RX) 接适配器 TX、GND 共地；电平必须是 3.3 V TTL。使用 115200、8N1，协议串口不要混入调试文字。

在本目录的 PowerShell 中运行 `./start.ps1`，也可以手动运行：

```powershell
py app.py
```

选择 COM 口并连接；“查询状态”发送 `GET_STATUS`，“停止分析 / IDLE”发送 `SET_MODE 0`，“实时分析 / RTA”发送 `SET_MODE 1`。固件在 RTA 模式下每约 100 ms 发送 LEVEL 与 WAVEFORM；波形是最新 512 个 ADC 样本的 **64 组 min/max 包络**，每组覆盖 8 点，不是连续原始音频，也不是硬件示波器触发。波形横轴约 10.67 ms，纵轴是以 ADC 中点 2048 为零的有符号码值（-2048～2047）。旧固件只发 STATUS/LEVEL 时，波形区会保持等待提示。

本工具按 `PROTOCOL.md` 的小端帧格式自动找帧头、校验 CRC、解析状态/电平/波形/应答。串口读线程不直接改界面。没有设备时可运行 `py -m unittest test_protocol.py` 检查解码、命令编码与坏帧恢复。真机收发和绘图效果仍需在开发板上验证。

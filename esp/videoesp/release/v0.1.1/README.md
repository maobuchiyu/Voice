# ESP v0.1.1 刷新修复发布

2026-10-01。已编译并经 COM8 烧录，启动版本和内存容量核对通过。手机实际连续刷新及 UART 计数待确认，未标为完整硬件验收通过。

修复 HTTP 同步整块传输、WebSocket 刷新交接、回调栈占用和失败结果；补充接收字节、复位原因、主循环耗时与栈余量。详情见 [CHANGELOG](../../CHANGELOG.md) 和 [VALIDATION](../../VALIDATION.md)。

- firmware.bin 是应用镜像，不是地址 0 完整 Flash 镜像；使用 PlatformIO Upload。
- source.zip 为本版源码与验证截图；manifest.json 保存 SHA256。
- Flash 787,513 B（12.0%），静态 RAM 57,120 B（17.4%）。
- GPIO16 RX / GPIO17 TX，115200 8N1；两板独立 USB 供电，3V3 不互连。

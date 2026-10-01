# AcousticLab ESP v0.1.0 发布文件

构建于 2026-10-01，ESP32-S3 N16R8。尚未上板烧录；协议与浏览器软件检查已通过。

- firmware.bin 是应用镜像；使用源码工程 PlatformIO Upload 烧录，不要直接写到 Flash 地址 0。
- bootloader.bin / partitions.bin / firmware.elf 留作诊断与匹配构建记录，不是独立完整镜像。
- source.zip 含本次源码、完整网页、构建配置、测试及文档，解压后以 PlatformIO 构建。依赖版本已固定。
- manifest.json 记录文件 SHA256、资源占用和源码哈希。

GPIO16 RX、GPIO17 TX、COM8；热点密码 12345678，两板 USB 独立供电，3V3 不互连。当前 C5 没有 FFT/DDS 时，相应页面功能禁用；详情见源码 README/PROTOCOL/VALIDATION。

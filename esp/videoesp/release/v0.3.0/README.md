# ESP v0.3.0 发布

2026-10-05。需要搭配C5 v0.3.0，ESP网页直接使用真实串口测量结果。

已完成THD/H1–H5、121点幅相扫频、loop/mic校准NVS保存/IDLE加载、4096控制、相对加权/频带、30秒瀑布图和完整谱CSV/JSON。版本和能力来自设备报告；旧C5禁用不支持的功能。

软件检查：Release无警告；39项JS协议检查、C5同源6项测试/真实帧双解析、基础15类/6次刷新与新增14类浏览器检查通过，错误0。细节见[验证记录](../../VALIDATION.md)、[版本说明](../../CHANGELOG.md)、[使用说明](../../README.md)。

本版未检测到CH343/COM8，尚未烧录。高级精度、校准断电保存、真机热点刷新和30分钟稳定性待验收。

## 本机构建文件

- firmware.bin：应用镜像，使用PlatformIO Upload；不能写到地址0。
- firmware.elf、bootloader.bin、partitions.bin、boot_app0.bin：同次构建/固定框架引导文件。
- source.zip：本版ESP源码、文档、计划/TODO及独立测试截图；测试夹具不嵌入固件。
- manifest.json：文件大小与SHA256；二进制/源码zip保存在本地，按项目约定不提交Git缓存或编译产物。

静态RAM80,944 B（24.7%），应用Flash807,481 B（12.3%）。16 MB QIO Flash、8 MB OPI PSRAM。Arduino2.0.17，WebSockets2.7.2，ArduinoJson6.21.5，Espressif32 7.0.1。

GPIO16 RX ← C5 PA2 TX，GPIO17 TX → PA3 RX，GND共地，115200 8N1。两板独立USB供电，3V3不互连。上传步骤见ESP README；网页版本应显示v0.3.0。

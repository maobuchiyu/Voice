# Voice / AcousticLab

STM32C532CC + ESP32-S3 N16R8 声学测量项目。C5 负责采样、测试信号和 DSP，ESP 使用 Arduino 提供热点和网页，双板通过 115200 8N1 UART 通信。

- [项目计划](PROJECT_PLAN.md)
- [开发进度与验收待办](TODO.md)
- [C5 固件与构建](firmware-c5/generated/README.md)
- [ESP 固件与构建](esp/videoesp/README.md)
- [网页原型](acousticlab-preview/README.md)
- [串口上位机](upper_pc/README.md)

当前版本的实现与实测状态以计划及 TODO 为准，软件交付不等于所有硬件验收通过。

## 版本管理

每次小版本推进完成后，完成对应检查，更新计划、待办与版本说明，提交并推送到本仓库。首次提交用于保存现有项目基线，不新增硬件验收结论。

仓库保留源码、构建配置及所需厂商依赖；本机环境、构建缓存和固件二进制不纳入源码版本管理。第三方代码保留其目录中的许可证，按对应许可证使用。

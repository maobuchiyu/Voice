# C5 v0.1.0 Release

2026-10-01，STM32C532CCT6，GCC 14.3.1，Release -Os，HAL2/CMSIS-DSP。

- AcousticLab_C5.hex：建议用STM32CubeProgrammer/ST-Link直接下载。
- AcousticLab_C5.bin：裸应用镜像，下载地址0x08000000。
- AcousticLab_C5.elf：含符号的调试文件；.map为链接资源报告。
- PROTOCOL.md / VALIDATION.md：协议、软件验证与待做实物验收。
- SHA256SUMS.txt：交付文件校验。

默认IDLE，测试音关闭（1 kHz、−12 dBFS）。RTA发布电平、波形与256点频谱；测试音独立开关。PA4经1 kΩ接PA0进行回环，PA0只选一个输入源。主工程中文说明位于../../README.md。

本版已完成软件构建和测试，尚未烧录；实测交流回环、<8 ms耗时及30分钟稳定性仍待验收。不是全项目v1.0或包含THD/扫频/校准的后续固件。

重建：主工程执行 ./build.ps1 -Profile Release -Regenerate，然后 ./tools/package_v01.ps1。

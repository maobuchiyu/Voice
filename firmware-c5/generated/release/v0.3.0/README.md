# C5 v0.3.0 Release

目标：STM32C532CCT6，2026-10-05软件交付。功能包括4096 FFT、平均/加权/频带功率、完整频谱分块导出、121点扫频、H1～H5 THD与RAM校准曲线。

烧录本目录 `AcousticLab_C5.hex`；BIN起始地址0x08000000。使用ST-Link/STM32CubeProgrammer。复位默认IDLE、48 kHz、FFT2048、1 kHz/−12 dBFS测试音关闭。版本码0x000300。

Release：Flash119224 B，SRAM56848 B（含4 KiB栈和512 B堆预留）。优化版本用于真机耗时检查；Debug仅用于调试。

13项同源C/CMSIS及虚拟外设软件测试通过，ESP C++与网页解码器接受实际C5帧。此版本未执行烧录；电气回环精度、校准重复性、最坏路径<8 ms、30分钟稳定性和双板联合验收仍待完成。具体步骤见VALIDATION.md。

重建：在generated运行 `./build.ps1 -Profile Release -Regenerate`，然后 `./tools/package_v03.ps1`。恢复VS Code配置使用 `./build.ps1 -Profile Debug -Regenerate`。HEX/BIN/ELF/MAP由本机打包产生并被Git忽略，仓库保留源码、协议、验证记录、版本说明和SHA256清单。

# v0.1.0 验证记录

日期：2026-10-01。硬件目标 ESP32-S3 N16R8，RX16/TX17，115200 8N1。

## 已完成的软件检查

| 检查 | 结果 |
|---|---|
| PlatformIO Release，用户源码 `-Wall -Wextra -Werror` | 成功，无警告 |
| 静态 RAM | 51,376 / 327,680 B，15.7% |
| 应用 Flash | 782,833 / 6,553,600 B，11.9% |
| JavaScript 语法 | app.js、protocol.js、测试服务器通过 |
| 协议检查 | 14 项通过，含每字节 CRC 损坏、全部截断长度、NaN、波形非法 min/max、2048/4096 频谱分组完整性 |
| Chrome 浏览器流程 | 15 类场景通过，浏览器错误 0 |
| 布局 | 1440×1000 与 390×844，无水平溢出；截图人工检查 |
| 导出 | JSON 64 组波形/256 点频谱；CSV 324 行数据，逗号和引号备注保留 |
| 内置网页 | 四份 gzip 解压后与 web/ 源文件逐字节一致；测试夹具不在资产列表 |

15 类浏览器场景：启停、暂停、单次、AUTOSET、频谱、测试音命令入口、保存/重载、CSV/JSON、超时保持模式、CRC 错帧、断线重连、C5 重启、桌面/手机溢出检查、缺失 C5 能力、帮助。

浏览器使用独立本地测试服务器的帧夹具，验证 UI 与接口流程；不会证明 C5 算法、ESP NVS 写入或实际 UART 工作。曾发现刷新期间仍有命令在途导致初始查询被跳过，已改为收到 busy=false 后再查询，复测通过。

## 复现

在本工程目录执行：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32s3_n16r8
node test/protocol.test.cjs
node tools/dev_server.cjs
```

最后一条在独立终端保持运行，仅绑定 127.0.0.1 的 HTTP 8080 与 WebSocket 81。安装/使用 Playwright 后，在工作区根目录执行：

```powershell
node esp/videoesp/tools/run_browser_checks.cjs <playwright模块绝对路径>
```

也可用 Playwright CLI 打开 127.0.0.1:8080，然后 `run-code --filename esp/videoesp/tools/browser_checks.js`。检查器截图和导出放入执行目录的 `output/playwright/`。这是测试服务器，永远不打包进固件。

本次截图（均为测试帧，不是真机测量）：[桌面波形](output/playwright/acousticlab-desktop-scope.png)、[桌面频谱](output/playwright/acousticlab-desktop-spectrum.png)、[手机波形](output/playwright/acousticlab-mobile-scope.png)、[手机频谱](output/playwright/acousticlab-mobile-spectrum.png)。

## 新版待上板验收

当前电脑只检测到蓝牙串口，未检测到 COM8；本版未上传。此前最小固件已有手机查询/RTA/波形实测记录。

- [ ] 烧录后串口确认 v0.1.0、16 MB Flash、约 8 MB PSRAM；手机完整页面加载正常。
- [ ] 实际 STATUS 查询、RTA/IDLE、LEVEL/WAVEFORM 数值和 10 Hz 更新率。
- [ ] 保存设置、断电重启，显示配置正确恢复，RTA/测试音不自动启动。
- [ ] 手机 Wi-Fi 重连、C5 单独复位、ESP 单独复位及 UART 断线提示。
- [ ] 连续运行 30 分钟，记录 minHeap、坏帧、丢块、DAC 错误及串口流量。
- [ ] C5 完成 FFT/DDS 后，核对真实频谱单位/分组及测试音状态、频率、电平确认。

保留密码 `12345678`、COM8、GPIO16/17；两板独立 USB 供电，3V3 不互连。系统 v0.1 的 FFT/DDS 和精度验收仍见根目录 TODO。

## v0.1.1 修复验证（2026-10-01）

- Release 编译无警告；RAM 57,120 B（17.4%），应用 Flash 787,513 B（12.0%）。
- COM8 上传成功，全部写入段哈希校验通过；重启串口确认 v0.1.1、Flash 16,777,216 B、PSRAM 8,385,943 B、循环栈余量 5,328 B。
- 14 项协议检查、原 15 类浏览器场景、额外 6 次连续刷新通过；浏览器错误 0。新增接收字节读数检查通过。测试为本地夹具，不验证固件 HTTP socket 的真实传输时序。
- 新 HTTP 有界读取、非阻塞分段发送及 WebSocket 交接已编译；手机实际热点连续刷新仍待反馈。监听期间未收到崩溃日志，不据此断言已证明原问题唯一根因。
- v0.1.0 烧录已由用户后续确认；本文件前面的“未烧录”是该版最初构建时记录。C5 FFT/DDS、NVS、实际更新率和 30 分钟稳定性仍待验收。

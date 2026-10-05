# ESP v0.3.0 验证记录

日期：2026-10-05；板型ESP32-S3 N16R8；Arduino2.0.17；GPIO16 RX/17 TX，115200 8N1。

## v0.3 软件检查

| 检查 | 结果 |
|---|---|
| PlatformIO Release，用户源码 -Wall -Wextra -Werror | 成功，无警告 |
| 静态 RAM | 80,944 / 327,680 B（24.7%） |
| 应用 Flash | 807,481 / 6,553,600 B（12.3%） |
| JS语法 | app、protocol、advanced、测试工具通过 |
| 基础协议 | 14项通过，所有截断长度/逐字节CRC损坏 |
| v0.3协议 | 25项通过：扩展/THD/功率、2048/4096全块、乱序/重复/冲突/元信息/超时、缺块不可导出、校准边界 |
| C5同源协议 | generated/tools/test_v03.py的6项测试通过；实际Audio/Protocol输出同时经过ESP C++与JS解码 |
| 基础浏览器 | 原15类场景及6次连续刷新通过，错误0 |
| v0.3浏览器 | 14类场景通过，错误0：能力报告、THD/H1–H5、暂停/导出采样位置一致、削顶无效、121点扫频、校准草稿/保存/加载、取消部分扫频、增益改变禁止旧校准、4096控制、2049点导出、缺块/重连拒绝导出、功率/频带、桌面/窄屏、旧固件禁用 |
| 视觉检查 | 1440×1000与390×844无水平溢出；已查看THD表格、幅相双轴、桌面/手机布局截图 |
| 嵌入资源 | 5份gzip解压与web源码一致；不包含test/tools或模拟夹具 |

测试入口：`node test/protocol.test.cjs`、`node test/advanced.test.cjs`；`node tools/dev_server.cjs`启动仅绑定127.0.0.1的测试服务，`tools/run_browser_checks.cjs <playwright模块路径> [browser_v03_checks.js]`运行无测试框架的浏览器检查。C5同源测试从仓库根运行`python firmware-c5/generated/tools/test_v03.py`。

截图位于本机构建目录output/playwright（不提交缓存）；本地release/v0.3.0/source.zip附带v03-harmonics、v03-sweep、v03-desktop与v03-mobile。模拟幅相/THD仅用于界面状态和协议验证，测量数值精度由C5同源数值测试与后续实物记录分别证明。

## 本版待完成的硬件验收

- 本次仅枚举到蓝牙COM3/5/6，没有CH343/COM8；没有烧录ESP v0.3，不声明上板通过。
- 两板均烧录v0.3后，确认版本、IDLE、测试音关闭、48 kHz和扩展能力；GPIO16/17与PA2/3，共地，两板3V3不互连。
- 实际电气回环100/1000/4000 Hz THD及121点幅相、削顶/弱输入判断、校准重复性；记录参考仪表及电平。
- 真机完整谱2049点传输<1秒、UART错误/丢块、C5最坏处理<8 ms；无模拟结果替代。
- 手机热点连续刷新、暂停/导出、扫频取消/重连、NVS校准断电保存/IDLE重装、加载中断后的清除恢复。
- ESP运行最低堆/栈余量、最慢循环和30分钟稳定性；保留真实日志。

以下为历史v0.1记录，资源值不适用于当前v0.3。

---

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

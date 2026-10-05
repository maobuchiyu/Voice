# AcousticLab ESP v0.3.0

ESP32-S3 N16R8 的热点、串口桥接和v0.3 网页。测量值全部来自 CRC 校验通过的 C5 数据；网页不生成模拟测量，也不重新计算 FFT。网页随固件烧录，无需上传文件系统、联网或加载 CDN。

## 功能

- 波形包络、256 点频谱、RMS/峰值、主频有效性、削顶和运行状态。
- 查询、开始 RTA、停止到 IDLE；匹配 REPLY 序号和新的 STATUS，2 秒超时，不自动重发。
- 暂停显示、下一帧 SINGLE、显示 AUTOSET、AC/DC 显示耦合、时基/垂直档位、频谱峰值保持。
- 输入路径、增益备注和显式保存设置到 Preferences；未变化不重复写入 Flash。
- CSV/JSON 显示快照导出，保留各帧的序号、采样位置、时间和输入备注，不宣称不同类型帧同步。
- WebSocket 重连、C5 重启识别、过期快照提示、流量/坏帧/丢块/DAC 错误/堆内存诊断。
- 手机与桌面布局、帮助说明；启动和重连不会自动启动 RTA 或测试音。

## v0.3 测量与操作

需要搭配 C5 v0.3.0（协议扩展 0x0300）。旧 C5 的基础波形/频谱仍兼容，高级控件只在真实能力报告后启用。

- **RTA**：2048/4096 FFT、固定 0.3 秒功率平均、相对 A/C/Z、160–16000 Hz 八个频带。分辨率不足的频带显示不可用。
- **THD**：设置 100–4000 Hz、−40～−6 dBFS，再点“开始 THD”。ESP 先确认测试音，再启动 C5 的约 500 ms H1–H5 测量。输出幅度、dBFS、dBc、幅度比及 THD %/dB；不是 THD+N。削顶、弱输入等无效值显示“—”，导出保留原因。
- **扫频**：先停止当前分析，再开始 20–20000 Hz 的 121 点扫频。显示原始/校准后增益与相位，未校准的点不会冒充校准结果。取消、断线或暂停中漏收的点保留为部分结果。
- **完整谱**：RTA 获得 FFT 后请求，收齐 N/2+1 个 bin（含 DC/Nyquist）才开放 CSV/JSON。与 256 点预览分开，冲突、超时或重连会丢弃未完成数据集。
- **瀑布图**：浏览器保留最多 30 秒、300 份真实预览；断档不补模拟值。
- **校准**：电气回环完整扫频可生成反向幅相补偿草稿；可信麦克风表由 JSON 导入。显式保存 ESP 的 loop/mic 两份表，停止至 IDLE，再加载到 C5。先保存一致的输入路径和增益备注；改变增益/路径时清除旧应用或重新加载。导入/保存不会自动应用，重启不会自动加载或开音。

停止测量回到 IDLE；THD 测试音由独立“关闭”按钮关闭。频率、模式变更会清除实时旧结果；暂停或离线的结果以快照标记，导出含原始有效性和采样位置。物理输入仍通过跳线切换。

校准文件（相位单位为度；C5 上传时转换为弧度）：

```json
{"schema":"acousticlab.calibration.v1","kind":"mic","id":1001,"name":"参考麦克风","source":"厂商校准文件","gainNote":"固定增益","points":[{"frequency":20,"gainDb":0,"phaseDeg":0},{"frequency":20000,"gainDb":0,"phaseDeg":0}]}
```

仅接受 2–121 个递增频点，20–20000 Hz，增益补偿 ±80 dB，相位 ±180°，文件/请求不超过 12 KiB。麦克风曲线须覆盖 20 和 20000 Hz。`gainDb`/`phaseDeg` 是加到测量结果的补偿量，厂商若给的是响应误差，需要先取反。SPL 与衰减入口留在 v0.4，当前相对电平不表示绝对声压。

接口与测试见 [PROTOCOL](PROTOCOL.md)、[VALIDATION](VALIDATION.md)。本版未检测到 CH343/COM8，尚未烧录或完成真机联合验收。

## 构建和烧录

VS Code PlatformIO 打开本目录，Build；关闭占用 COM8 的串口监视器后 Upload，再 Monitor。

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32s3_n16r8
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32s3_n16r8 -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor -p COM8 -b 115200
```

端口变化时修改 `platformio.ini`。PlatformIO Upload 同时处理引导程序、分区和应用；独立 `firmware.bin` 是应用镜像，不能写到地址 0。

固定依赖：Espressif32 7.0.1、Arduino-ESP32 2.0.17、WebSockets 2.7.2、ArduinoJson 6.21.5。16 MB QIO Flash、8 MB OPI PSRAM，应用分区 6.25 MB。用户源码启用 `-Wall -Wextra -Werror`。

## 接线与操作

| ESP | C5 |
|---|---|
| GPIO16 / RX | PA2 / TX |
| GPIO17 / TX | PA3 / RX |
| GND | GND |

双板分别 USB 供电，**不连接两板 3V3**。此前额外 3V3 线拔除后，用户确认查询和 RTA 波形恢复；未通过电源测量确定具体电气机制。UART 为 115200、8N1、3.3 V TTL。

连接 `AcousticLab-xxxx`，密码保留 `12345678`，打开 `http://192.168.4.1/`。自动查询状态后点“开始分析”，等待 C5 真实 RTA 状态。

暂停显示保持网页快照，C5 继续采样；SINGLE 等待所选视图下一帧并保持。停止分析才发送 IDLE。时基只缩放 512 点、约 10.67 ms 的包络窗口，没有硬件触发或连续音频。AC 仅改变显示中心，导出保留收到的原值。输入选择仅记录，实际路径通过跳线切换。

保存设置成功后重启恢复显示配置及测试音候选值，不自动应用到 C5。离线、IDLE、暂停快照明确标记。热点和 WebSocket 限一个测量客户端。

## 目录和验证

- `src/main.cpp`：UART、命令确认、网络、NVS、看门狗。
- `include/protocol.h`：有界解析、CRC 和字段检查。
- `web/`：实际内置网页；`scripts/embed_web.py` 确定性 gzip 打包到 `.pio/generated/web_assets.h`。
- `test/`、`tools/`：本地测试数据和浏览器检查，不进入固件。
- `release/v0.3.0/`：本次构建固件、哈希及构建说明。

本次软件检查与待做的硬件验收见 [VALIDATION.md](VALIDATION.md)。此前最小固件的实物验证不代替本版烧录、NVS 和长期运行验收。

## v0.1.1 刷新修复

HTTP 改为有界非阻塞传输；WebSocket 两槽交接但只保留一个测量客户端。回调栈减负，连接后立即发送诊断。页面及 `/api/info` 增加接收字节、有效帧、坏帧、复位原因、最慢循环和最低栈余量。COM8 上传与启动核对已完成，手机刷新复测待确认。详细变更见 [CHANGELOG.md](CHANGELOG.md)。

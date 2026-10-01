"""AcousticLab C5 简易串口上位机：解码、波形包络和固定命令按钮。"""

from __future__ import annotations

import queue
import threading
import tkinter as tk
from datetime import datetime
from tkinter import messagebox, scrolledtext, ttk

from serial_win import SerialPort, list_ports
from protocol import (COMMAND, LEVEL, REPLY, STATUS, TYPE_NAMES, WAVEFORM,
                      REPLY_NAMES, Frame, FrameParser, decode, encode)


class SerialMonitor:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        root.title("AcousticLab C5 串口上位机")
        root.geometry("980x760")
        self.serial_port = None
        self.stop_event = threading.Event()
        self.events: queue.Queue[tuple[str, object]] = queue.Queue(maxsize=256)
        self.parser = FrameParser()
        self.command_seq = 0
        self.waveform: list[tuple[int, int]] = []
        self.waveform_index = 0
        self.frame_count = 0
        self.port_var = tk.StringVar()
        self.link_var = tk.StringVar(value="未连接")
        self.status_var = tk.StringVar(value="等待 STATUS 帧")
        self.level_var = tk.StringVar(value="等待 LEVEL 帧；点击“实时分析”后固件才会发送")
        self.waveform_var = tk.StringVar(value="等待 WAVEFORM 帧；需刷入包含波形预览的新固件")
        self.count_var = tk.StringVar(value="收到 0 帧，CRC/格式错误 0")
        self._build_ui()
        self.refresh_ports()
        root.after(50, self._drain_events)
        root.protocol("WM_DELETE_WINDOW", self._close)

    def _build_ui(self) -> None:
        controls = ttk.Frame(self.root, padding=10)
        controls.pack(fill="x")
        ttk.Label(controls, text="串口").pack(side="left")
        self.port_list = ttk.Combobox(controls, textvariable=self.port_var, width=18)
        self.port_list.pack(side="left", padx=6)
        ttk.Button(controls, text="刷新", command=self.refresh_ports).pack(side="left")
        self.connect_button = ttk.Button(controls, text="连接", command=self.toggle_connection)
        self.connect_button.pack(side="left", padx=8)
        ttk.Label(controls, text="115200 8N1", foreground="#555").pack(side="left", padx=8)
        ttk.Label(controls, textvariable=self.link_var).pack(side="right")

        commands = ttk.LabelFrame(self.root, text="固定指令", padding=10)
        commands.pack(fill="x", padx=10, pady=4)
        self.command_buttons = [
            ttk.Button(commands, text="查询状态", command=lambda: self.send_command("查询状态", b"\x01")),
            ttk.Button(commands, text="停止分析 / IDLE", command=lambda: self.send_command("IDLE", b"\x02\x00")),
            ttk.Button(commands, text="实时分析 / RTA", command=lambda: self.send_command("RTA", b"\x02\x01")),
        ]
        for button in self.command_buttons:
            button.pack(side="left", padx=(0, 12))
            button.configure(state="disabled")
        ttk.Label(commands, text="命令帧自动填序号与 CRC；REPLY 显示在日志中。",
                  foreground="#555").pack(side="left")

        info = ttk.LabelFrame(self.root, text="解码结果", padding=10)
        info.pack(fill="x", padx=10, pady=4)
        for variable in (self.status_var, self.level_var, self.waveform_var, self.count_var):
            ttk.Label(info, textvariable=variable, anchor="w").pack(fill="x", pady=2)

        chart = ttk.LabelFrame(self.root, text="ADC 波形包络：64 组 min/max，每组覆盖 8 个采样；不是逐点原始波形", padding=8)
        chart.pack(fill="both", expand=True, padx=10, pady=4)
        self.canvas = tk.Canvas(chart, background="#101927", highlightthickness=0, height=300)
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", lambda _: self._draw_waveform())

        logs = ttk.LabelFrame(self.root, text="收发日志（最近 100 条）", padding=8)
        logs.pack(fill="both", expand=True, padx=10, pady=(4, 10))
        self.log = scrolledtext.ScrolledText(logs, height=10, state="disabled", font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)
        self._draw_waveform()

    def refresh_ports(self) -> None:
        ports = list_ports()
        self.port_list["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(ports[0])

    def toggle_connection(self) -> None:
        if self.serial_port is not None:
            self.disconnect()
            return
        port = self.port_var.get().strip()
        if not port:
            messagebox.showwarning("串口未选择", "请先选择 COM 口。")
            return
        try:
            self.serial_port = SerialPort(port)
        except Exception as exc:
            messagebox.showerror("连接失败", str(exc))
            return
        self.stop_event.clear()
        self.parser = FrameParser()
        self.frame_count = 0
        self.connect_button.configure(text="断开")
        for button in self.command_buttons:
            button.configure(state="normal")
        self.link_var.set(f"已连接 {port}")
        self._append_log(f"已连接 {port}，115200 8N1")
        threading.Thread(target=self._read_loop, daemon=True).start()

    def disconnect(self) -> None:
        self.stop_event.set()
        port = self.serial_port
        self.serial_port = None
        if port is not None:
            try:
                port.close()
            except Exception:
                pass
        self.connect_button.configure(text="连接")
        for button in self.command_buttons:
            button.configure(state="disabled")
        self.link_var.set("未连接")
        self._append_log("串口已断开")

    def _read_loop(self) -> None:
        port = self.serial_port
        while not self.stop_event.is_set():
            try:
                chunk = port.read(1024)
                if chunk:
                    for frame in self.parser.feed(chunk):
                        self._queue_event("frame", frame)
                    self._queue_event("errors", self.parser.bad_frames)
            except Exception as exc:
                if not self.stop_event.is_set():
                    self._queue_event("error", str(exc))
                break

    def _queue_event(self, kind: str, value: object) -> None:
        try:
            self.events.put_nowait((kind, value))
        except queue.Full:
            # 图形只需最近数据；接收线程不能因界面刷新而阻塞串口。
            try:
                self.events.get_nowait()
                self.events.put_nowait((kind, value))
            except queue.Empty:
                pass

    def _drain_events(self) -> None:
        for _ in range(100):
            try:
                kind, value = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "frame":
                self._handle_frame(value)
            elif kind == "errors":
                self.count_var.set(f"收到 {self.frame_count} 帧，CRC/格式错误 {value}")
            elif kind == "error":
                self._append_log(f"串口错误：{value}")
                self.disconnect()
        self.root.after(50, self._drain_events)

    def _handle_frame(self, frame: Frame) -> None:
        self.frame_count += 1
        try:
            fields = decode(frame)
        except ValueError as exc:
            self._append_log(f"{TYPE_NAMES.get(frame.type, frame.type)} 负载错误：{exc}")
            return
        if frame.type == STATUS:
            mode = "RTA" if fields["mode"] == 1 else "IDLE" if fields["mode"] == 0 else str(fields["mode"])
            self.status_var.set(
                f"STATUS 序号={frame.seq}  模式={mode}  采样率={fields['sample_rate']} Hz  "
                f"块数={fields['completed_blocks']}  丢块={fields['lost_blocks']}  "
                f"DAC错误={fields['dac_errors']}  接收坏帧={fields['bad_frames']}  "
                f"运行={fields['uptime_ms']/1000:.1f} s")
            self._append_log(f"STATUS #{frame.seq} 采样序号={frame.sample_index} 模式={mode}")
        elif frame.type == LEVEL:
            freq = f"{fields['main_frequency_hz']:.1f} Hz" if fields["flags"] & 1 else "未实现"
            self.level_var.set(
                f"LEVEL RMS={fields['rms_dbfs']:.1f} dBFS  峰值={fields['peak_dbfs']:.1f} dBFS  "
                f"主频={freq}  削顶样本={fields['clipped_samples']}  最慢块≈{fields['worst_block_us']} µs")
        elif frame.type == WAVEFORM:
            self.waveform = fields["min_max"]
            self.waveform_index = frame.sample_index
            self.waveform_var.set(
                f"WAVEFORM 首样本={frame.sample_index}  64 组 × 8 点 = 512 点 / 10.67 ms  "
                f"ADC 去中点码值范围 -2048～2047")
            self._draw_waveform()
        elif frame.type == REPLY:
            result = REPLY_NAMES.get(fields["result"], str(fields["result"]))
            self._append_log(
                f"REPLY 对应命令序号={frame.seq} 命令={fields['command']} "
                f"结果={result} 当前模式={fields['current_mode']}")
        else:
            self._append_log(f"{TYPE_NAMES.get(frame.type, frame.type)} #{frame.seq}: {fields}")
        self.count_var.set(f"收到 {self.frame_count} 帧，CRC/格式错误 {self.parser.bad_frames}")

    def _draw_waveform(self) -> None:
        canvas = self.canvas
        canvas.delete("all")
        width = max(canvas.winfo_width(), 300)
        height = max(canvas.winfo_height(), 160)
        left, right, top, bottom = 46, width - 14, 14, height - 28
        canvas.create_rectangle(left, top, right, bottom, outline="#40506a")
        for value in (-2048, -1024, 0, 1024, 2047):
            y = bottom - (value + 2048) / 4095 * (bottom - top)
            canvas.create_line(left, y, right, y, fill="#263447")
            canvas.create_text(left - 5, y, text=str(value), fill="#a8bacd", anchor="e", font=("Consolas", 9))
        canvas.create_text(left, bottom + 13, text="0 ms", fill="#a8bacd", anchor="w")
        canvas.create_text(right, bottom + 13, text="10.67 ms", fill="#a8bacd", anchor="e")
        if not self.waveform:
            canvas.create_text((left + right) / 2, (top + bottom) / 2,
                               text="等待真实波形包络", fill="#a8bacd")
            return
        for index, (minimum, maximum) in enumerate(self.waveform):
            x = left + (index + 0.5) / len(self.waveform) * (right - left)
            y_low = bottom - (minimum + 2048) / 4095 * (bottom - top)
            y_high = bottom - (maximum + 2048) / 4095 * (bottom - top)
            canvas.create_line(x, y_low, x, y_high, fill="#41d6c3", width=3)

    def send_command(self, name: str, payload: bytes) -> None:
        if self.serial_port is None:
            return
        self.command_seq = (self.command_seq + 1) & 0xFFFF
        frame = encode(COMMAND, self.command_seq, payload)
        try:
            self.serial_port.write(frame)
            self._append_log(f"TX {name} #{self.command_seq}: {frame.hex(' ').upper()}")
        except Exception as exc:
            self._append_log(f"发送失败：{exc}")
            self.disconnect()

    def _append_log(self, message: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", f"{datetime.now():%H:%M:%S}  {message}\n")
        if int(self.log.index("end-1c").split(".")[0]) > 100:
            self.log.delete("1.0", "2.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _close(self) -> None:
        self.disconnect()
        self.root.destroy()


if __name__ == "__main__":
    window = tk.Tk()
    SerialMonitor(window)
    window.mainloop()

"""AcousticLab C5 串口帧编解码；可独立于界面测试。"""

from __future__ import annotations

import struct
from dataclasses import dataclass

SYNC = b"\xA5\x5A"
VERSION = 1
OVERHEAD = 14
MAX_PAYLOAD = 1010  # 固件 TX 缓冲共 1024 字节

STATUS = 1
LEVEL = 2
WAVEFORM = 4
COMMAND = 7
REPLY = 8

TYPE_NAMES = {1: "STATUS", 2: "LEVEL", 3: "SPECTRUM_PREVIEW",
              4: "WAVEFORM", 5: "MEASUREMENT", 6: "DATA_CHUNK",
              7: "COMMAND", 8: "REPLY"}
REPLY_NAMES = {0: "成功", 1: "长度错误", 2: "参数错误", 3: "不支持的命令"}


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE，覆盖版本字段至负载末尾。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass(frozen=True)
class Frame:
    type: int
    seq: int
    sample_index: int
    payload: bytes


def encode(type_: int, seq: int, payload: bytes = b"", sample_index: int = 0) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("负载过长")
    body = struct.pack("<BBHHI", VERSION, type_, seq & 0xFFFF,
                       len(payload), sample_index & 0xFFFFFFFF) + payload
    return SYNC + body + struct.pack("<H", crc16(body))


class FrameParser:
    """接收任意分片的字节流；坏帧从下一字节重新寻找同步头。"""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.bad_frames = 0

    def feed(self, data: bytes) -> list[Frame]:
        self.buffer.extend(data)
        frames: list[Frame] = []
        while len(self.buffer) >= 2:
            offset = self.buffer.find(SYNC)
            if offset < 0:
                # 留下末尾 A5，供下次接收的 5A 配对。
                self.buffer[:] = self.buffer[-1:] if self.buffer[-1] == 0xA5 else b""
                break
            if offset:
                del self.buffer[:offset]
            if len(self.buffer) < 8:
                break
            version, type_, seq, length = struct.unpack_from("<BBHH", self.buffer, 2)
            if version != VERSION or length > MAX_PAYLOAD:
                self.bad_frames += 1
                del self.buffer[0]
                continue
            frame_length = OVERHEAD + length
            if len(self.buffer) < frame_length:
                break
            expected = struct.unpack_from("<H", self.buffer, frame_length - 2)[0]
            actual = crc16(self.buffer[2:frame_length - 2])
            if actual != expected:
                self.bad_frames += 1
                del self.buffer[0]
                continue
            sample_index = struct.unpack_from("<I", self.buffer, 8)[0]
            frames.append(Frame(type_, seq, sample_index,
                                bytes(self.buffer[12:frame_length - 2])))
            del self.buffer[:frame_length]
        return frames


def decode(frame: Frame) -> dict:
    """按当前固件版本解释常用负载；不支持的类型保留原始十六进制。"""
    payload = frame.payload
    if frame.type == STATUS:
        if len(payload) not in (32, 40):
            raise ValueError("STATUS 负载长度不是 32 或 40 字节")
        mode, running, flags, sample_rate, fft_length, reserved, blocks, lost, dac_errors, bad, uptime = struct.unpack_from(
            "<BBHIHHIIIII", payload)
        fields = dict(mode=mode, audio_running=running, flags=flags,
                    sample_rate=sample_rate, fft_length=fft_length, reserved=reserved,
                    completed_blocks=blocks, lost_blocks=lost,
                    dac_errors=dac_errors, bad_frames=bad, uptime_ms=uptime)
        if len(payload) == 40:
            frequency, level, _ = struct.unpack_from("<IhH", payload, 32)
            fields.update(tone_frequency_hz=frequency, tone_peak_dbfs=level / 100,
                          tone_enabled=bool(flags & 0x200))
        return fields
    if frame.type == LEVEL:
        if len(payload) != 32:
            raise ValueError("LEVEL 负载长度不是 32 字节")
        rms, peak, frequency, clipped, flags, lost, worst, uptime = struct.unpack("<fffIIIII", payload)
        return dict(rms_dbfs=rms, peak_dbfs=peak, main_frequency_hz=frequency,
                    clipped_samples=clipped, flags=flags, lost_blocks=lost,
                    worst_block_us=worst, uptime_ms=uptime)
    if frame.type == WAVEFORM:
        if len(payload) < 4:
            raise ValueError("WAVEFORM 负载过短")
        source_count, count = struct.unpack_from("<HH", payload)
        if source_count != 512 or count != 64 or len(payload) != 4 + count * 4:
            raise ValueError("WAVEFORM 组数或长度与当前固件不符")
        values = struct.unpack_from("<" + "hh" * count, payload, 4)
        pairs = list(zip(values[::2], values[1::2]))
        if any(low > high or low < -2048 or high > 2047 for low, high in pairs):
            raise ValueError("WAVEFORM ADC 数值超出范围")
        return dict(source_count=source_count, group_count=count,
                    samples_per_group=source_count // count,
                    min_max=pairs)
    if frame.type == REPLY:
        if len(payload) != 4:
            raise ValueError("REPLY 负载长度不是 4 字节")
        return dict(command=payload[0], result=payload[1], current_mode=payload[2])
    return dict(raw_hex=payload.hex(" ").upper())

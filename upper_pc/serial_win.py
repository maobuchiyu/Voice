"""仅用 Windows 标准 API 访问 COM 口，避免安装额外串口库。"""

from __future__ import annotations

import ctypes
import winreg
from ctypes import wintypes

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
MAXDWORD = 0xFFFFFFFF
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


class DCB(ctypes.Structure):
    _fields_ = [
        ("DCBlength", wintypes.DWORD), ("BaudRate", wintypes.DWORD),
        ("Flags", wintypes.DWORD), ("wReserved", wintypes.WORD),
        ("XonLim", wintypes.WORD), ("XoffLim", wintypes.WORD),
        ("ByteSize", wintypes.BYTE), ("Parity", wintypes.BYTE),
        ("StopBits", wintypes.BYTE), ("XonChar", ctypes.c_char),
        ("XoffChar", ctypes.c_char), ("ErrorChar", ctypes.c_char),
        ("EofChar", ctypes.c_char), ("EvtChar", ctypes.c_char),
        ("wReserved1", wintypes.WORD),
    ]


class COMMTIMEOUTS(ctypes.Structure):
    _fields_ = [
        ("ReadIntervalTimeout", wintypes.DWORD),
        ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
        ("ReadTotalTimeoutConstant", wintypes.DWORD),
        ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
        ("WriteTotalTimeoutConstant", wintypes.DWORD),
    ]


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.HANDLE)
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.GetCommState.argtypes = (wintypes.HANDLE, ctypes.POINTER(DCB))
kernel32.SetCommState.argtypes = (wintypes.HANDLE, ctypes.POINTER(DCB))
kernel32.SetCommTimeouts.argtypes = (wintypes.HANDLE, ctypes.POINTER(COMMTIMEOUTS))
kernel32.ReadFile.argtypes = (wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                              ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID)
kernel32.WriteFile.argtypes = kernel32.ReadFile.argtypes
kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)


def _check(ok: int) -> None:
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error())


def list_ports() -> list[str]:
    """从 Windows 串口设备映射读取 COM 名称。"""
    ports = []
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r"HARDWARE\DEVICEMAP\SERIALCOMM") as key:
            index = 0
            while True:
                try:
                    _, name, _ = winreg.EnumValue(key, index)
                    ports.append(name)
                    index += 1
                except OSError:
                    break
    except OSError:
        pass
    return sorted(set(ports), key=lambda name: (not name.upper().startswith("COM"), name))


class SerialPort:
    def __init__(self, name: str) -> None:
        self.handle = kernel32.CreateFileW("\\\\.\\" + name, GENERIC_READ | GENERIC_WRITE,
                                           0, None, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, None)
        if self.handle == INVALID_HANDLE_VALUE:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            state = DCB()
            state.DCBlength = ctypes.sizeof(DCB)
            _check(kernel32.GetCommState(self.handle, ctypes.byref(state)))
            state.BaudRate = 115200
            state.Flags = 1  # fBinary=1，其余流控标志清零。
            state.ByteSize = 8
            state.Parity = 0
            state.StopBits = 0
            _check(kernel32.SetCommState(self.handle, ctypes.byref(state)))
            timeouts = COMMTIMEOUTS(MAXDWORD, 0, 100, 0, 1000)
            _check(kernel32.SetCommTimeouts(self.handle, ctypes.byref(timeouts)))
        except Exception:
            self.close()
            raise

    def read(self, size: int) -> bytes:
        buffer = ctypes.create_string_buffer(size)
        count = wintypes.DWORD()
        _check(kernel32.ReadFile(self.handle, buffer, size, ctypes.byref(count), None))
        return buffer.raw[:count.value]

    def write(self, data: bytes) -> None:
        buffer = ctypes.create_string_buffer(data)
        count = wintypes.DWORD()
        _check(kernel32.WriteFile(self.handle, buffer, len(data), ctypes.byref(count), None))
        if count.value != len(data):
            raise OSError(f"串口只发送了 {count.value}/{len(data)} 字节")

    def close(self) -> None:
        if self.handle is not None:
            kernel32.CloseHandle(self.handle)
            self.handle = None

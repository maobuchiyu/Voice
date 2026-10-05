"""在 PC 编译并执行同一份 C/CMSIS 算法，验证 v0.1 数值和数据格式。"""
from pathlib import Path
import argparse
import ctypes as c
import math
import os
import struct
import subprocess
import shutil
import unittest

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--gcc', default=r'E:\cclion\CLion 2026.2.1\bin\mingw\bin\gcc.exe')
args, unused = parser.parse_known_args()
gcc = Path(args.gcc)
os.environ['PATH'] = str(gcc.parent) + os.pathsep + os.environ['PATH']
build = ROOT / 'build' / 'host-v01'
build.mkdir(parents=True, exist_ok=True)
source = ROOT / 'arch' / 'cmsis_dsp' / 'Source'
sources = [ROOT/'app'/name for name in ('generator.c', 'dsp.c', 'protocol_codec.c', 'calibration.c', 'measurement.c')]
sources += [source/'TransformFunctions'/name for name in (
    'arm_rfft_fast_init_f32.c', 'arm_rfft_fast_f32.c', 'arm_cfft_init_f32.c',
    'arm_cfft_f32.c', 'arm_cfft_radix8_f32.c', 'arm_bitreversal2.c')]
sources += [source/'CommonTables'/name for name in ('arm_common_tables.c', 'arm_const_structs.c')]
library = build/'algorithms.dll'
subprocess.run([str(gcc), '-shared', '-O2', '-Wall', '-Werror', '-D__GNUC_PYTHON__',
                '-static-libgcc', '-I'+str(ROOT/'app'),
                '-I'+str(ROOT/'arch/cmsis_dsp/Include'),
                '-I'+str(ROOT/'arch/cmsis_dsp/PrivateInclude'),
                *map(str,sources), '-o', str(library), '-lm'], check=True)
lib = c.CDLL(str(library))
# 第二个库包含真实 Audio/Protocol 主循环，仅外设由测试替身驱动。
integration_library = build/'integration.dll'
subprocess.run([str(gcc), '-shared', '-O2', '-Wall', '-Werror', '-D__GNUC_PYTHON__',
                '-static-libgcc', '-I'+str(ROOT/'tools/host'), '-I'+str(ROOT/'app'),
                '-I'+str(ROOT/'arch/cmsis_dsp/Include'), '-I'+str(ROOT/'arch/cmsis_dsp/PrivateInclude'),
                *map(str,sources), str(ROOT/'app/audio.c'), str(ROOT/'app/protocol_uart.c'),
                str(ROOT/'tools/host/peripherals.c'), '-o',str(integration_library),'-lm'],check=True)
integration = c.CDLL(str(integration_library))
F512 = c.c_float * 512
U512 = c.c_uint16 * 512
I256 = c.c_int16 * 256

class Tone(c.Structure):
    _fields_ = [('frequency_hz', c.c_uint32), ('peak_centidb', c.c_int16), ('enabled', c.c_uint8)]

lib.generator_request.argtypes = [c.POINTER(Tone)]
lib.generator_get_config.argtypes = [c.POINTER(Tone)]
lib.generator_render.argtypes = [c.POINTER(c.c_uint16), c.c_uint32]
lib.dsp_push.argtypes = [c.POINTER(c.c_float), c.c_uint32, c.c_uint32]
lib.dsp_get_spectrum.argtypes = [c.POINTER(c.c_int16), c.POINTER(c.c_uint32)]
lib.dsp_get_frequency.restype = c.c_float
lib.protocol_crc16.argtypes = [c.c_void_p, c.c_uint16]
lib.protocol_crc16.restype = c.c_uint16
lib.protocol_encode.argtypes = [c.c_void_p, c.c_uint8, c.c_uint16, c.c_uint32, c.c_void_p, c.c_uint16]
lib.protocol_encode.restype = c.c_uint16

def feed_sine(frequency, amplitude, blocks=8, dc=0.0):
    for block in range(blocks):
        values = F512(*(dc + amplitude*math.sin(2*math.pi*frequency*(block*512+i)/48000) for i in range(512)))
        lib.dsp_push(values, 512, (block+1)*512)
    levels, index = I256(), c.c_uint32()
    ready = lib.dsp_get_spectrum(levels, c.byref(index))
    return ready, list(levels), index.value

class V01Tests(unittest.TestCase):
    def setUp(self):
        self.assertEqual(lib.dsp_init(), 0)
        lib.generator_init()
        lib.generator_request(c.byref(Tone(1000,-1200,0)))
        lib.generator_render(U512(),512)

    def test_fft_amplitude_and_frequency(self):
        for bin_ in (1,32,128,129,500,850):
            lib.dsp_reset()
            hz = bin_*48000/2048
            ready, levels, _ = feed_sine(hz,10**(-12/20))
            self.assertEqual(ready,1)
            self.assertLess(abs(max(levels)/100+12),0.2)
            self.assertLess(abs(lib.dsp_get_frequency()-hz),1.0)
        lib.dsp_reset()
        feed_sine(1000,0.25)
        self.assertLess(abs(lib.dsp_get_frequency()-1000),1.0)

    def test_overlap_dc_silence_and_reset(self):
        index = c.c_uint32()
        self.assertEqual(feed_sine(750,0.25,blocks=3)[0],0)
        lib.dsp_push(F512(),512,2048)
        self.assertEqual(lib.dsp_get_spectrum(I256(),c.byref(index)),1)
        self.assertEqual(index.value,2048)
        lib.dsp_push(F512(),512,2560)
        lib.dsp_get_spectrum(I256(),c.byref(index))
        self.assertEqual(index.value,2048)
        lib.dsp_push(F512(),512,3072)
        lib.dsp_get_spectrum(I256(),c.byref(index))
        self.assertEqual(index.value,3072)
        lib.dsp_reset()
        self.assertEqual(lib.dsp_get_spectrum(I256(),c.byref(index)),0)
        ready, levels, _ = feed_sine(750,0.0,dc=0.4)
        self.assertEqual(ready,1)
        self.assertEqual(max(levels),-12000)
        self.assertEqual(lib.dsp_frequency_valid(),0)

    def test_nyquist_scaling_and_mapping(self):
        for block in range(4):
            lib.dsp_push(F512(*(0.5 if i%2==0 else -0.5 for i in range(512))),512,(block+1)*512)
        levels, index = I256(), c.c_uint32()
        lib.dsp_get_spectrum(levels,c.byref(index))
        self.assertLess(abs(levels[-1]/100 - 20*math.log10(0.5)),0.02)
        # 固定区间覆盖每个正频率 bin，且没有重叠和空隙。
        bins = list(range(1,129))
        for j in range(128):
            bins.extend(range(129+j*896//128,129+(j+1)*896//128))
        self.assertEqual(bins,list(range(1,1025)))

    def test_dds_ramp_level_stop_and_limits(self):
        lib.generator_request(c.byref(Tone(1000,-1200,1)))
        values = U512()
        lib.generator_render(values,512)
        self.assertLess(abs(values[0]-2048),5)
        lib.generator_render(values,512)
        amplitude = max(abs(value-2048) for value in values)/2048
        self.assertLess(abs(20*math.log10(amplitude)+12),0.05)
        lib.generator_request(c.byref(Tone(1000,-1200,0)))
        lib.generator_render(values,512)
        self.assertEqual(list(values)[480:],[2048]*32)
        lib.generator_render(values,512)
        self.assertEqual(list(values),[2048]*512)
        for hz, level in ((20,-4000),(20000,-600)):
            lib.generator_request(c.byref(Tone(hz,level,1)))
            lib.generator_render(values,512)
            self.assertTrue(all(0 <= value <= 4095 for value in values))

    def test_crc_command_and_spectrum_frame(self):
        known = bytes.fromhex('01 07 34 12 01 00 00 00 00 00 01')
        self.assertEqual(lib.protocol_crc16(known,len(known)),0x6203)
        payload = struct.pack('<IBBH',48000,0,8,256) # 编码只验证字节保持，不解释负载。
        out = (c.c_uint8*1024)()
        count = lib.protocol_encode(out,3,65535,0xFFFFFFFF,payload,len(payload))
        self.assertEqual(count,len(payload)+14)
        raw = bytes(out[:count])
        self.assertEqual(raw[:2],b'\xA5\x5A')
        self.assertEqual(raw[12:-2],payload)
        self.assertEqual(int.from_bytes(raw[-2:],'little'),lib.protocol_crc16(raw[2:-2],count-4))
        self.assertEqual(lib.protocol_encode(out,3,1,0,b'X'*1011,1011),0)

    def test_audio_uart_commands_rate_rms_and_fault(self):
        app = integration
        app.host_block.argtypes = [c.POINTER(c.c_uint16),c.c_uint32]
        app.host_receive.argtypes = [c.c_void_p,c.c_uint32]
        app.host_copy_tx.argtypes = [c.c_void_p]
        self.assertEqual(app.audio_start(),0)
        self.assertEqual(app.protocol_uart_start(),0)
        def frame(seq,payload):
            body=struct.pack('<BBHHI',1,7,seq,len(payload),0)+payload
            crc=lib.protocol_crc16(body,len(body))
            return b'\xA5\x5A'+body+struct.pack('<H',crc)
        # 两个命令连续到达时，每个 REPLY 保留自身序号；非法参数不改变 DDS。
        incoming=frame(10,b'\x03\x01'+struct.pack('<Ih',19,-1200))+frame(11,b'\x02\x01')
        app.host_receive(incoming,len(incoming))
        app.protocol_uart_poll()
        out=(c.c_uint8*1024)()
        n=app.host_copy_tx(out)
        self.assertEqual(bytes(out[:n])[3],8)
        self.assertEqual(bytes(out[:n])[4:6],b'\x0a\x00')
        self.assertEqual(out[13],2)
        app.host_tick(10)
        app.protocol_uart_poll()
        n=app.host_copy_tx(out)
        self.assertEqual(bytes(out[:n])[4:6],b'\x0b\x00')
        self.assertEqual(out[13],0)
        # 运行 60 秒；按每 512 点的真实时刻提供输入，每毫秒轮询主循环。
        samples=U512(*(round(2048+2048*0.25*math.sin(2*math.pi*750*i/48000)) for i in range(512)))
        next_block=1
        for t in range(11,60011):
            app.host_tick(t)
            if t*48 >= next_block*512:
                app.host_block(samples,(next_block-1)%2)
                next_block+=1
            app.audio_poll()
            app.protocol_uart_poll()
        for type_ in (2,3,4):
            self.assertGreaterEqual(app.host_count(type_),590)
            self.assertLessEqual(app.host_count(type_),601)
        self.assertLess(app.host_total_bytes()/60,9000)
        print('60 秒虚拟串口：LEVEL=%d SPECTRUM=%d WAVEFORM=%d，%.1f B/s' %
              (app.host_count(2),app.host_count(3),app.host_count(4),app.host_total_bytes()/60))
        self.assertEqual(app.audio_is_running(),1)
        self.assertGreater(app.host_feeds(),5500)
        # 请求最新 LEVEL，检查真实 Audio RMS/FFT 字段及丢块统计。
        for t in range(60011,60211):
            app.host_tick(t)
            if t*48 >= next_block*512:
                app.host_block(samples,(next_block-1)%2)
                next_block+=1
            app.audio_poll(); app.protocol_uart_poll()
            n=app.host_copy_tx(out)
            if out[3]==2:
                rms,peak,hz,clipped,flags,lost,worst,uptime=struct.unpack('<fffIIIII',bytes(out[12:44]))
                self.assertLess(abs(rms-20*math.log10(0.25/math.sqrt(2))),0.1)
                self.assertLess(abs(hz-750),1)
                self.assertEqual(clipped,0); self.assertEqual(lost,0)
                self.assertEqual(flags & 1,1)
                break
        else:
            self.fail('缺少 LEVEL 帧')
        # ADC 故障后停止输出，不再喂狗，不将缓存频率作为有效结果。
        before=app.host_feeds()
        app.host_adc_fault(); app.audio_poll()
        self.assertEqual(app.audio_is_running(),0)
        self.assertEqual(app.host_stopped(),1)
        self.assertEqual(app.host_feeds(),before)

    def test_tone_commit_status_and_missed_output_deadline(self):
        # 独立库实例提供上电静态状态，不向正式固件加入测试重置接口。
        isolated = build/'deadline.dll'
        shutil.copyfile(integration_library,isolated)
        app=c.CDLL(str(isolated))
        app.host_block.argtypes=[c.POINTER(c.c_uint16),c.c_uint32]
        app.host_receive.argtypes=[c.c_void_p,c.c_uint32]
        self.assertEqual(app.audio_start(),0)
        self.assertEqual(app.protocol_uart_start(),0)
        payload=b'\x03\x01'+struct.pack('<Ih',1000,-1200)
        body=struct.pack('<BBHHI',1,7,123,len(payload),0)+payload
        incoming=b'\xA5\x5A'+body+struct.pack('<H',lib.protocol_crc16(body,len(body)))
        app.host_receive(incoming,len(incoming)); app.protocol_uart_poll()
        samples=U512(*([2048]*512))
        app.host_tick(11); app.host_block(samples,0); app.audio_poll(); app.protocol_uart_poll()
        out=(c.c_uint8*1024)()
        app.host_copy_tx(out)
        self.assertEqual(out[3],1)
        flags=int.from_bytes(bytes(out[14:16]),'little')
        self.assertEqual(flags & 0x300,0x300)
        self.assertEqual(struct.unpack('<Ih',bytes(out[44:50])),(1000,-1200))
        # 跳过主循环两半区：检测已重放旧DAC数据，立即停输出而非继续伪正常运行。
        app.host_block(samples,1); app.host_block(samples,0); app.audio_poll()
        self.assertEqual(app.audio_is_running(),0)
        self.assertEqual(app.host_stopped(),1)

if __name__ == '__main__':
    unittest.main(argv=['test_v01.py',*unused],verbosity=2)

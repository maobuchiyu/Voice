"""v0.3同源C算法与虚拟外设验证；运行时不连接开发板。"""
import ctypes as c
import math
import shutil
import struct
import unittest
import subprocess
import json
import os
import test_v01 as base

lib=base.lib
esp_header=base.ROOT.parents[1]/'esp/videoesp/include'
esp_library=base.build/'esp-validator.dll'
validator_options=['-I'+str(esp_header),'-DC5_ESP_HEADER="protocol.h"'] if os.environ.get('C5_LIVE_ESP')=='1' else []
subprocess.run([str(base.gcc.with_name('g++.exe')),'-shared','-O2','-static-libgcc','-static-libstdc++',
                *validator_options,str(base.ROOT/'tools/host/esp_protocol.cpp'),'-o',str(esp_library)],check=True)
esp=c.CDLL(str(esp_library)); esp.host_esp_valid.argtypes=[c.c_void_p,c.c_uint32]
class Reference(c.Structure):
    _fields_=[('start',c.c_uint32),('phase',c.c_uint32),('step',c.c_uint32),('generation',c.c_uint32),
              ('gain',c.c_float),('enabled',c.c_uint8),('steady',c.c_uint8)]
class Harmonic(c.Structure):
    _fields_=[(name,c.c_float) for name in ('frequency','amplitude','dbfs','dbc','percent','phase','corrected_dbfs','corrected_phase')]
class Result(c.Structure):
    _fields_=[('sequence',c.c_uint32),('end_index',c.c_uint32),('samples',c.c_uint32),('calibration_id',c.c_uint32),
              ('point',c.c_uint16),('point_count',c.c_uint16),('reason',c.c_uint16),('kind',c.c_uint8),('mask',c.c_uint8),
              *[(name,c.c_float) for name in ('frequency','transfer_db','transfer_phase','thd_percent','thd_db')],('harmonic',Harmonic*5)]
class Power(c.Structure):
    _fields_=[('z',c.c_float),('a',c.c_float),('c',c.c_float),('bands',c.c_float*8),('index',c.c_uint32),('valid',c.c_uint16)]
class CalPoint(c.Structure):
    _fields_=[('hz',c.c_float),('db',c.c_float),('phase',c.c_float)]
lib.generator_render_at.argtypes=[c.POINTER(c.c_uint16),c.c_uint32,c.c_uint32]
lib.generator_reference.argtypes=[c.c_uint32,c.POINTER(Reference)]
lib.measurement_push.argtypes=[c.POINTER(c.c_float),c.c_uint32,c.c_uint32,c.POINTER(Reference),c.c_uint32]
lib.measurement_get.argtypes=[c.POINTER(Result)]
lib.calibration_begin.argtypes=[c.c_uint32,c.c_uint8,c.c_uint32,c.c_uint16]
lib.calibration_append.argtypes=[c.c_uint16,c.POINTER(CalPoint),c.c_uint16]
lib.calibration_apply.argtypes=[c.c_float,c.POINTER(c.c_float),c.POINTER(c.c_float)]
lib.calibration_set_spl.argtypes=[c.c_float,c.c_uint32]
lib.calibration_spl.argtypes=[c.c_float,c.POINTER(c.c_float)]
lib.dsp_get_power.argtypes=[c.POINTER(Power)]
lib.dsp_snapshot_read.argtypes=[c.c_uint16,c.POINTER(c.c_int16),c.c_uint16]

def measured_block(index, amplitudes=(.25,.005,.0025,.001,.0005), phases=(.4,.2,-.3,.1,.7), clipped=0):
    lib.generator_render_at(base.U512(),512,index)
    ref=Reference()
    assert lib.generator_reference(index,c.byref(ref))==1
    values=base.F512(*(0.3+sum(a*math.sin((h+1)*((ref.phase+i*ref.step)%2**32)*2*math.pi/2**32+phases[h])
                               for h,a in enumerate(amplitudes)) for i in range(512)))
    lib.measurement_push(values,512,index,c.byref(ref),clipped)
    result=Result()
    return result if lib.measurement_get(c.byref(result)) else None

class V03Tests(unittest.TestCase):
    def setUp(self):
        self.assertEqual(lib.dsp_init(),0)
        lib.generator_init(); lib.calibration_init(); lib.measurement_init()

    def test_4096_fft_power_mapping_snapshot(self):
        self.assertEqual(lib.dsp_set_length(4096),0)
        hz=85*48000/4096
        ready,levels,_=base.feed_sine(hz,.25,blocks=24)
        self.assertTrue(ready)
        self.assertLess(abs(max(levels)/100-20*math.log10(.25)),.2)
        self.assertLess(abs(lib.dsp_get_frequency()-hz),1)
        p=Power(); self.assertEqual(lib.dsp_get_power(c.byref(p)),1)
        self.assertLess(abs(p.z-20*math.log10(.25/math.sqrt(2))),.1)
        self.assertLess(abs(p.a-p.z),.1); self.assertLess(abs(p.c-p.z),.1)
        self.assertEqual(p.valid,255)
        self.assertEqual(lib.dsp_freeze(),0)
        original_index=lib.dsp_result_index()
        self.assertNotEqual(lib.dsp_set_length(2048),0)
        values=(c.c_int16*2049)()
        self.assertEqual(lib.dsp_snapshot_read(0,values,2049),2049)
        self.assertLess(abs(values[85]/100-20*math.log10(.25)),.2)
        base.feed_sine(hz,.05,blocks=12)
        newer=(c.c_int16*2049)(); lib.dsp_snapshot_read(0,newer,2049)
        self.assertEqual(list(newer),list(values)); self.assertEqual(lib.dsp_result_index(),original_index)
        lib.dsp_release(); self.assertEqual(lib.dsp_is_frozen(),0)
        self.assertEqual(lib.dsp_snapshot_read(0,newer,2049),0)

    def test_thd_phase_and_noninteger_cycles(self):
        for hz in (100,1000,1001,4000):
            lib.generator_init(); lib.measurement_init()
            lib.generator_request(c.byref(base.Tone(hz,-1200,1)))
            lib.generator_render_at(base.U512(),512,0)
            self.assertEqual(lib.measurement_start(1,-1200),0)
            result=None
            for index in range(512,40000,512):
                result=measured_block(index)
                if result: break
            self.assertIsNotNone(result)
            self.assertEqual(result.reason,0); self.assertEqual(result.samples,24000); self.assertEqual(result.mask,31)
            expected=100*math.sqrt(.005**2+.0025**2+.001**2+.0005**2)/.25
            self.assertLess(abs(result.thd_percent/expected-1),.05)
            self.assertLess(abs(result.harmonic[0].phase-.4),.002)
            for h,amplitude in enumerate((.25,.005,.0025,.001,.0005)):
                self.assertLess(abs(result.harmonic[h].amplitude/amplitude-1),.02)

    def test_thd_invalid_weak_clipped_and_disabled(self):
        self.assertNotEqual(lib.measurement_start(1,-1200),0)
        for amplitude,clipped,bit in ((.00005,0,1),(.25,1,2)):
            lib.measurement_init(); lib.generator_init()
            lib.generator_request(c.byref(base.Tone(1000,-1200,1))); lib.generator_render_at(base.U512(),512,0)
            self.assertEqual(lib.measurement_start(1,-1200),0)
            for index in range(512,40000,512):
                result=measured_block(index,(amplitude,),(0,),clipped)
                if result: break
            self.assertTrue(result.reason & bit)
        lib.measurement_cancel(); self.assertEqual(lib.measurement_active(),0)

    def test_calibration_transaction_phase_and_gain_invalidation(self):
        points=(CalPoint*2)(CalPoint(20,0,math.radians(170)),CalPoint(20000,6,math.radians(-170)))
        self.assertEqual(lib.calibration_begin(123,1,0,2),0)
        self.assertNotEqual(lib.calibration_commit(),0)
        self.assertNotEqual(lib.calibration_append(1,points,2),0)
        self.assertEqual(lib.calibration_append(0,points,2),0)
        self.assertEqual(lib.calibration_commit(),0)
        db,phase=c.c_float(),c.c_float()
        self.assertEqual(lib.calibration_apply(20*math.sqrt(1000),c.byref(db),c.byref(phase)),1)
        self.assertAlmostEqual(db.value,3,places=4); self.assertAlmostEqual(phase.value,math.pi,places=4)
        self.assertEqual(lib.calibration_begin(124,1,0,2),0)
        bad=(CalPoint*2)(CalPoint(20,0,0),CalPoint(10,0,0))
        self.assertNotEqual(lib.calibration_append(0,bad,2),0)
        self.assertEqual(lib.calibration_id(),123)
        lib.calibration_abort(); lib.calibration_path(2,99)
        self.assertEqual(lib.calibration_id(),0)
        self.assertNotEqual(lib.calibration_set_spl(110,98),0)
        self.assertEqual(lib.calibration_set_spl(110,99),0)
        spl=c.c_float(); self.assertTrue(lib.calibration_spl(-20,c.byref(spl)))
        self.assertEqual(spl.value,90)
        lib.calibration_path(2,100); self.assertFalse(lib.calibration_spl(-20,c.byref(spl)))

    def test_all_121_sweep_points_and_stop(self):
        self.assertEqual(lib.measurement_start(2,-1200),0)
        points=[]
        for index in range(0,48000*90,512):
            result=measured_block(index,(.25,),(.4,))
            if result:
                self.assertEqual(result.reason,0)
                self.assertEqual(result.point,len(points)); self.assertEqual(result.point_count,121)
                self.assertLess(abs(result.frequency/(20*1000**(result.point/120))-1),1e-5)
                self.assertLess(abs(result.harmonic[0].phase-.4),.002)
                points.append(result.frequency); lib.measurement_consume()
                if not lib.measurement_active(): break
        self.assertEqual(len(points),121)
        lib.generator_render_at(base.U512(),512,index+512)
        tone=base.Tone(); lib.generator_get_config(c.byref(tone)); self.assertEqual(tone.enabled,0)

    def test_uart_full_export_keeps_dma_running(self):
        isolated=base.build/'export.dll'; shutil.copyfile(base.integration_library,isolated)
        app=c.CDLL(str(isolated)); app.host_block.argtypes=[c.POINTER(c.c_uint16),c.c_uint32]
        app.host_receive.argtypes=[c.c_void_p,c.c_uint32]
        self.assertEqual(app.audio_start(),0); self.assertEqual(app.protocol_uart_start(),0)
        block=1; next_seq=0; chunks=[]; previous=None; clock=0; measurements=[]; replies=[]; frames=[]
        def command(payload):
            nonlocal next_seq
            next_seq+=1; body=struct.pack('<BBHHI',1,7,next_seq,len(payload),0)+payload
            raw=b'\xa5\x5a'+body+struct.pack('<H',lib.protocol_crc16(body,len(body)))
            app.host_receive(raw,len(raw))
        def run(ms):
            nonlocal block,previous,clock
            out=(c.c_uint8*1024)()
            for t in range(clock+1,clock+ms+1):
                app.host_tick(t)
                if t*48>=block*512:
                    samples=base.U512(*(round(2048+512*math.sin(2*math.pi*1000*((block-1)*512+i)/48000)) for i in range(512)))
                    app.host_block(samples,(block-1)%2); block+=1
                app.audio_poll(); app.protocol_uart_poll()
                n=app.host_copy_tx(out); raw=bytes(out[:n])
                if raw!=previous and n>=14:
                    self.assertEqual(int.from_bytes(raw[-2:],'little'),lib.protocol_crc16(raw[2:-2],n-4))
                    self.assertTrue(esp.host_esp_valid(raw,n),(raw[3],raw.hex()))
                    frames.append(raw.hex())
                    if raw[3]==6: chunks.append(raw)
                    if raw[3]==5: measurements.append(raw[12:-2])
                    if raw[3]==8: replies.append(raw[12:-2])
                    previous=raw
            clock+=ms
        command(b'\x04\x00\x10'); run(100)
        command(b'\x02\x01'); run(300)
        command(b'\x05'); before=app.host_feeds(); run(900)
        self.assertEqual(len(chunks),9)
        count=0; dataset=None; sample=None
        for raw in chunks:
            id_,kind,unit,n,fs,total,offset,number,cal=struct.unpack('<IBBHIHHHI',raw[12:34])
            self.assertEqual(offset,count); self.assertEqual(total,2049); self.assertEqual(fs,48000)
            self.assertEqual(n,4096); self.assertEqual(number*2+36,len(raw))
            if dataset is None: dataset=id_; sample=raw[8:12]
            self.assertEqual(id_,dataset); self.assertEqual(raw[8:12],sample)
            count+=number
        self.assertEqual(count,2049); self.assertEqual(app.dsp_is_frozen(),0)
        self.assertGreater(app.host_feeds()-before,80); self.assertEqual(app.audio_is_running(),1)
        # 同一真实UART主循环继续执行扩展查询、THD、停止、校准上传和扫频。
        command(b'\x0e'); run(100)
        self.assertTrue(any(p[:2]==b'\x01\x03' for p in measurements))
        command(struct.pack('<BBIh',3,1,1000,-1200)); run(100)
        command(struct.pack('<BBh',7,1,-1200)); run(1200)
        thd=[p for p in measurements if p[:2]==b'\x01\x01']
        self.assertTrue(thd); self.assertEqual(len(thd[0]),204)
        self.assertEqual(struct.unpack_from('<I',thd[0],8)[0],24000)
        command(b'\x02\x00'); run(100)
        command(struct.pack('<BBI',12,1,99)); run(100)
        command(struct.pack('<BIBIH',8,777,1,99,2)); run(100)
        command(struct.pack('<BH Bffffff',9,0,2,20,0,0,20000,0,0)); run(100)
        command(b'\x0a'); run(100)
        self.assertEqual(replies[-1][1],0)
        command(b'\x0e'); run(100)
        extended=[p for p in measurements if p[:2]==b'\x01\x03'][-1]
        self.assertEqual(struct.unpack_from('<I',extended,16)[0],777)
        command(struct.pack('<BBh',7,2,-1200)); run(1600)
        self.assertTrue(any(p[:2]==b'\x01\x02' for p in measurements))
        command(b'\x02\x00'); run(100)
        # 网页使用同一批真实C5帧，验证C++桥接与浏览器解码都接受。
        decoder=str(base.ROOT.parents[1]/'esp/videoesp/web/protocol.js') if os.environ.get('C5_LIVE_ESP')=='1' else str(base.ROOT/'tools/host/esp_v03_contract.cjs')
        subprocess.run(['node','-e',
            "const p=require(process.argv[1]);let s='';process.stdin.on('data',x=>s+=x);process.stdin.on('end',()=>{for(const h of JSON.parse(s))p.decode(Uint8Array.from(Buffer.from(h,'hex')));});",
            decoder],input=json.dumps(frames),text=True,check=True)

if __name__=='__main__': unittest.main(argv=['test_v03.py',*base.unused],verbosity=2)

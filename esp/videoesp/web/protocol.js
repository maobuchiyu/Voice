(function(root) {
  'use strict';
  function crc(bytes,start=0,end=bytes.length) {
    let value=0xffff;
    for(let i=start;i<end;i++) {
      value^=bytes[i]<<8;
      for(let b=0;b<8;b++) value=((value&0x8000) ? (value<<1)^0x1021 : value<<1)&0xffff;
    }
    return value;
  }
  function bins(index,n,fs) {
    const k=n/2, lo=index<128 ? index+1 : 129+Math.floor((index-128)*(k-128)/128);
    const hi=index<128 ? lo : 128+Math.floor((index-127)*(k-128)/128);
    return {lo,hi,startHz:lo*fs/n,endHz:hi*fs/n,centerHz:(lo+hi)*fs/(2*n)};
  }
  function decode(buffer) {
    const bytes=buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
    if(bytes.length<14) throw new Error('short_frame');
    const d=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength), n=d.getUint16(6,true);
    if(bytes[0]!==0xa5 || bytes[1]!==0x5a || bytes[2]!==1 || n>1010 || bytes.length!==14+n) throw new Error('invalid_frame');
    if(d.getUint16(bytes.length-2,true)!==crc(bytes,2,bytes.length-2)) throw new Error('bad_crc');
    const frame={type:bytes[3],seq:d.getUint16(4,true),sampleIndex:d.getUint32(8,true)};
    if(frame.type===1) {
      if(n!==32 && n!==40) throw new Error('invalid_status');
      Object.assign(frame,{mode:bytes[12],audioRunning:bytes[13]===1,flags:d.getUint16(14,true),sampleRate:d.getUint32(16,true),fftLength:d.getUint16(20,true),blocks:d.getUint32(24,true),lost:d.getUint32(28,true),dacErrors:d.getUint32(32,true),badFrames:d.getUint32(36,true),uptime:d.getUint32(40,true)});
      if(frame.mode>3 || bytes[13]>1 || !frame.sampleRate || frame.sampleRate>192000 || ![0,2048,4096].includes(frame.fftLength)) throw new Error('invalid_status');
      frame.generatorSupported=!!(frame.flags&0x100);
      frame.generatorActive=!!(frame.flags&0x200);
      if(frame.generatorSupported) {
        if(n<40) throw new Error('invalid_generator_status');
        frame.toneFrequency=d.getUint32(44,true); frame.toneLevel=d.getInt16(48,true)/100;
        if(frame.toneFrequency<20 || frame.toneFrequency>20000 || frame.toneLevel < -40 || frame.toneLevel > -6) throw new Error('invalid_generator_status');
      }
      frame.advancedSupported=!!(frame.flags&0x1c00);
    } else if(frame.type===2) {
      if(n!==32) throw new Error('invalid_level');
      Object.assign(frame,{rms:d.getFloat32(12,true),peak:d.getFloat32(16,true),frequency:d.getFloat32(20,true),clipped:d.getUint32(24,true),flags:d.getUint32(28,true),lost:d.getUint32(32,true),worstBlock:d.getUint32(36,true),uptime:d.getUint32(40,true)});
      if(![frame.rms,frame.peak,frame.frequency].every(Number.isFinite) || frame.rms < -200 || frame.rms >6 || frame.peak < -200 || frame.peak >6 || ((frame.flags&1) && (frame.frequency<=0 || frame.frequency>96000))) throw new Error('invalid_level');
    } else if(frame.type===3) {
      if(n!==520) throw new Error('invalid_spectrum');
      frame.sampleRate=d.getUint32(12,true); frame.fftLength=d.getUint16(16,true); frame.count=d.getUint16(18,true);
      if(!frame.sampleRate || frame.sampleRate>192000 || ![2048,4096].includes(frame.fftLength) || frame.count!==256) throw new Error('invalid_spectrum');
      frame.levels=Array.from({length:256},(_,i)=>d.getInt16(20+i*2,true)/100);
    } else if(frame.type===4) {
      if(n!==260 || d.getUint16(12,true)!==512 || d.getUint16(14,true)!==64) throw new Error('invalid_waveform');
      frame.sourceCount=512; frame.groupCount=64;
      frame.pairs=Array.from({length:64},(_,i)=>[d.getInt16(16+i*4,true),d.getInt16(18+i*4,true)]);
      if(frame.pairs.some(([lo,hi])=>lo < -2048 || hi >2047 || lo>hi)) throw new Error('invalid_waveform');
    } else if(frame.type===5) {
      if(n<2 || bytes[12]!==1) throw new Error('invalid_measurement');
      frame.kind=bytes[13];
      const u16=o=>d.getUint16(12+o,true),u32=o=>d.getUint32(12+o,true),f32=o=>d.getFloat32(12+o,true);
      if(frame.kind===3) {
        if(n!==28 || u16(2)!==0x300 || ![2048,4096].includes(u16(8)) || bytes[22]>3 || ![1,2].includes(bytes[23]) || [32,33,34,35].some(i=>bytes[i]>1)) throw new Error('invalid_extension');
        const mask=u32(4);
        Object.assign(frame,{protocolRevision:u16(2),capabilityMask:mask,fftLength:u16(8),mode:bytes[22],path:bytes[23],gainId:u32(12),calibrationId:u32(16),calibrationLoading:!!bytes[32],measurementActive:!!bytes[33],splValid:!!bytes[34],exporting:!!bytes[35],uptime:u32(24),capabilities:{analysis:!!(mask&1),full:!!(mask&2),harmonics:!!(mask&4),sweep:!!(mask&8),calibration:!!(mask&16),power:!!(mask&32)}});
      } else if(frame.kind===4) {
        if(n!==56 || bytes[14]>1 || bytes[15] || u16(18)!==8 || u16(16)>255) throw new Error('invalid_power');
        frame.weighted=[f32(8),f32(12),f32(4)]; frame.splValid=!!bytes[14];frame.spl=f32(52);
        frame.bands=[160,250,500,1000,2000,4000,8000,16000].map((frequency,i)=>({frequency,dbfs:f32(20+i*4),valid:!!(u16(16)&1<<i)}));
        if([...frame.weighted,...frame.bands.map(b=>b.dbfs)].some(v=>!Number.isFinite(v)||v < -200||v>20)||!Number.isFinite(frame.spl)||frame.spl<0||frame.spl>220) throw new Error('invalid_power_values');
      } else {
        if(n!==204 || ![1,2].includes(frame.kind) || u16(2)>63 || bytes[32]>31 || ![1,2].includes(bytes[33]) || bytes[34] || bytes[35] || !u32(8) || u16(18)!==(frame.kind===1?1:121) || u16(16)>=u16(18)) throw new Error('invalid_measurement');
        Object.assign(frame,{reason:u16(2),resultId:u32(4),samples:u32(8),calibrationId:u32(12),pointIndex:u16(16),pointCount:u16(18),validMask:bytes[32],path:bytes[33],frequency:f32(24),rawGainDb:f32(28),rawPhaseDeg:f32(32)*180/Math.PI,thdPercent:f32(36),thdDb:f32(40)});
        frame.valid=!(frame.reason&31);frame.correctedValid=frame.valid&&!!frame.calibrationId&&!(frame.reason&32);
        frame.harmonics=Array.from({length:5},(_,i)=>{const o=44+i*32;return {order:i+1,available:!!(frame.validMask&1<<i),frequency:f32(o),amplitude:f32(o+4),dbfs:f32(o+8),dbc:f32(o+12),percent:f32(o+16),phaseDeg:f32(o+20)*180/Math.PI,correctedDbfs:f32(o+24),correctedPhaseDeg:f32(o+28)*180/Math.PI};});
        frame.gainDb=frame.rawGainDb+frame.harmonics[0].correctedDbfs-frame.harmonics[0].dbfs;frame.phaseDeg=frame.harmonics[0].correctedPhaseDeg;
        if(!Number.isFinite(frame.frequency)||frame.frequency<(frame.kind===1?100:20)||frame.frequency>(frame.kind===1?4000:20000)||[frame.rawGainDb,frame.rawPhaseDeg,frame.thdPercent,frame.thdDb,...frame.harmonics.flatMap(h=>[h.frequency,h.amplitude,h.dbfs,h.dbc,h.percent,h.phaseDeg,h.correctedDbfs,h.correctedPhaseDeg])].some(v=>!Number.isFinite(v))) throw new Error('invalid_measurement_values');
      }
    } else if(frame.type===6) {
      if(n<24) throw new Error('invalid_chunk');
      Object.assign(frame,{datasetId:d.getUint32(12,true),kind:bytes[16],format:bytes[17],fftLength:d.getUint16(18,true),sampleRate:d.getUint32(20,true),total:d.getUint16(24,true),offset:d.getUint16(26,true),count:d.getUint16(28,true),calibrationId:d.getUint32(30,true)});
      if(!frame.datasetId||frame.kind!==1||frame.format!==1||!frame.sampleRate||frame.sampleRate>192000||![2048,4096].includes(frame.fftLength)||frame.total!==frame.fftLength/2+1||frame.count<1||frame.count>236||frame.offset+frame.count>frame.total||n!==22+frame.count*2) throw new Error('invalid_chunk');
      frame.levels=Array.from({length:frame.count},(_,i)=>d.getInt16(34+i*2,true)/100);
      if(frame.levels.some(v=>v < -200 || v>20)) throw new Error('invalid_chunk_values');
    } else if(frame.type===8) {
      if(n!==4 || bytes[13]>5 || bytes[14]>3) throw new Error('invalid_reply');
      Object.assign(frame,{command:bytes[12],result:bytes[13],mode:bytes[14]});
    } else throw new Error('unsupported_type');
    return frame;
  }
  class Dataset {
    constructor(id=0,now=Date.now()) {this.id=id;this.started=now;this.received=0;this.final=false;this.meta=null;this.values=null;this.seen=null;}
    add(f,now=Date.now()) {
      if(now-this.started>5000) throw new Error('dataset_timeout');
      if(!this.id)this.id=f.datasetId;
      if(f.datasetId!==this.id) throw new Error('dataset_mismatch');
      const key=[f.sampleRate,f.fftLength,f.total,f.sampleIndex,f.calibrationId].join(':');
      if(this.meta&&this.meta!==key) throw new Error('metadata_mismatch');
      if(!this.meta){this.meta=key;this.frame={...f};this.values=new Array(f.total);this.seen=new Uint8Array(f.total);}
      for(let i=0;i<f.count;i++){const k=f.offset+i;if(this.seen[k]&&this.values[k]!==f.levels[i])throw new Error('duplicate_conflict');if(!this.seen[k]){this.seen[k]=1;this.received++;}this.values[k]=f.levels[i];}

      return this.received===f.total ? {...this.frame,levels:[...this.values],complete:true,count:f.total,offset:0,final:true} : null;
    }
  }
  function calibration(value) {
    if(!value||value.schema!=='acousticlab.calibration.v1'||!['loop','mic'].includes(value.kind)||!Number.isInteger(value.id)||value.id<1||value.id>0xffffffff||typeof value.name!=='string'||new TextEncoder().encode(value.name).length>64||typeof value.source!=='string'||new TextEncoder().encode(value.source).length>160||typeof value.gainNote!=='string'||new TextEncoder().encode(value.gainNote).length>96||!Array.isArray(value.points)||value.points.length<2||value.points.length>121)throw new Error('invalid_calibration');
    let prev=0;for(const p of value.points){if(![p.frequency,p.gainDb,p.phaseDeg].every(Number.isFinite)||p.frequency<20||p.frequency>20000||p.frequency<=prev||Math.abs(p.gainDb)>80||Math.abs(p.phaseDeg)>180)throw new Error('invalid_calibration_point');prev=p.frequency;}
    if(value.kind==='mic'&&(Math.abs(value.points[0].frequency-20)>.001||Math.abs(value.points.at(-1).frequency-20000)>.001))throw new Error('mic_coverage');
    return {schema:value.schema,kind:value.kind,id:value.id,name:value.name,source:value.source,gainNote:value.gainNote,points:value.points.map(p=>({frequency:p.frequency,gainDb:p.gainDb,phaseDeg:p.phaseDeg}))};
  }
  const api={crc,decode,bins,Dataset,calibration};
  if(typeof module!=='undefined' && module.exports) module.exports=api;
  root.AcousticProtocol=api;
})(typeof globalThis!=='undefined' ? globalThis : this);

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
      if(frame.mode>1 || bytes[13]>1 || !frame.sampleRate || frame.sampleRate>192000 || ![0,2048,4096].includes(frame.fftLength)) throw new Error('invalid_status');
      frame.generatorSupported=!!(frame.flags&0x100);
      frame.generatorActive=!!(frame.flags&0x200);
      if(frame.generatorSupported) {
        if(n!==40) throw new Error('invalid_generator_status');
        frame.toneFrequency=d.getUint32(44,true); frame.toneLevel=d.getInt16(48,true)/100;
        if(frame.toneFrequency<20 || frame.toneFrequency>20000 || frame.toneLevel < -40 || frame.toneLevel > -6) throw new Error('invalid_generator_status');
      }
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
    } else if(frame.type===8) {
      if(n!==4 || bytes[13]>3 || bytes[14]>1) throw new Error('invalid_reply');
      Object.assign(frame,{command:bytes[12],result:bytes[13],mode:bytes[14]});
    } else throw new Error('unsupported_type');
    return frame;
  }
  const api={crc,decode,bins};
  if(typeof module!=='undefined' && module.exports) module.exports=api;
  root.AcousticProtocol=api;
})(typeof globalThis!=='undefined' ? globalThis : this);

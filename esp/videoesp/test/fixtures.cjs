// Explicit test fixtures, never embedded into the device web assets.
const P=require('../web/protocol.js');
function frame(type,payload,seq=1,sampleIndex=0) {
  const f=Buffer.alloc(14+payload.length);f[0]=0xa5;f[1]=0x5a;f[2]=1;f[3]=type;
  f.writeUInt16LE(seq&65535,4);f.writeUInt16LE(payload.length,6);f.writeUInt32LE(sampleIndex>>>0,8);payload.copy(f,12);f.writeUInt16LE(P.crc(f,2,f.length-2),f.length-2);return f;
}
function status(o={}) {
  const p=Buffer.alloc(o.generator?40:32);p[0]=o.mode??1;p[1]=1;p.writeUInt16LE(o.generator?0x100|(o.tone?0x200:0):0,2);
  p.writeUInt32LE(48000,4);p.writeUInt16LE(o.fft??2048,8);p.writeUInt32LE(10000,12);p.writeUInt32LE(o.lost||0,16);p.writeUInt32LE(o.dac||0,20);p.writeUInt32LE(o.bad||0,24);p.writeUInt32LE(o.uptime??5000,28);
  if(o.generator){p.writeUInt32LE(o.frequency||1000,32);p.writeInt16LE((o.level??-12)*100,36);}return frame(1,p,o.seq||1,o.sample||51200);
}
function level(o={}) {
  const p=Buffer.alloc(32);p.writeFloatLE(o.rms??-18.2,0);p.writeFloatLE(o.peak??-10.7,4);p.writeFloatLE(o.frequency??1000,8);p.writeUInt32LE(o.clipped||0,12);p.writeUInt32LE(o.valid===false?0:1,16);p.writeUInt32LE(1000,24);p.writeUInt32LE(o.uptime||5000,28);return frame(2,p,o.seq||2,o.sample||51200);
}
function wave(seq=3,sample=51200) {
  const p=Buffer.alloc(260);p.writeUInt16LE(512,0);p.writeUInt16LE(64,2);
  for(let i=0;i<64;i++){const center=Math.sin(i*Math.PI/4)*500;p.writeInt16LE(Math.round(center-95),4+i*4);p.writeInt16LE(Math.round(center+95),6+i*4);}return frame(4,p,seq,sample);
}
function spectrum(seq=4,sample=51200,n=2048) {
  const p=Buffer.alloc(520);p.writeUInt32LE(48000,0);p.writeUInt16LE(n,4);p.writeUInt16LE(256,6);
  for(let i=0;i<256;i++){const hz=P.bins(i,n,48000).centerHz;const value=-105+87*Math.exp(-Math.pow((hz-1000)/55,2))+40*Math.exp(-Math.pow((hz-2000)/100,2))+Math.sin(i*3)*2;p.writeInt16LE(Math.round(value*100),8+i*2);}return frame(3,p,seq,sample);
}
module.exports={frame,status,level,wave,spectrum};

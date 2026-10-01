const assert=require('node:assert/strict'),P=require('../web/protocol.js'),F=require('./fixtures.cjs');
let checks=0;function check(fn){fn();checks++;}
check(()=>assert.equal(P.crc(Buffer.from('123456789')),0x29b1));
for(const fn of ['status','level','wave','spectrum']) check(()=>assert.ok(P.decode(F[fn]())));
check(()=>assert.equal(P.decode(F.status({generator:true,tone:true,frequency:4321,level:-23})).toneFrequency,4321));
check(()=>assert.equal(P.decode(F.status({fft:0})).fftLength,0));
check(()=>assert.equal(P.decode(F.level({valid:false})).flags,0));
check(()=>assert.equal(P.decode(F.wave(65535,0xfffffffe)).sampleIndex,0xfffffffe));
check(()=>{const valid=F.wave();for(let i=0;i<valid.length;i++){const bad=Buffer.from(valid);bad[i]^=1;assert.throws(()=>P.decode(bad));}});
check(()=>{const f=F.status();for(let n=0;n<f.length;n++)assert.throws(()=>P.decode(f.subarray(0,n)));assert.throws(()=>P.decode(Buffer.concat([f,Buffer.alloc(1)])));});
check(()=>{const p=Buffer.alloc(260);p.writeUInt16LE(512);p.writeUInt16LE(64,2);p.writeInt16LE(20,4);p.writeInt16LE(-20,6);assert.throws(()=>P.decode(F.frame(4,p)));});
check(()=>{const p=Buffer.alloc(32);p.writeFloatLE(NaN);assert.throws(()=>P.decode(F.frame(2,p)));});
check(()=>{for(const n of [2048,4096]){let last=0;for(let i=0;i<256;i++){const b=P.bins(i,n,48000);assert.equal(b.lo,last+1);assert.ok(b.hi>=b.lo);last=b.hi;}assert.equal(last,n/2);}});
console.log('Protocol checks passed:',checks,'(including every-byte CRC corruption and all truncated lengths)');

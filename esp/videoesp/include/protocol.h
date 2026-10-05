#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace Protocol {
constexpr size_t capacity = 1024;
constexpr uint16_t generatorSupported = 0x0100;
constexpr uint16_t generatorActive = 0x0200;
constexpr uint16_t harmonicsSupported=4, sweepSupported=8, fullSupported=2, calibrationSupported=16, analysisSupported=1;
inline bool finiteRange(float v,float lo,float hi) { return isfinite(v) && v>=lo && v<=hi; }
inline uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { for (unsigned i=0; i<4; ++i) p[i] = v >> (i*8); }
inline float f32(const uint8_t* p) { uint32_t bits=u32(p); float v; memcpy(&v,&bits,4); return v; }
inline uint16_t crc(const uint8_t* p, size_t size) {
  uint16_t v=0xffff;
  while(size--) { v ^= uint16_t(*p++) << 8; for(unsigned b=0;b<8;++b) v = v&0x8000 ? (v<<1)^0x1021 : v<<1; }
  return v;
}
inline size_t command(uint8_t* out, uint16_t seq, const uint8_t* payload, uint16_t size) {
  memset(out,0,14+size); out[0]=0xa5; out[1]=0x5a; out[2]=1; out[3]=7;
  put16(out+4,seq); put16(out+6,size); memcpy(out+12,payload,size);
  put16(out+12+size,crc(out+2,10+size)); return 14+size;
}
// Reject malformed values before caching or forwarding a frame.
inline bool validPayload(const uint8_t* f) {
  const uint8_t* p=f+12; const uint16_t n=u16(f+6);
  switch(f[3]) {
    case 1:
      return (n==32 || n==40) && p[0]<=3 && p[1]<=1 && u32(p+4)>0 && u32(p+4)<=192000 &&
        (u16(p+8)==0 || u16(p+8)==2048 || u16(p+8)==4096) &&
        (!(u16(p+2)&generatorSupported) || (n==40 && u32(p+32)>=20 && u32(p+32)<=20000 &&
          int16_t(u16(p+36))>=-4000 && int16_t(u16(p+36))<=-600));
    case 2:
      return n==32 && isfinite(f32(p)) && isfinite(f32(p+4)) && isfinite(f32(p+8)) &&
        f32(p)<=6 && f32(p)>=-200 && f32(p+4)<=6 && f32(p+4)>=-200 &&
        (!(u32(p+16)&1) || (f32(p+8)>0 && f32(p+8)<=96000));
    case 3:
      return n==520 && u32(p)>0 && u32(p)<=192000 && (u16(p+4)==2048 || u16(p+4)==4096) && u16(p+6)==256;
    case 4:
      if(n!=260 || u16(p)!=512 || u16(p+2)!=64) return false;
      for(unsigned i=0;i<64;++i) {
        const int16_t lo=int16_t(u16(p+4+i*4)), hi=int16_t(u16(p+6+i*4));
        if(lo < -2048 || hi>2047 || lo>hi) return false;
      }
      return true;
    case 5: {
      if(n<2 || p[0]!=1) return false;
      if(p[1]==3) return n==28 && u16(p+2)==0x0300 &&
        (u16(p+8)==2048 || u16(p+8)==4096) && p[10]<=3 && p[11]>=1 && p[11]<=2 &&
        p[20]<=1 && p[21]<=1 && p[22]<=1 && p[23]<=1;
      if(p[1]==4) {
        if(n!=56 || p[2]>1 || p[3] || u16(p+18)!=8 || (u16(p+16)&~255)) return false;
        for(unsigned i=0;i<3;++i) if(!finiteRange(f32(p+4+4*i),-200,20)) return false;
        for(unsigned i=0;i<8;++i) if(!finiteRange(f32(p+20+4*i),-200,20)) return false;
        return finiteRange(f32(p+52),0,220);
      }
      if(n!=204 || (p[1]!=1 && p[1]!=2) || (u16(p+2)&~63) || p[20]>31 ||
         p[21]<1 || p[21]>2 || p[22] || p[23] || !u32(p+8) ||
         u16(p+18)!=(p[1]==1?1:121) || u16(p+16)>=u16(p+18) ||
         !finiteRange(f32(p+24),p[1]==1?100:20,p[1]==1?4000:20000)) return false;
      for(unsigned i=28;i<204;i+=4) if(!isfinite(f32(p+i))) return false;
      return true;
    }
    case 6: {
      if(n<24 || !u32(p) || p[4]!=1 || p[5]!=1 ||
         (u16(p+6)!=2048 && u16(p+6)!=4096) || !u32(p+8) || u32(p+8)>192000 ||
         u16(p+12)!=u16(p+6)/2+1 || !u16(p+16) || u16(p+16)>236 ||
         u16(p+14)+u16(p+16)>u16(p+12) || n!=22+2*u16(p+16)) return false;
      for(unsigned i=0;i<u16(p+16);++i) if(int16_t(u16(p+22+i*2))<-20000 || int16_t(u16(p+22+i*2))>2000) return false;
      return true;
    }
    case 8: return n==4 && p[1]<=5 && p[2]<=3;
    default: return false;
  }
}
class Parser {
 public:
  uint32_t badFrames=0;
  size_t used=0;
  uint8_t bytes[capacity];
  void reset() { used=0; }
  template<class Receiver> void push(uint8_t b, Receiver receive) {
    if(used==capacity) { ++badFrames; discard(1); }
    bytes[used++]=b;
    while(used>=2) {
      if(bytes[0]!=0xa5 || bytes[1]!=0x5a) { discard(1); continue; }
      if(used<8) return;
      const uint16_t payload=u16(bytes+6);
      if(bytes[2]!=1 || payload>capacity-14) { ++badFrames; discard(1); continue; }
      const size_t size=payload+14;
      if(used<size) return;
      if(u16(bytes+size-2)!=crc(bytes+2,size-4)) { ++badFrames; discard(1); continue; }
      if(validPayload(bytes)) receive(bytes,size); else ++badFrames;
      discard(size);
    }
  }
 private:
  void discard(size_t count) { used-=count; memmove(bytes,bytes+count,used); }
};
} // namespace Protocol

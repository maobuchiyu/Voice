#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include "protocol.h"
#include "web_assets.h"
#include "asset_http.h"
#ifndef C5_UART_RX_PIN
#define C5_UART_RX_PIN 16
#endif
#ifndef C5_UART_TX_PIN
#define C5_UART_TX_PIN 17
#endif
#ifndef ACOUSTICLAB_AP_PASSWORD
#define ACOUSTICLAB_AP_PASSWORD "12345678"
#endif

namespace {
constexpr char version[]="0.1.1";
constexpr uint32_t commandTimeout=2000, deviceTimeout=3000;
HardwareSerial c5(1);
size_t httpInfo(char*,size_t);
AssetHttp http(httpInfo);
WebSocketsServer ws(81);
Preferences preferences;
Protocol::Parser parser;
StaticJsonDocument<768> savedConfig;
char ssid[24];
bool storageReady=false, connected=false;
bool helloPending=false;
char jsonOutput[2048];
StaticJsonDocument<1536> request;
uint32_t maxLoopUs=0;
uint8_t activeClient=0;
uint32_t lastByte=0, lastFlush=0, lastStats=0, bytesReceived=0, previousBytes=0;
uint32_t rate=0, framesReceived=0, coalesced=0, lastC5Uptime=0, restarts=0;
bool haveUptime=false;
struct Cache {
  uint8_t bytes[Protocol::capacity]; size_t size=0; uint32_t at=0; bool dirty=false;
  void clear() { size=0; dirty=false; }
  void store(const uint8_t* f,size_t n) { if(dirty) ++coalesced; memcpy(bytes,f,n); size=n; at=millis(); dirty=true; }
} status, level, spectrum, waveform;
uint16_t nextSeq=0;
struct Pending {
  bool active=false, ack=false, freshStatus=false; uint16_t seq=0; uint32_t id=0, since=0;
  uint8_t client=0, command=0; int8_t mode=-1;
  uint32_t frequency=0; int16_t toneLevel=0; bool toneEnabled=false;
} pending;
bool online() { return status.size && uint32_t(millis()-status.at)<deviceTimeout; }
uint8_t mode() { return status.size ? status.bytes[12] : 0; }
bool generatorSupported() { return online() && (Protocol::u16(status.bytes+14)&Protocol::generatorSupported); }
template<size_t N> void sendJson(uint8_t client,const StaticJsonDocument<N>& doc) {
  const size_t n=serializeJson(doc,jsonOutput,sizeof(jsonOutput));
  if(n<sizeof(jsonOutput)) ws.sendTXT(client,jsonOutput,n);
}
void event(uint8_t client,const char* kind,uint32_t id,const char* message,uint16_t seq=0,int result=-1) {
  StaticJsonDocument<256> doc;
  doc["event"]=kind; doc["id"]=id; doc["message"]=message; doc["seq"]=seq;
  if(!strcmp(kind,"result")) doc["result"]=result;
  if(connected && client==activeClient) sendJson(client,doc);
}
void defaults() {
  savedConfig.clear(); savedConfig["view"]="scope"; savedConfig["input"]="mic";
  savedConfig["gainNote"]=""; savedConfig["coupling"]="dc";
  savedConfig["timebase"]=1.066667; savedConfig["vertical"]=0.25;
  savedConfig["frequency"]=1000; savedConfig["toneLevel"]=-12;
  savedConfig["spectrumMin"]=-120; savedConfig["spectrumMax"]=0;
}
bool validConfig(JsonVariantConst c) {
  const char* view=c["view"]|"", *input=c["input"]|"", *coupling=c["coupling"]|"", *note=c["gainNote"]|"";
  const double t=c["timebase"]|-1.0, v=c["vertical"]|-1.0;
  return c.is<JsonObjectConst>() && c["gainNote"].is<const char*>() && (!strcmp(view,"scope") || !strcmp(view,"spectrum")) &&
    (!strcmp(input,"mic") || !strcmp(input,"loop") || !strcmp(input,"external")) &&
    (!strcmp(coupling,"ac") || !strcmp(coupling,"dc")) && strlen(note)<=96 &&
    isfinite(t) && t>=0.1 && t<=1.1 && isfinite(v) && v>=0.05 && v<=1 &&
    c["frequency"].is<int>() && c["frequency"].as<int>()>=20 && c["frequency"].as<int>()<=20000 &&
    c["toneLevel"].is<int>() && c["toneLevel"].as<int>()>=-40 && c["toneLevel"].as<int>()<=-6 &&
    c["spectrumMin"].is<int>() && c["spectrumMax"].is<int>() &&
    c["spectrumMin"].as<int>()>=-160 && c["spectrumMin"].as<int>()<=-20 &&
    c["spectrumMax"].as<int>()>=-40 && c["spectrumMax"].as<int>()<=6 &&
    c["spectrumMax"].as<int>()-c["spectrumMin"].as<int>()>=20;
}
void copyConfig(JsonVariantConst c) {
  const char* keys[]={"view","input","gainNote","coupling","timebase","vertical","frequency","toneLevel","spectrumMin","spectrumMax"};
  savedConfig.clear(); for(const char* key:keys) savedConfig[key]=c[key];
}
void loadConfig() {
  defaults(); storageReady=preferences.begin("acousticlab",false);
  if(!storageReady) return;
  String value=preferences.getString("ui","");
  if(value.length()>768) return;
  StaticJsonDocument<1024> doc;
  if(!deserializeJson(doc,value) && validConfig(doc.as<JsonVariantConst>())) copyConfig(doc.as<JsonVariantConst>());
}
void hello(uint8_t client) {
  StaticJsonDocument<1536> doc;
  doc["event"]="hello"; doc["version"]=version; doc["ssid"]=ssid;
  doc["flash"]=ESP.getFlashChipSize(); doc["psram"]=ESP.getPsramSize();
  doc["storage"]=storageReady; doc["config"]=savedConfig.as<JsonObject>();
  doc["busy"]=pending.active; sendJson(client,doc);
}
size_t httpInfo(char* out,size_t size) {
  StaticJsonDocument<768> doc;
  doc["version"]=version; doc["ssid"]=ssid; doc["online"]=online();
  doc["flash"]=ESP.getFlashChipSize(); doc["psram"]=ESP.getPsramSize();
  doc["uptime"]=millis(); doc["rxBytes"]=bytesReceived;
  doc["frames"]=framesReceived; doc["badFrames"]=parser.badFrames;
  doc["resetReason"]=int(esp_reset_reason()); doc["maxLoopUs"]=maxLoopUs;
  doc["stackFree"]=uxTaskGetStackHighWaterMark(nullptr);
  return serializeJson(doc,out,size);
}
void statistics() {
  const uint32_t now=millis(), elapsed=now-lastStats;
  rate=elapsed ? uint32_t(uint64_t(bytesReceived-previousBytes)*1000/elapsed) : 0;
  previousBytes=bytesReceived; lastStats=now;
  if(!connected) return;
  StaticJsonDocument<768> doc;
  doc["event"]="stats"; doc["uptime"]=now; doc["online"]=online();
  doc["rxBytes"]=bytesReceived; doc["rxRate"]=rate; doc["frames"]=framesReceived;
  doc["badFrames"]=parser.badFrames; doc["coalesced"]=coalesced; doc["restarts"]=restarts;
  doc["freeHeap"]=ESP.getFreeHeap(); doc["minHeap"]=ESP.getMinFreeHeap();
  doc["statusAge"]=status.size ? int32_t(now-status.at) : -1;
  doc["resetReason"]=int(esp_reset_reason()); doc["maxLoopUs"]=maxLoopUs;
  doc["stackFree"]=uxTaskGetStackHighWaterMark(nullptr);
  doc["busy"]=pending.active; sendJson(activeClient,doc);
}
void finish(const char* message,int result) {
  event(pending.client,"result",pending.id,message,pending.seq,result);
  Serial.printf("C5 result: seq=%u code=%d %s\n",pending.seq,result,message);
  pending.active=false;
}
void tryComplete() {
  if(!pending.active || !pending.ack || !pending.freshStatus) return;
  if(pending.command==2 && mode()!=pending.mode) return;
  if(pending.command==3) {
    if(!generatorSupported()) return;
    const bool enabled=Protocol::u16(status.bytes+14)&Protocol::generatorActive;
    if(enabled!=pending.toneEnabled || Protocol::u32(status.bytes+44)!=pending.frequency ||
       int16_t(Protocol::u16(status.bytes+48))!=pending.toneLevel) return;
  }
  finish("confirmed",0);
}
void sendCommand(uint8_t client,uint32_t id,uint8_t command,int modeValue=-1,
                 uint32_t frequency=0,int16_t toneLevel=0,bool enabled=false) {
  if(pending.active) { event(client,"error",id,"busy"); return; }
  if(command!=1 && !online()) { event(client,"error",id,"device_offline"); return; }
  if(command==3 && !generatorSupported()) { event(client,"error",id,"unsupported"); return; }
  uint8_t payload[8]={command}, frame[22]; uint16_t n=1;
  if(command==2) { payload[1]=modeValue; n=2; }
  if(command==3) { payload[1]=enabled; Protocol::put32(payload+2,frequency); Protocol::put16(payload+6,toneLevel); n=8; }
  const uint16_t seq=++nextSeq; const size_t size=Protocol::command(frame,seq,payload,n);
  if(c5.availableForWrite()<int(size) || c5.write(frame,size)!=size) { event(client,"error",id,"uart_write_failed"); return; }
  pending=Pending{}; pending.active=true; pending.seq=seq; pending.id=id;
  pending.client=client; pending.since=millis(); pending.command=command; pending.mode=modeValue;
  pending.frequency=frequency; pending.toneLevel=toneLevel; pending.toneEnabled=enabled;
  Serial.printf("C5 command: seq=%u command=%u\n",seq,command);
  event(client,"sent",id,"awaiting_reply",seq);
}
void received(const uint8_t* f,size_t n) {
  ++framesReceived;
  switch(f[3]) {
    case 1: {
      const uint32_t uptime=Protocol::u32(f+40);
      if(haveUptime && int32_t(uptime-lastC5Uptime)<0) {
        ++restarts; level.clear(); waveform.clear(); spectrum.clear();
        if(pending.active) finish("device_restarted",-1);
        if(connected) event(activeClient,"device_restarted",0,"device_restarted");
      }
      haveUptime=true; lastC5Uptime=uptime; status.store(f,n);
      if(mode()==0) { level.clear(); waveform.clear(); spectrum.clear(); }
      if(pending.active) { pending.freshStatus=true; tryComplete(); }
      break;
    }
    case 2: if(online() && mode()==1) level.store(f,n); break;
    case 3:
      if(online() && mode()==1 && Protocol::u16(status.bytes+20)==Protocol::u16(f+16) &&
         Protocol::u32(status.bytes+16)==Protocol::u32(f+12)) spectrum.store(f,n);
      break;
    case 4: if(online() && mode()==1) waveform.store(f,n); break;
    case 8:
      if(!pending.active || Protocol::u16(f+4)!=pending.seq || f[12]!=pending.command) break;
      if(f[13]!=0) { finish("rejected",f[13]); break; }
      pending.ack=true; event(pending.client,"ack",pending.id,"awaiting_status",pending.seq); tryComplete(); break;
  }
}
void pollC5() {
  if(parser.used && uint32_t(millis()-lastByte)>250) { parser.reset(); ++parser.badFrames; }
  // Bounded work gives the network a turn even with continuous UART traffic.
  for(unsigned i=0;i<2048 && c5.available();++i) {
    const uint8_t byte=c5.read(); ++bytesReceived; lastByte=millis(); parser.push(byte,received);
  }
}
void flush() {
  const uint32_t now=millis(); if(uint32_t(now-lastFlush)<100) return; lastFlush=now;
  Cache* caches[]={&status,&level,&spectrum,&waveform};
  for(Cache* cache:caches) {
    if(!cache->dirty) continue;
    cache->dirty=false;
    if(connected && now-cache->at < (cache==&status ? deviceTimeout : 1000U)) ws.sendBIN(activeClient,cache->bytes,cache->size);
  }
}
void onWebSocket(uint8_t client,WStype_t type,uint8_t* payload,size_t size) {
  if(type==WStype_CONNECTED) {
    if(connected && activeClient!=client) ws.disconnect(activeClient);
    connected=true; activeClient=client; helloPending=true;
    Serial.printf("WS connected: client=%u\n",client);
    return;
  }
  if(type==WStype_DISCONNECTED) {
    Serial.printf("WS disconnected: client=%u\n",client);
    if(client==activeClient) { connected=false; helloPending=false; }
    return;
  }
  if(type!=WStype_TEXT) return;
  if(size>768) { event(client,"error",0,"invalid_request"); return; }
  request.clear();
  if(deserializeJson(request,payload,size,DeserializationOption::NestingLimit(3))) { event(client,"error",0,"invalid_request"); return; }
  const uint32_t id=request["id"]|0U; const char* action=request["action"]|"";
  if(!strcmp(action,"save")) {
    if(!validConfig(request["config"])) { event(client,"error",id,"invalid_config"); return; }
    if(!storageReady) { event(client,"error",id,"storage_failed"); return; }
    StaticJsonDocument<768> previous; previous.set(savedConfig);
    copyConfig(request["config"]); String value; serializeJson(savedConfig,value);
    if(preferences.getString("ui","")!=value && preferences.putString("ui",value)!=value.length()) {
      savedConfig.set(previous); event(client,"error",id,"storage_failed"); return;
    }
    event(client,"saved",id,"saved"); return;
  }
  if(!strcmp(action,"status")) sendCommand(client,id,1);
  else if(!strcmp(action,"rta")) sendCommand(client,id,2,1);
  else if(!strcmp(action,"idle")) sendCommand(client,id,2,0);
  else if(!strcmp(action,"tone")) {
    const int frequency=request["frequency"]|0, levelValue=request["level"]|0;
    if(!request["enabled"].is<bool>() || !request["frequency"].is<int>() || !request["level"].is<int>() ||
       frequency<20 || frequency>20000 || levelValue < -40 || levelValue > -6) { event(client,"error",id,"invalid_request"); return; }
    sendCommand(client,id,3,-1,frequency,levelValue*100,request["enabled"]);
  } else event(client,"error",id,"unknown_command");
}
} // namespace
void setup() {
  Serial.begin(115200); c5.setRxBufferSize(4096); c5.begin(115200,SERIAL_8N1,C5_UART_RX_PIN,C5_UART_TX_PIN);
  loadConfig(); WiFi.mode(WIFI_AP); WiFi.setSleep(false);
  snprintf(ssid,sizeof(ssid),"AcousticLab-%04X",unsigned(ESP.getEfuseMac()&0xffff));
  if(!WiFi.softAP(ssid,ACOUSTICLAB_AP_PASSWORD,1,false,1)) { Serial.println("SoftAP start failed"); return; }
  http.begin(); ws.onEvent(onWebSocket); ws.begin(); ws.enableHeartbeat(15000,3000,2);
  esp_task_wdt_init(8,true); if(esp_task_wdt_status(nullptr)!=ESP_OK) esp_task_wdt_add(nullptr);
  Serial.printf("AcousticLab %s AP: %s password: %s URL: http://%s/\n",version,ssid,ACOUSTICLAB_AP_PASSWORD,WiFi.softAPIP().toString().c_str());
  Serial.printf("C5 UART RX=%d TX=%d 115200 8N1; Flash=%u PSRAM=%u\n",C5_UART_RX_PIN,C5_UART_TX_PIN,ESP.getFlashChipSize(),ESP.getPsramSize());
  Serial.printf("Reset reason=%d; loop stack free=%u bytes\n",int(esp_reset_reason()),unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
void loop() {
  const uint32_t began=micros();
  pollC5(); http.loop(); ws.loop(); pollC5();
  if(helloPending && connected) {
    helloPending=false; hello(activeClient);
    Cache* caches[]={&status,&level,&spectrum,&waveform};
    for(Cache* cache:caches) if(cache->size) cache->dirty=true;
    statistics();
  }
  flush();
  if(pending.active && uint32_t(millis()-pending.since)>=commandTimeout) finish(pending.ack ? "status_timeout" : "reply_timeout",-1);
  if(uint32_t(millis()-lastStats)>=1000) statistics();
  esp_task_wdt_reset();
  maxLoopUs=max(maxLoopUs,uint32_t(micros()-began));
  delay(1);
}

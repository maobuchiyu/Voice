#pragma once
#include <WiFi.h>
#include <lwip/sockets.h>
#include <errno.h>
#include "web_assets.h"

// Four flash assets and a small diagnostic endpoint. Each loop does bounded
// work; a slow HTTP reader never makes the UART wait for a whole asset write.
class AssetHttp {
 public:
  using Info = size_t (*)(char*, size_t);
  explicit AssetHttp(Info info) : server(80), info(info) {}
  void begin() { server.begin(); }
  void loop() {
    if(!client.connected()) {
      client.stop(); client=server.available();
      used=headerSize=headerSent=bodySent=bodySize=0; responding=false;
      lastProgress=millis();
      if(!client) return;
    }
    if(uint32_t(millis()-lastProgress)>5000) { client.stop(); return; }
    if(!responding) {
      for(unsigned count=0; count<512 && client.available(); ++count) {
        if(used==sizeof(request)-1) { reply(431,"text/plain","Request too large\n",false); break; }
        request[used++]=char(client.read()); request[used]=0; lastProgress=millis();
        if(used>=4 && !memcmp(request+used-4,"\r\n\r\n",4)) { route(); break; }
      }
    }
    if(!responding) return;
    // One nonblocking socket write per turn. PROGMEM is memory mapped on S3.
    if(headerSent<headerSize) {
      headerSent+=write(reinterpret_cast<const uint8_t*>(header)+headerSent,headerSize-headerSent);
    } else if(bodySent<bodySize) {
      bodySent+=write(body+bodySent,min(size_t(1024),bodySize-bodySent));
    } else client.stop();
  }
 private:
  WiFiServer server;
  WiFiClient client;
  Info info;
  char request[1024], header[384], json[768];
  size_t used=0, headerSize=0, headerSent=0, bodySize=0, bodySent=0;
  const uint8_t* body=nullptr;
  uint32_t lastProgress=0;
  bool responding=false;
  size_t write(const uint8_t* data,size_t size) {
    const int sent=::send(client.fd(),data,size,MSG_DONTWAIT);
    if(sent>0) { lastProgress=millis(); return sent; }
    if(sent==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) client.stop();
    return 0;
  }
  void reply(int code,const char* mime,const char* text,bool gzip,size_t size=0) {
    body=reinterpret_cast<const uint8_t*>(text); bodySize=size ? size : strlen(text);
    const char* reason=code==200?"OK":code==404?"Not Found":code==405?"Method Not Allowed":"Request Header Fields Too Large";
    headerSize=snprintf(header,sizeof(header),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n%s\r\n",code,reason,mime,unsigned(bodySize),gzip?"Content-Encoding: gzip\r\n":"");
    responding=true;
  }
  void route() {
    char method[8], path[160];
    if(sscanf(request,"%7s %159s",method,path)!=2 || strcmp(method,"GET")) {
      reply(405,"text/plain","GET required\n",false); return;
    }
    char* query=strchr(path,'?'); if(query) *query=0;
    if(!strcmp(path,"/api/info")) {
      const size_t size=info(json,sizeof(json)); reply(200,"application/json",json,false,size); return;
    }
    for(const WebAsset& asset:webAssets) {
      if(!strcmp(path,asset.path) || (!strcmp(path,"/") && !strcmp(asset.path,"/index.html"))) {
        reply(200,asset.mime,reinterpret_cast<const char*>(asset.data),true,asset.size); return;
      }
    }
    reply(404,"text/plain","Not found\n",false);
  }
};

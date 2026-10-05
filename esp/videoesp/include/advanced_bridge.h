// Included inside the bridge namespace; no tasks or unbounded transfer queues.
const char* calibrationKey(uint8_t kind) { return kind==1 ? "cal_loop" : "cal_mic"; }
bool validCalibration(JsonVariantConst profile) {
  const char* kind=profile["kind"]|"";
  if(strcmp(profile["schema"]|"","acousticlab.calibration.v1") ||
    (strcmp(kind,"loop") && strcmp(kind,"mic")) || !profile["id"].is<uint32_t>() || !profile["id"].as<uint32_t>()) return false;
  const char* keys[]={"name","source","gainNote"}; const unsigned limits[]={64,160,96};
  for(unsigned i=0;i<3;++i) if(!profile[keys[i]].is<const char*>() || strlen(profile[keys[i]].as<const char*>())>limits[i]) return false;
  JsonArrayConst points=profile["points"].as<JsonArrayConst>();
  if(points.size()<2 || points.size()>121) return false;
  float previous=0;
  for(JsonVariantConst point:points) {
    if(!point["frequency"].is<float>() || !point["gainDb"].is<float>() || !point["phaseDeg"].is<float>()) return false;
    const float hz=point["frequency"], gain=point["gainDb"], phase=point["phaseDeg"];
    if(!Protocol::finiteRange(hz,20,20000) || hz<=previous || !Protocol::finiteRange(gain,-80,80) || !Protocol::finiteRange(phase,-180,180)) return false;
    previous=hz;
  }
  if(!strcmp(kind,"mic") && (fabsf(points[0]["frequency"].as<float>()-20)>.001f ||
      fabsf(points[points.size()-1]["frequency"].as<float>()-20000)>.001f)) return false;
  return true;
}
void addCalibrationList(JsonArray list) {
  if(!storageReady) return;
  for(uint8_t kind=1;kind<=2;++kind) {
    String value=preferences.getString(calibrationKey(kind),"");
    if(value.length()>12288 || value.isEmpty()) continue;
    DynamicJsonDocument doc(16384);
    if(deserializeJson(doc,value) || !validCalibration(doc.as<JsonVariantConst>())) continue;
    JsonObject entry=list.createNestedObject(); entry["kind"]=doc["kind"]; entry["id"]=doc["id"];
    entry["name"]=doc["name"]; entry["gainNote"]=doc["gainNote"]; entry["source"]=doc["source"];
    entry["count"]=doc["points"].size();
  }
}
bool sendAdvanced(uint8_t client,uint32_t id,const uint8_t* payload,uint16_t n) {
  if(pending.active) { event(client,"error",id,"busy"); return false; }
  if(!online()) { event(client,"error",id,"device_offline"); return false; }
  uint8_t frame[142]; const uint16_t seq=++nextSeq;
  const size_t size=Protocol::command(frame,seq,payload,n);
  if(c5.availableForWrite()<int(size) || c5.write(frame,size)!=size) { event(client,"error",id,"uart_write_failed"); return false; }
  pending=Pending{}; pending.active=true; pending.id=id; pending.seq=seq; pending.client=client;
  pending.since=millis(); pending.command=payload[0];
  if(payload[0]==14) extended.clear();
  event(client,"sent",id,"awaiting_reply",seq); return true;
}
void putFloat(uint8_t* out,float value) { uint32_t bits; memcpy(&bits,&value,4); Protocol::put32(out,bits); }
uint32_t gainIdentity(const char* note,uint8_t kind) {
  uint32_t hash=2166136261U^kind; while(*note) { hash^=uint8_t(*note++); hash*=16777619U; } return hash ? hash : 1;
}
bool calibrationMatches() {
  if(!extended.size || !Protocol::u32(extended.bytes+28)) return true;
  const uint8_t path=extended.bytes[23]; const char* kind=path==1 ? "loop" : "mic";
  return !strcmp(savedConfig["input"]|"",kind) && gainIdentity(savedConfig["gainNote"]|"",path)==Protocol::u32(extended.bytes+24);
}
void continueAdvanced() {
  uint8_t payload[124]={}; uint16_t n=1;
  if(clearCalibration.active) {
    const auto target=clearCalibration;
    if(pending.command==11) { payload[0]=12; payload[1]=target.kind; Protocol::put32(payload+2,target.gain); n=6; }
    else payload[0]=14;
    if(!sendAdvanced(target.client,target.id,payload,n)) clearCalibration.active=false;
    return;
  }
  if(thdStart.active) {
    payload[0]=7; payload[1]=1; Protocol::put16(payload+2,thdStart.level); n=4;
    const auto start=thdStart; thdStart.active=false;
    if(sendAdvanced(start.client,start.id,payload,n)) pending.mode=2;
    return;
  }
  if(pending.command==12) {
    payload[0]=8; Protocol::put32(payload+1,calTransfer.id); payload[5]=calTransfer.kind;
    Protocol::put32(payload+6,calTransfer.gainId); Protocol::put16(payload+10,calTransfer.count); n=12;
  } else if(calTransfer.offset<calTransfer.count) {
    const uint8_t count=min(uint16_t(10),uint16_t(calTransfer.count-calTransfer.offset));
    payload[0]=9; Protocol::put16(payload+1,calTransfer.offset); payload[3]=count;
    for(unsigned i=0;i<count;++i) for(unsigned j=0;j<3;++j) putFloat(payload+4+i*12+j*4,calTransfer.points[calTransfer.offset+i][j]);
    calTransfer.offset+=count; n=4+12*count;
  } else payload[0]=pending.command==10 ? 14 : 10;
  if(!sendAdvanced(calTransfer.client,calTransfer.requestId,payload,n)) calTransfer.active=false;
}
void receiveChunk(const uint8_t* f,size_t n) {
  if(!pending.active || !pending.ack || pending.command!=5 || !connected || pending.client!=activeClient) return;
  const uint8_t* p=f+12; const uint32_t dataset=Protocol::u32(p);
  const uint16_t total=Protocol::u16(p+12), offset=Protocol::u16(p+14), count=Protocol::u16(p+16);
  if(Protocol::u32(p+8)!=Protocol::u32(status.bytes+16) || Protocol::u16(p+6)!=Protocol::u16(status.bytes+20) ||
    (pending.datasetTotal && (pending.datasetId!=dataset || pending.datasetTotal!=total || pending.datasetSample!=Protocol::u32(f+8) || pending.calId!=Protocol::u32(p+18)))) { finish("dataset_mismatch",-1); return; }
  pending.datasetId=dataset; pending.datasetTotal=total; pending.datasetSample=Protocol::u32(f+8); pending.calId=Protocol::u32(p+18);
  for(unsigned i=offset;i<offset+count;++i) {
    uint8_t& b=pending.datasetSeen[i/8]; const uint8_t bit=1<<(i%8); const int16_t value=int16_t(Protocol::u16(p+22+(i-offset)*2));
    if((b&bit) && datasetValues[i]!=value) { finish("dataset_conflict",-1); return; }
    if(!(b&bit)) { b|=bit; ++pending.datasetCount; datasetValues[i]=value; }
  }
  ws.sendBIN(activeClient,const_cast<uint8_t*>(f),n); pending.datasetComplete=pending.datasetCount==total; tryComplete();
}
bool advancedRequest(uint8_t client,uint32_t id,const char* action) {
  if(!strcmp(action,"cal_save") || !strcmp(action,"cal_get")) {
    if(!storageReady) { event(client,"error",id,"storage_failed"); return true; }
    if(!strcmp(action,"cal_save")) {
      JsonVariantConst profile=request["profile"];
      if(!validCalibration(profile)) { event(client,"error",id,"invalid_calibration"); return true; }
      const uint8_t kind=!strcmp(profile["kind"],"loop")?1:2;
      String value; serializeJson(profile,value);
      if(value.length()>12288 || (preferences.getString(calibrationKey(kind),"")!=value && preferences.putString(calibrationKey(kind),value)!=value.length())) {
        event(client,"error",id,"storage_failed"); return true;
      }
    }
    const char* kind=!strcmp(action,"cal_save") ? request["profile"]["kind"]|"" : request["kind"]|"";
    if(strcmp(kind,"loop") && strcmp(kind,"mic")) { event(client,"error",id,"invalid_request"); return true; }
    DynamicJsonDocument doc(18000); String stored=preferences.getString(calibrationKey(!strcmp(kind,"loop")?1:2),"");
    if(stored.length()>12288 || deserializeJson(doc,stored) || !validCalibration(doc.as<JsonVariantConst>())) { event(client,"error",id,"invalid_calibration"); return true; }
    JsonVariant profile=doc.as<JsonVariant>();
    // Keep the root profile intact by serializing it into a separate response.
    DynamicJsonDocument response(19000); response["event"]="cal_profile"; response["id"]=id; response["profile"]=profile;
    String output; serializeJson(response,output); ws.sendTXT(client,output); return true;
  }
  uint8_t payload[16]={}; uint16_t n=0, flag=0;
  if(!strcmp(action,"extended")) { payload[0]=14; n=1; }
  else if(!strcmp(action,"analysis")) {
    flag=Protocol::analysisSupported; const int fft=request["fft"]|0;
    if(fft!=2048 && fft!=4096) { event(client,"error",id,"invalid_request"); return true; }
    payload[0]=4; Protocol::put16(payload+1,fft); n=3;
  } else if(!strcmp(action,"thd") || !strcmp(action,"sweep")) {
    flag=!strcmp(action,"thd") ? Protocol::harmonicsSupported : Protocol::sweepSupported;
    if(!request["enabled"].is<bool>() || !request["level"].is<int>()) { event(client,"error",id,"invalid_request"); return true; }
    const int level=request["level"]; if(level<-40 || level>-6) { event(client,"error",id,"invalid_request"); return true; }
    if(!request["enabled"].as<bool>()) { payload[0]=2; payload[1]=0; n=2; }
    else if(!strcmp(action,"thd")) {
      const int hz=request["frequency"]|0;
      if(hz<100 || hz>4000 || !request["frequency"].is<int>()) { event(client,"error",id,"invalid_request"); return true; }
      payload[0]=3; payload[1]=1; Protocol::put32(payload+2,hz); Protocol::put16(payload+6,level*100); n=8;
    } else { payload[0]=7; payload[1]=2; Protocol::put16(payload+2,level*100); n=4; }
  } else if(!strcmp(action,"full")) { flag=Protocol::fullSupported; payload[0]=5; n=1; }
  else if(!strcmp(action,"cal_clear") || !strcmp(action,"cal_apply")) {
    flag=Protocol::calibrationSupported;
    if(!capability(flag)) { event(client,"error",id,"unsupported"); return true; }
    if(mode()!=0 || pending.active) { event(client,"error",id,mode()!=0 ? "requires_idle":"busy"); return true; }
    const char* kind=request["kind"]|"";
    if(strcmp(kind,"loop") && strcmp(kind,"mic")) { event(client,"error",id,"invalid_request"); return true; }
    const uint8_t path=!strcmp(kind,"loop")?1:2;
    payload[0]=12; payload[1]=path; n=6;
    uint32_t gain=gainIdentity(savedConfig["gainNote"]|"",path);
    if(!strcmp(action,"cal_clear")) {
      gain=extended.size ? Protocol::u32(extended.bytes+24)^0x80000000U : gain^0x80000000U;
      clearCalibration.active=true; clearCalibration.client=client; clearCalibration.id=id; clearCalibration.kind=path; clearCalibration.gain=gain;
      payload[0]=11; n=1;
    } else {
      DynamicJsonDocument doc(16384); String value=preferences.getString(calibrationKey(path),"");
      if(value.length()>12288 || deserializeJson(doc,value) || !validCalibration(doc.as<JsonVariantConst>())) { event(client,"error",id,"invalid_calibration"); return true; }
      if(strcmp(savedConfig["input"]|"",kind) || strcmp(savedConfig["gainNote"]|"",doc["gainNote"]|"")) { event(client,"error",id,"calibration_path_mismatch"); return true; }
      calTransfer=CalTransfer{}; calTransfer.active=true; calTransfer.client=client; calTransfer.requestId=id;
      calTransfer.kind=path; calTransfer.gainId=gain; calTransfer.id=doc["id"]; calTransfer.count=doc["points"].size();
      for(unsigned i=0;i<calTransfer.count;++i) {
        calTransfer.points[i][0]=doc["points"][i]["frequency"]; calTransfer.points[i][1]=doc["points"][i]["gainDb"];
        calTransfer.points[i][2]=doc["points"][i]["phaseDeg"].as<float>()*0.01745329252f;
      }
    }
    Protocol::put32(payload+2,gain);
    if(!sendAdvanced(client,id,payload,n)) { calTransfer.active=false; clearCalibration.active=false; }
    return true;
  } else return false;
  if((payload[0]==3 || payload[0]==5 || payload[0]==7) && !calibrationMatches()) { event(client,"error",id,"calibration_path_mismatch"); return true; }
  if(flag && !capability(flag)) { event(client,"error",id,"unsupported"); return true; }
  if(sendAdvanced(client,id,payload,n)) {
    if(payload[0]==4) pending.fft=Protocol::u16(payload+1);
    if(payload[0]==2) pending.mode=0;
    if(payload[0]==7) pending.mode=3;
    if(payload[0]==3) {
      pending.toneEnabled=true; pending.frequency=Protocol::u32(payload+2); pending.toneLevel=int16_t(Protocol::u16(payload+6));
      thdStart.active=true; thdStart.client=client; thdStart.id=id; thdStart.level=pending.toneLevel;
    }
    if(payload[0]==5) { StaticJsonDocument<128> doc; doc["event"]="dataset_begin"; doc["id"]=id; sendJson(client,doc); }
  }
  return true;
}

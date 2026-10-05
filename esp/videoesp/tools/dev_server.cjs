// Local test device: test fixtures are intentionally separate from firmware.
// node tools/dev_server.cjs; HTTP 8080 + WebSocket 81, bound only to 127.0.0.1.
const http=require('node:http'),fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const F=require('../test/fixtures.cjs'),root=path.resolve(__dirname,'../web');
let mode=0,fft=2048,generator=true,tone=false,frequency=1000,level=-12,seq=1,sample=0,started=Date.now(),silent=false,busy=false,config={view:'scope',input:'mic',gainNote:'',coupling:'dc',timebase:1.066667,vertical:0.25,frequency:1000,toneLevel:-12,spectrumMin:-120,spectrumMax:0};
const clients=new Set();
let advanced=false,reason=0,calibrationId=0,profiles={},sweepPoint=0,resultId=0,datasetId=0,partial=false;
function gainIdentity(note,kind){let hash=(2166136261^kind)>>>0;for(const b of Buffer.from(note)){hash=Math.imul(hash^b,16777619)>>>0;}return hash||1;}
function packet(socket,body,opcode=1){const payload=Buffer.isBuffer(body)?body:Buffer.from(JSON.stringify(body));let head;if(payload.length<126){head=Buffer.from([0x80|opcode,payload.length]);}else{head=Buffer.alloc(4);head[0]=0x80|opcode;head[1]=126;head.writeUInt16BE(payload.length,2);}socket.write(Buffer.concat([head,payload]));}
function broadcast(body,opcode=1){for(const socket of clients)packet(socket,body,opcode);}
function sendStatus(){broadcast(F.status({mode,fft,generator,tone,frequency,level,advanced,uptime:(Date.now()-started)>>>0,seq:seq++,sample}),2);}
function command(socket,r){
  if(r.action==='cal_save'){profiles[r.profile.kind]=r.profile;packet(socket,{event:'cal_profile',id:r.id,profile:r.profile});return;}
  if(r.action==='cal_get'){packet(socket,{event:'cal_profile',id:r.id,profile:profiles[r.kind]});return;}
  if(r.action==='save'){config=r.config;packet(socket,{event:'saved',id:r.id,message:'saved'});return;}
  if(busy){packet(socket,{event:'error',id:r.id,message:'busy'});return;}
  const current=seq++;
  packet(socket,{event:'sent',id:r.id,seq:current,message:'awaiting_reply'});
  if(silent){busy=true;setTimeout(()=>{busy=false;packet(socket,{event:'result',id:r.id,result:-1,message:'reply_timeout',seq:current});},2000);return;}
  busy=true;
  setTimeout(()=>{
    if(r.action==='analysis')fft=r.fft;
    if(r.action==='thd'){mode=r.enabled?2:0;tone=r.enabled;frequency=r.frequency;level=r.level;}
    if(r.action==='sweep'){mode=r.enabled?3:0;sweepPoint=0;level=r.level;}
    if(r.action==='cal_apply')calibrationId=profiles[r.kind]?.id||0;
    if(r.action==='cal_clear')calibrationId=0;
    if(advanced)packet(socket,F.extended({mode,fft,calibrationId,path:config.input==='mic'?2:1,gainId:gainIdentity(config.gainNote,config.input==='mic'?2:1),seq:seq++,sample}),2);
    if(r.action==='full'){packet(socket,{event:'dataset_begin',id:r.id});const id=++datasetId;for(let offset=0;offset<fft/2+1;offset+=236){if(partial&&offset>236)break;packet(socket,F.chunk({id,fft,offset,calibrationId,seq:seq++,sample}),2);}}
    if(r.action==='rta')mode=1;if(r.action==='idle')mode=0;
    if(r.action==='tone'){tone=r.enabled;frequency=r.frequency;level=r.level;}
    packet(socket,{event:'ack',id:r.id,seq:current,message:'awaiting_status'});sendStatus();
    packet(socket,{event:'result',id:r.id,seq:current,result:0,message:'confirmed'});busy=false;
  },150);
}
function connection(socket){
  clients.add(socket);packet(socket,{event:'hello',version:'0.3.0-test-fixture',ssid:'LOCAL TEST FIXTURE',flash:16777216,psram:8388608,storage:true,config,busy,calibrations:Object.values(profiles).map(p=>({...p,count:p.points.length}))});sendStatus();
  let input=Buffer.alloc(0);
  socket.on('data',data=>{input=Buffer.concat([input,data]);while(input.length>=2){let n=input[1]&127,offset=2;const masked=!!(input[1]&128),opcode=input[0]&15;if(n===126){if(input.length<4)return;n=input.readUInt16BE(2);offset=4;}if(n===127||n>12288){socket.destroy();return;}if(masked)offset+=4;if(input.length<offset+n)return;let payload=Buffer.from(input.subarray(offset,offset+n));if(masked){const mask=input.subarray(offset-4,offset);for(let i=0;i<n;i++)payload[i]^=mask[i%4];}input=input.subarray(offset+n);if(opcode===8){socket.end();return;}if(opcode===9){packet(socket,payload,10);continue;}if(opcode===1){try{command(socket,JSON.parse(payload));}catch{}}}});
  socket.on('close',()=>clients.delete(socket));socket.on('error',()=>clients.delete(socket));
}
const ws=http.createServer();ws.on('upgrade',(req,socket)=>{const key=req.headers['sec-websocket-key'];if(!key){socket.destroy();return;}const accept=crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\n\r\n');connection(socket);});ws.listen(81,'127.0.0.1');
http.createServer((req,res)=>{
  if(req.url.startsWith('/__test')){const q=new URL(req.url,'http://localhost').searchParams;
    if(q.has('advanced'))advanced=q.get('advanced')==='1';if(q.has('reason'))reason=Number(q.get('reason'));if(q.has('partial'))partial=q.get('partial')==='1';
    if(q.has('silent'))silent=q.get('silent')==='1';if(q.has('fft'))fft=Number(q.get('fft'));if(q.has('generator'))generator=q.get('generator')==='1';
    if(q.has('reboot')){started=Date.now();sample=0;mode=0;tone=false;calibrationId=0;sendStatus();}
    if(q.has('disconnect'))for(const s of clients)s.destroy();
    if(q.has('bad')){const bad=F.wave();bad[20]^=1;broadcast(bad,2);}
    sendStatus();res.end(JSON.stringify({mode,fft,generator,tone,config}));return;
  }
  const name=req.url.split('?')[0]==='/'?'index.html':req.url.split('?')[0].slice(1);
  if(!['index.html','styles.css','protocol.js','advanced.js','app.js'].includes(name)){res.writeHead(404);res.end();return;}
  res.setHeader('Cache-Control','no-store');res.setHeader('Content-Type',name.endsWith('.html')?'text/html; charset=utf-8':name.endsWith('.css')?'text/css':'text/javascript');res.end(fs.readFileSync(path.join(root,name)));
}).listen(8080,'127.0.0.1',()=>console.log('Local TEST FIXTURE http://127.0.0.1:8080/ ; websocket port 81'));
setInterval(()=>{if(mode===1){sample=(sample+4800)>>>0;broadcast(F.level({seq:seq++,sample,valid:!!fft,frequency}),2);broadcast(F.wave(seq++,sample),2);if(fft)broadcast(F.spectrum(seq++,sample,fft),2);if(advanced)broadcast(F.power(seq++,sample),2);}},100);
setInterval(()=>{if(advanced&&mode===2){sample+=24000;broadcast(F.measurement({kind:1,frequency,reason,calibrationId,resultId:++resultId,seq:seq++,sample}),2);}if(advanced&&mode===3){sample+=9600;broadcast(F.measurement({kind:2,frequency:sweepPoint===120?20000:20*Math.pow(1000,sweepPoint/120),point:sweepPoint,reason,calibrationId,resultId:++resultId,seq:seq++,sample}),2);sweepPoint++;if(sweepPoint===121){mode=0;sendStatus();}}},30);
setInterval(()=>{sendStatus();broadcast({event:'stats',online:true,uptime:Date.now()-started,rxBytes:100000,rxRate:mode?8540:46,frames:seq,badFrames:0,coalesced:0,restarts:0,freeHeap:240000,minHeap:238000,statusAge:0,busy});},1000);

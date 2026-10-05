(() => {
  'use strict';
  const $=id=>document.getElementById(id), P=AcousticProtocol;
  const canvas=$('chart'), ctx=canvas.getContext('2d');
  const defaults={view:'scope',input:'mic',gainNote:'',coupling:'dc',timebase:1.066667,vertical:0.25,frequency:1000,toneLevel:-12,spectrumMin:-120,spectrumMax:0};
  let config={...defaults}, saved={...defaults}, loaded=false, storage=false, socket, reconnectTimer, retry=1000;
  let info={}, stats={}, status=null, statusAt=0, espAt=0, connected=false, busy=false, pending=null, requestId=0, needsStatus=false;
  let paused=false, single=false, shown={}, latest={}, hold=-Infinity, browserBadFrames=0, dirty=true, plotGeometry=null;
  const counts={2:0,3:0,4:0}, rates={2:0,3:0,4:0}; let rateAt=performance.now();
  const inputNames={mic:'MAX4466 麦克风',loop:'DAC → ADC 回环',external:'外部模拟输入'};
  const format=(n,d=1)=>Number.isFinite(n) ? n.toLocaleString('zh-CN',{maximumFractionDigits:d,minimumFractionDigits:d}) : '—';
  const modeName=s=>({0:'IDLE',1:'RTA',2:'THD',3:'SWEEP'})[s?.mode]||'—';
  const advanced=AcousticAdvanced.create({context:()=>({status,config,saved,paused,online:!!online(),locked:!connected||busy||!!pending,storage,info}),request,download,notice});
  const online=()=>connected && status && performance.now()-statusAt<3000 && performance.now()-espAt<3500;
  const fresh=f=>f && performance.now()-f.at<1500;
  function notice(text,tone='') { $('notice').textContent=text; $('notice').className='notice '+tone; }
  function metric(id,value,unit) { $(id).replaceChildren(document.createTextNode(value)); const em=document.createElement('em'); em.textContent=unit; $(id).append(em); }
  function same(a,b) { return JSON.stringify(a)===JSON.stringify(b); }
  function syncConfig() {
    $('scopeTab').setAttribute('aria-selected',String(config.view==='scope'));
    $('spectrumTab').setAttribute('aria-selected',String(config.view==='spectrum'));
    for(const [id,key] of Object.entries({input:'input',gainNote:'gainNote',coupling:'coupling',timebase:'timebase',vertical:'vertical',toneFrequency:'frequency',toneLevel:'toneLevel',spectrumMin:'spectrumMin',spectrumMax:'spectrumMax'})) $(id).value=config[key];
    $('scopeSettings').hidden=config.view!=='scope'; $('spectrumSettings').hidden=config.view!=='spectrum';
    $('inputLabel').textContent=inputNames[config.input];
    $('chartTitle').textContent=config.view==='scope' ? '输入波形 · CH1' : '实时频谱';
    $('chartDescription').textContent=config.view==='scope' ? '64 组 min/max · 最近一个采样块' : '256 点频谱预览 · C5 计算的峰值包络';
    $('previewNote').textContent=config.view==='scope' ? '显示的是采样块包络快照。暂停与单次预览只控制页面显示，C5 继续采样。' : '主频读数来自 C5 完整 FFT。导出内容为 256 点预览，不包含完整 FFT 或谐波测量。';
    $('saveHint').textContent=same(config,saved) ? '设置已与设备一致；开机不自动开始分析。' : '有未保存的修改。点击保存后保留在 ESP。';
    $('cursor').hidden=true; dirty=true; update();
  }
  function update() {
    const alive=online(), running=alive && status.mode>0, selected=config.view==='scope' ? shown.wave : shown.spectrum;
    $('inputLabel').textContent=inputNames[selected?.input || config.input]+(selected && selected.input!==config.input ? ' · 快照接法' : '');
    $('connectionDot').className='dot '+(alive ? 'online' : connected ? '' : 'offline');
    $('connectionLabel').textContent=alive ? 'ESP · C5 已连接' : connected ? 'ESP 已连 · C5 离线' : 'ESP 断开 · 重连中';
    $('deviceState').textContent=alive ? (running ? modeName(status)+' · 分析中' : 'IDLE · 就绪') : 'C5 离线';
    $('deviceDetail').textContent=alive ? (status.audioRunning ? '采样与 DAC DMA 已启动' : '音频外设未启动') : (status ? '保留最后状态，等待新数据' : '请检查 C5 供电与串口接线');
    $('modePill').textContent=alive ? modeName(status) : '离线'; $('modePill').className='mode-pill'+(running?' running':'');
    metric('sampleRate',status ? format(status.sampleRate,0) : '—','Hz');
    $('fftLength').textContent=status ? (status.fftLength ? format(status.fftLength,0)+' 点' : '未启用') : '—';
    $('fftDetail').textContent=status?.fftLength ? 'Hann 窗 · C5 完整 FFT' : '当前 C5 尚未提供 FFT';
    const lock=!connected || busy || !!pending;
    $('startButton').disabled=lock || !alive || running || !advanced.canMeasure(); $('stopButton').disabled=lock || !alive || !running;
    $('queryButton').disabled=lock; $('saveButton').disabled=lock || !storage;
    $('pauseButton').disabled=!selected && !running;
    $('pauseButton').textContent=paused || single ? '恢复实时显示' : '暂停显示';
    $('singleButton').disabled=!running || (config.view==='spectrum' && !status.fftLength); $('autosetButton').disabled=!selected;
    $('csvButton').disabled=$('jsonButton').disabled=!(shown.wave || shown.spectrum || shown.level);
    $('toneOnButton').disabled=lock || !alive || !status?.generatorSupported;
    $('toneOffButton').disabled=lock || !alive || !status?.generatorSupported || !status.generatorActive;
    $('generatorState').textContent=status?.generatorSupported ? (status.generatorActive ? '输出中' : '已关闭') : 'C5 尚未支持';
    $('generatorHint').textContent=status?.generatorSupported ? '已生效：'+format(status.toneFrequency,0)+' Hz / '+format(status.toneLevel,1)+' dBFS · '+(status.generatorActive?'正弦输出中':'输出已关闭') : '当前 C5 未实现 DDS 命令。填写的参数可保存，升级 C5 后才能开启实际输出。';
    let state=single ? '等待单次预览' : paused ? '显示已暂停' : !alive ? '断线 · 保留快照' : !running ? 'IDLE · 保留快照' : selected && fresh(selected) ? '实时更新' : '等待新数据';
    $('displayState').textContent=state;
    $('readingsState').textContent=shown.level ? state : '等待电平数据';
    $('frameRate').textContent=format(rates[config.view==='scope'?4:3],1)+' Hz';
    $('emptyState').hidden=!!selected;
    if(!selected) {
      $('emptyTitle').textContent=config.view==='spectrum' && status && !status.fftLength ? 'C5 尚未提供频谱' : config.view==='scope' ? '等待输入波形' : '等待频谱数据';
      $('emptyHint').textContent=config.view==='spectrum' && status && !status.fftLength ? '当前波形与电平可以使用。C5 实现 FFT 后，这里会显示真实频谱。' : single ? '等待下一份图表数据。' : running ? '设备正在分析，等待有效数据帧。' : '连接 C5 后点击“开始分析”。';
    }
    const l=shown.level;
    metric('rms',l?format(l.rms):'—','dBFS'); metric('peak',l?format(l.peak):'—','dBFS'); metric('clipped',l?format(l.clipped,0):'—','个');
    metric('frequency',l && l.flags&1 && status?.fftLength ? format(l.frequency,1) : '—','Hz');
    $('frequencyHint').textContent=status?.fftLength ? (l?.flags&1 ? '来自 C5 完整 FFT' : '主频无有效结果') : 'FFT 未实现 · 数据不足';
    $('peakHold').textContent='保持 '+format(shown.hold)+' dBFS';
    $('rmsHint').textContent=l ? '采样序号 '+l.sampleIndex : '等待有效电平帧';
    $('levelMeter').style.width=l ? Math.max(0,Math.min(100,(l.rms+80)/80*100))+'%' : '0%';
    const fs=selected?.sampleRate || status?.sampleRate;
    $('windowLabel').textContent=selected && fs ? '窗口 '+format((selected.sourceCount || selected.fftLength)/fs*1000,2)+' ms' : '窗口 — ms';
    $('frameLabel').textContent=selected ? '采样序号 '+selected.sampleIndex+' · 帧 '+selected.seq : '采样序号 —';
    $('rates').textContent=[rates[4],rates[3],rates[2]].map(v=>format(v,1)).join(' / ')+' Hz';
    $('throughput').textContent=format((stats.rxRate||0)/1000,2)+' KB/s';
    $('uartLoad').textContent=format((stats.rxRate||0)/11520*100,1)+'% / 115200 8N1';
    $('badFrames').textContent=(status?.badFrames??'—')+' / '+(stats.badFrames??'—')+(browserBadFrames?' · 页面 '+browserBadFrames:'');
    $('errors').textContent=(status?.lost??'—')+' / '+(status?.dacErrors??'—'); $('blocks').textContent=format(status?.blocks,0);
    $('worstBlock').textContent=latest.level ? format(latest.level.worstBlock/1000,1)+' ms · 粗略时基' : '—';
    $('uptimes').textContent=duration(status?.uptime)+' / '+duration(stats.uptime);
    $('heap').textContent=stats.freeHeap ? format(stats.freeHeap/1024,1)+' / '+format(stats.minHeap/1024,1)+' KiB' : '—';
    $('coalesced').textContent=(stats.coalesced??'—')+' / '+(stats.restarts??'—'); $('frames').textContent=format(stats.frames,0);
    $('rxBytes').textContent=format(stats.rxBytes,0)+' B';
    $('resetReason').textContent=({1:'上电',3:'软件复位',4:'异常崩溃',5:'中断看门狗',6:'任务看门狗',7:'其他看门狗',9:'供电欠压'})[stats.resetReason] || (stats.resetReason===undefined?'—':String(stats.resetReason));
    $('maxLoop').textContent=Number.isFinite(stats.maxLoopUs)?format(stats.maxLoopUs/1000,2)+' ms':'—';
    $('stackFree').textContent=Number.isFinite(stats.stackFree)?format(stats.stackFree,0)+' B':'—';
    advanced.update(); dirty=true;
  }
  function duration(ms) { if(!Number.isFinite(ms)) return '—'; const s=Math.floor(ms/1000); return Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m '+s%60+'s'; }
  function capture() { shown={...latest,hold}; update(); }
  function receiveFrame(buffer) {
    let f; try { f=P.decode(buffer); } catch { browserBadFrames++; return; }
    f.at=performance.now(); f.receivedAt=new Date().toISOString();
    f.input=config.input; f.gainNote=config.gainNote;
    if(f.type===1) {
      const reboot=status && ((f.uptime-status.uptime)|0)<0;
      if(reboot) { latest={}; shown={}; hold=-Infinity; paused=false; single=false; advanced.reset(); notice('C5 已重新启动，旧预览已清除。',''); }
      if(status && status.mode!==f.mode) {
        latest={}; hold=-Infinity;
        if(f.mode===1 && !paused) shown={};
      }
      status=f; statusAt=f.at;
      if(!f.fftLength) { delete latest.spectrum; delete shown.spectrum; }
      if(!f.mode) { single=false; latest={}; }
      advanced.status(f); update(); return;
    }
    if(f.type===5 || f.type===6) { advanced.frame(f); return; }
    if(f.type===8) return; // Command correlation is performed by the ESP.
    if(!status || !status.mode) return;
    const key={2:'level',3:'spectrum',4:'wave'}[f.type];
    if(f.type===3 && (f.fftLength!==status.fftLength || f.sampleRate!==status.sampleRate)) return;
    f.sampleRate=f.sampleRate || status.sampleRate;
    advanced.frame(f);
    counts[f.type]++;
    latest[key]=f;
    if(f.type===2) hold=Math.max(hold,f.peak);
    if(single) {
      if(key===(config.view==='scope'?'wave':'spectrum')) { single=false; paused=true; capture(); notice('单次预览已捕获，显示保持；设备继续采样。','success'); }
    } else if(!paused) capture();
  }
  const messages={requires_idle:'请先停止分析，等待 IDLE。',invalid_calibration:'校准 JSON 或频点无效。',calibration_path_mismatch:'请先保存与校准表匹配的输入路径和增益备注。',dataset_mismatch:'完整数据集元信息不一致，已取消。',dataset_conflict:'完整数据集重复块冲突，已取消。',reply_timeout:'未收到 C5 回复。检查 GPIO17 → PA3、共地与 C5 接收端。',status_timeout:'C5 已回复，但未收到目标状态。请查询设备状态。',busy:'上一条设备命令尚未完成。',device_offline:'C5 已离线，请检查供电与串口。',unsupported:'当前 C5 不支持此功能。',invalid_config:'设置超出允许范围，未保存。',invalid_request:'参数无效，请检查输入。',storage_failed:'ESP 配置保存失败。',uart_write_failed:'串口发送失败。',device_restarted:'命令期间 C5 重新启动，请重新查询状态。'};
  function jsonMessage(data) {
    let m; try { m=JSON.parse(data); } catch { return; }
    espAt=performance.now(); advanced.json(m);
    if(m.event==='hello') {
      info=m; storage=!!m.storage; busy=!!m.busy;
      $('version').textContent='v'+m.version; $('ssid').textContent=m.ssid || 'AcousticLab';
      if(!loaded) { config={...defaults,...m.config}; saved={...config}; loaded=true; syncConfig(); }
      needsStatus=true;
    } else if(m.event==='stats') { stats=m; busy=!!m.busy; }
    else if(m.event==='device_restarted') { advanced.reset(); latest={}; shown={}; hold=-Infinity; single=false; paused=false; }
    else if(pending && m.id===pending.id) {
      if(m.event==='sent') { pending.seq=m.seq; notice('命令已发送，等待 C5 回复…'); }
      if(m.event==='ack') notice('C5 已接收命令，等待状态确认…');
      if(m.event==='result' || m.event==='error' || m.event==='saved' || m.event==='cal_profile') {
        const action=pending.action, savedSnapshot=pending.configSnapshot; clearTimeout(pending.timer); pending=null; busy=false;
        if(m.event==='cal_profile') notice('校准表已读取/保存。','success');
        else if(m.event==='saved') { saved={...savedSnapshot}; syncConfig(); notice('工作台设置已保存到 ESP。','success'); }
        else if(m.event==='result' && m.result===0) notice(action==='status' ? '状态查询成功。' : action==='rta' ? 'C5 已进入 RTA，等待实时数据。' : action==='idle' ? 'C5 已进入 IDLE，停止上传分析预览。' : ['analysis','thd','sweep','full','cal_apply','cal_clear'].includes(action) ? 'C5 已确认测量请求。' : '测试音设置已由 C5 确认。','success');
        else notice(m.message==='rejected' ? ['','C5 拒绝：命令长度错误。','C5 拒绝：参数错误。','C5 尚未支持该命令。','C5 正忙，请先停止测量或等待导出完成。','C5 尚无可用结果，请等待 FFT。'][m.result] || 'C5 拒绝了命令。' : messages[m.message] || '请求未完成，请查询状态。','error');
      }
    }
    if(needsStatus && !busy && !pending && connected) { needsStatus=false; request('status',{},true); }
    update();
  }
  function request(action,fields={},automatic=false) {
    if(!connected || socket.readyState!==WebSocket.OPEN || pending || busy) return false;
    const id=++requestId;
    pending={id,action,configSnapshot:action==='save'?{...config}:null,timer:setTimeout(()=>{ if(pending?.id!==id) return; pending=null; notice('请求超时。请查询状态确认设备当前运行情况。','error'); update(); },action==='cal_apply'?90000:action==='full'?7000:3500)};
    advanced.started(action,id);
    socket.send(JSON.stringify({action,id,...fields}));
    if(!automatic) notice(action==='save'?'正在保存工作台设置…':'等待设备确认…'); update(); return true;
  }
  function connect() {
    clearTimeout(reconnectTimer);
    const address='ws://'+location.hostname+':81/ws';
    socket=new WebSocket(address); socket.binaryType='arraybuffer';
    socket.onopen=()=>{ connected=true; retry=1000; espAt=performance.now(); update(); };
    socket.onmessage=e=>{ if(typeof e.data==='string') jsonMessage(e.data); else receiveFrame(e.data); };
    socket.onerror=()=>socket.close();
    socket.onclose=()=>{
      connected=false; busy=false;
      if(pending) { clearTimeout(pending.timer); pending=null; notice('连接已断开，命令结果未知。重连后会查询实际状态。','error'); }
      else notice('ESP 连接已断开，正在重连。最后显示的快照已保留。');
      advanced.disconnected(); single=false; update(); reconnectTimer=setTimeout(connect,retry); retry=Math.min(8000,retry*2);
    };
  }
  function waveValues(wave) {
    // ADC values stay unchanged in exports.
    const offset=config.coupling==='ac' ? wave.pairs.reduce((s,[a,b])=>s+(a+b)/2,0)/wave.groupCount : 0;
    return wave.pairs.map(([a,b])=>[(a-offset)/2048,(b-offset)/2048]);
  }
  function draw() {
    if(!dirty) return; dirty=false;
    const rect=canvas.getBoundingClientRect(), dpr=Math.min(2,devicePixelRatio||1), w=rect.width,h=rect.height;
    if(!w || !h) return;
    if(canvas.width!==Math.round(w*dpr) || canvas.height!==Math.round(h*dpr)) { canvas.width=Math.round(w*dpr); canvas.height=Math.round(h*dpr); }
    ctx.setTransform(dpr,0,0,dpr,0,0); ctx.clearRect(0,0,w,h);
    const left=46,right=w-14,top=16,bottom=h-33,pw=right-left,ph=bottom-top;
    const scope=config.view==='scope', f=scope?shown.wave:shown.spectrum;
    const fs=f?.sampleRate || status?.sampleRate || 48000;
    const durationMs=scope ? Math.min(config.timebase*10,(f?.sourceCount||512)/fs*1000) : 0;
    const min=scope?-4*config.vertical:config.spectrumMin, max=scope?4*config.vertical:config.spectrumMax;
    const freqMax=Math.min(20000,fs/2), freqMin=20;
    const x=value=>left+pw*(scope ? value/durationMs : Math.log(value/freqMin)/Math.log(freqMax/freqMin));
    const y=value=>bottom-(value-min)/(max-min)*ph;
    plotGeometry={left,right,top,bottom,pw,ph,w,h,x,y,fs,durationMs,freqMin,freqMax,scope,frame:f};
    ctx.font='10px ui-monospace,Consolas,monospace'; ctx.lineWidth=1; ctx.strokeStyle='#2a392f'; ctx.fillStyle='#8ca393';
    for(let i=0;i<=8;i++) { const py=top+i*ph/8; ctx.beginPath(); ctx.moveTo(left,py+.5); ctx.lineTo(right,py+.5); ctx.stroke(); if(i%2===0) { ctx.textAlign='right'; ctx.fillText(format(max-(max-min)*i/8,scope?2:0),left-7,py+3); } }
    const ticks=scope ? Array.from({length:11},(_,i)=>i*durationMs/10) : [20,50,100,200,500,1000,2000,5000,10000,20000].filter(v=>v<=freqMax);
    let previous=-100;
    for(const value of ticks) {
      const px=x(value); ctx.beginPath();ctx.moveTo(px+.5,top);ctx.lineTo(px+.5,bottom);ctx.stroke();
      if(px-previous< (w<430?44:39) && value!==ticks[ticks.length-1]) continue;
      if(value===ticks[ticks.length-1] && px-previous<35) continue;
      ctx.textAlign=value===ticks[0]?'left':value===ticks[ticks.length-1]?'right':'center';
      ctx.fillText(scope?format(value,1):value>=1000?value/1000+'k':String(value),px,bottom+17); previous=px;
    }
    ctx.textAlign='left';ctx.fillText(scope?'FS':'dBFS',left,top-4);ctx.textAlign='right';ctx.fillText(scope?'ms':'Hz',right,h-3);
    if(!f) return;
    ctx.save();ctx.beginPath();ctx.rect(left,top,pw,ph);ctx.clip();
    if(scope) {
      const pairs=waveValues(f); ctx.strokeStyle='#b8d88a'; ctx.fillStyle='#b8d88a18';ctx.lineWidth=1.5;
      const step=f.sourceCount/f.groupCount/fs*1000;
      const visible=pairs.map((pair,i)=>({pair,i,time:(i+.5)*step})).filter(p=>p.time<=durationMs);
      ctx.beginPath();visible.forEach((p,i)=>{ if(i===0)ctx.moveTo(x(p.time),y(p.pair[1])); else ctx.lineTo(x(p.time),y(p.pair[1])); });
      [...visible].reverse().forEach(p=>ctx.lineTo(x(p.time),y(p.pair[0])));ctx.closePath();ctx.fill();
      ctx.beginPath();visible.forEach(p=>{ const px=x(p.time),lo=y(p.pair[0]),hi=y(p.pair[1]);ctx.moveTo(px,hi);ctx.lineTo(px,Math.max(hi+1,lo)); });ctx.stroke();
    } else {
      const points=f.levels.map((v,i)=>({v,hz:P.bins(i,f.fftLength,f.sampleRate).centerHz})).filter(p=>p.hz>=freqMin && p.hz<=freqMax);
      ctx.strokeStyle='#e9c781';ctx.lineWidth=1.5;ctx.beginPath();points.forEach((p,i)=>i?ctx.lineTo(x(p.hz),y(p.v)):ctx.moveTo(x(p.hz),y(p.v)));ctx.stroke();
      if(points.length) { ctx.lineTo(x(points[points.length-1].hz),bottom);ctx.lineTo(x(points[0].hz),bottom);ctx.closePath();const g=ctx.createLinearGradient(0,top,0,bottom);g.addColorStop(0,'#e9c78130');g.addColorStop(1,'#e9c78100');ctx.fillStyle=g;ctx.fill(); }
      if(shown.level?.flags&1) { const hz=shown.level.frequency;if(hz>=freqMin && hz<=freqMax) { ctx.setLineDash([3,4]);ctx.strokeStyle='#74c7b5';ctx.beginPath();ctx.moveTo(x(hz),top);ctx.lineTo(x(hz),bottom);ctx.stroke(); } }
    }
    if(!scope) advanced.markers(ctx,plotGeometry);
    ctx.restore();
  }
  function autoset() {
    if(config.view==='scope' && shown.wave) {
      const peak=Math.max(...waveValues(shown.wave).flat().map(Math.abs));
      config.vertical=[0.05,0.1,0.25,0.5,1].find(v=>v*3.5>=peak)||1; config.timebase=1.066667;
    } else if(shown.spectrum) {
      const max=Math.max(...shown.spectrum.levels), min=Math.min(...shown.spectrum.levels);
      config.spectrumMin=[-80,-100,-120,-140,-160].find(v=>v<=min-5)||-160;
      config.spectrumMax=max<-25 ? -20 : max>0 ? 6 : 0;
    }
    syncConfig(); notice('显示范围已按当前快照调整。','success');
  }
  function cursor(e) {
    const g=plotGeometry; if(!g?.frame) return;
    const rect=canvas.getBoundingClientRect(), px=e.clientX-rect.left;
    if(px<g.left || px>g.right) { $('cursor').hidden=true;return; }
    let text;
    if(g.scope) {
      const time=(px-g.left)/g.pw*g.durationMs, index=Math.min(63,Math.floor(time*g.fs/1000/(g.frame.sourceCount/g.frame.groupCount)));
      const [lo,hi]=waveValues(g.frame)[index];text=format(time,2)+' ms\n'+format(lo,3)+' … '+format(hi,3)+' FS';
    } else {
      const hz=g.freqMin*Math.pow(g.freqMax/g.freqMin,(px-g.left)/g.pw);
      let nearest=0,distance=Infinity;g.frame.levels.forEach((_,i)=>{const d=Math.abs(Math.log(P.bins(i,g.frame.fftLength,g.fs).centerHz/hz));if(d<distance){distance=d;nearest=i;}});
      const b=P.bins(nearest,g.frame.fftLength,g.fs);text=format(b.startHz,1)+'–'+format(b.endHz,1)+' Hz\n'+format(g.frame.levels[nearest],2)+' dBFS';
    }
    const el=$('cursor');el.textContent=text;el.hidden=false;el.style.left=Math.min(g.w-155,Math.max(8,px+12))+'px';el.style.top='25px';
  }
  function snapshot() {
    return {schema:'acousticlab.preview.v1',source:'c5_uart_crc16',exportedAt:new Date().toISOString(),firmware:info.version||'unknown',device:info.ssid||'unknown',connection:online()?'online':'offline',display:paused?'paused':status?.mode===1?'live':'idle_snapshot',config:{...config},calibration:advanced.calibrationInfo(),status,stats,frames:{level:shown.level||null,waveform:shown.wave||null,spectrum:shown.spectrum||null},peakHold:Number.isFinite(shown.hold)?shown.hold:null,measurements:advanced.snapshot(),note:'Preview snapshots; frames retain individual sequence/sample positions. Waveform is min/max envelope, spectrum is 256 peak bins. Not PCM or full FFT.'};
  }
  function download(text,mime,ext) {
    const url=URL.createObjectURL(new Blob([ext==='csv'?'\ufeff':'',text],{type:mime}));const link=document.createElement('a');link.href=url;link.download='AcousticLab-'+new Date().toISOString().replace(/[:.]/g,'-')+'.'+ext;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
  }
  function exportCsv() {
    const header=['kind','index','x_start','x_end','x_unit','value_min','value_max','value_unit','frame_seq','sample_index','received_at','sample_rate','fft_length','input','gain_note','source'];
    const rows=[header], add=(kind,i,x0,x1,xunit,lo,hi,unit,f)=>rows.push([kind,i,x0,x1,xunit,lo,hi,unit,f.seq,f.sampleIndex,f.receivedAt,f.sampleRate,f.fftLength||'',f.input,f.gainNote,'c5_uart_crc16']);
    const w=shown.wave,s=shown.spectrum,l=shown.level;
    if(w) w.pairs.forEach(([a,b],i)=>{const step=w.sourceCount/w.groupCount/w.sampleRate*1000;add('waveform',i,i*step,(i+1)*step,'ms',a/2048,b/2048,'FS',w);});
    if(s) s.levels.forEach((v,i)=>{const b=P.bins(i,s.fftLength,s.sampleRate);add('spectrum',i,b.startHz,b.endHz,'Hz',v,v,'dBFS',s);});
    if(l) { add('rms',0,'','','',l.rms,l.rms,'dBFS',l);add('peak',0,'','','',l.peak,l.peak,'dBFS',l);add('clipped',0,'','','',l.clipped,l.clipped,'samples',l);if(l.flags&1) add('main_frequency',0,'','','',l.frequency,l.frequency,'Hz',l); }
    const quote=value=>'"'+String(value??'').replace(/"/g,'""')+'"'; download(rows.map(row=>row.map(quote).join(',')).join('\r\n'),'text/csv;charset=utf-8','csv');
    notice('已导出当前快照的原始预览值；显示去均值与缩放参数可在 JSON 中保留。','success');
  }
  $('scopeTab').onclick=()=>{config.view='scope';single=false;syncConfig();};$('spectrumTab').onclick=()=>{config.view='spectrum';single=false;syncConfig();};
  $('startButton').onclick=()=>request('rta');$('stopButton').onclick=()=>request('idle');$('queryButton').onclick=()=>request('status');
  $('pauseButton').onclick=()=>{paused=!paused;single=false;advanced.pause(paused);if(!paused)capture();else update();};
  $('singleButton').onclick=()=>{single=true;paused=true;update();notice('等待下一份'+(config.view==='scope'?'波形':'频谱')+'预览；设备继续采样。');};
  $('autosetButton').onclick=autoset;$('resetPeakButton').onclick=()=>{hold=latest.level?.peak??-Infinity;shown.hold=hold;update();};
  $('saveButton').onclick=()=>{if(!validateInputs()) return; request('save',{config});};
  function validateInputs() {
    const f=$('toneFrequency').valueAsNumber,l=$('toneLevel').valueAsNumber;
    if(!Number.isInteger(f)||f<20||f>20000||!Number.isInteger(l)||l < -40||l > -6) {notice('测试音参数范围：20–20000 Hz，峰值 −40 至 −6 dBFS，使用整数。','error');return false;}
    config.frequency=f;config.toneLevel=l;return true;
  }
  $('toneOnButton').onclick=()=>{if(validateInputs())request('tone',{enabled:true,frequency:config.frequency,level:config.toneLevel});};
  $('toneOffButton').onclick=()=>request('tone',{enabled:false,frequency:status.toneFrequency,level:Math.round(status.toneLevel)});
  for(const [id,key] of Object.entries({input:'input',gainNote:'gainNote',coupling:'coupling',timebase:'timebase',vertical:'vertical',spectrumMin:'spectrumMin',spectrumMax:'spectrumMax'})) $(id).addEventListener('change',()=>{config[key]=['input','gainNote','coupling'].includes(key)?$(id).value:Number($(id).value);syncConfig();});
  for(const id of ['toneFrequency','toneLevel']) $(id).addEventListener('change',()=>{if(validateInputs())syncConfig();});
  $('jsonButton').onclick=()=>{download(JSON.stringify(snapshot(),null,2),'application/json;charset=utf-8','json');notice('已导出当前快照与配置。','success');};$('csvButton').onclick=exportCsv;
  $('helpButton').onclick=()=>$('helpDialog').showModal();$('closeHelp').onclick=()=>$('helpDialog').close();
  $('helpDialog').addEventListener('click',e=>{if(e.target===$('helpDialog')) {const r=$('helpDialog').getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)$('helpDialog').close();}});
  canvas.addEventListener('pointermove',cursor);canvas.addEventListener('pointerleave',()=>$('cursor').hidden=true);
  new ResizeObserver(()=>dirty=true).observe(canvas);
  function animate(){draw();requestAnimationFrame(animate);}requestAnimationFrame(animate);
  setInterval(()=>{const now=performance.now(),elapsed=now-rateAt;for(const type of [2,3,4]){rates[type]=counts[type]*1000/elapsed;counts[type]=0;}rateAt=now;update();},1000);
  syncConfig();connect();
  window.addEventListener('pagehide',()=>{clearTimeout(reconnectTimer);if(socket)socket.close(1000,'page closed');});
})();

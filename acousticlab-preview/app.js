(() => {
  const $ = (id) => document.getElementById(id);
  const spectrumCanvas = $('spectrumCanvas');
  const waveformCanvas = $('waveformCanvas');
  const scopeCanvas = $('scopeCanvas');
  const state = { view: 'scope', mode: 'rta', paused: false, scopePaused: false, single: false, toneOn: false, fft: 2048, tick: 0, timebase: 1, verticalScale: .2, triggerLevel: 0, triggerSlope: 'rising', triggerMode: 'auto', coupling: 'ac', toastTimer: 0 };
  let plotData = [];
  let scopeData = [];
  let scopeFrequency = 1000;
  let harmonicFrame = null;

  function makeHarmonics() {
    if (state.mode !== 'loop') return null;
    const frequency = state.toneOn ? Number($('toneFrequency').value) || 1000 : 1000;
    const fundamentalDbfs = state.toneOn ? Number($('toneLevel').value) : -14;
    const components = [0, -37, -54, -62, -69].map((dbc, index) => ({
      order: index + 1, frequencyHz: frequency * (index + 1),
      levelDbfs: fundamentalDbfs + dbc, dbc, amplitudeRatio: 10 ** (dbc / 20),
      inBand: frequency * (index + 1) <= 20000
    }));
    const valid = frequency >= 100 && frequency <= 4000;
    const ratio = Math.sqrt(components.slice(1).reduce((sum, item) => sum + item.amplitudeRatio ** 2, 0));
    return { demo: true, valid, fundamentalHz: frequency, components,
      thdPercent: valid ? ratio * 100 : null, thdDb: valid ? 20 * Math.log10(ratio) : null };
  }

  function renderHarmonics() {
    const frame = harmonicFrame;
    $('harmonicsCsvButton').disabled = !frame?.valid;
    $('harmonicsDemoButton').classList.toggle('is-hidden', state.mode === 'loop');
    $('harmonicsThd').textContent = frame?.valid ? `${frame.thdPercent.toFixed(3)} %` : '—';
    $('harmonicsThdDb').textContent = frame?.valid ? `${frame.thdDb.toFixed(2)} dB · H2～H5 / H1` : 'H2～H5 的幅度平方和开方 / H1';
    $('harmonicsFundamental').textContent = frame ? `${frame.fundamentalHz.toLocaleString('en-US')} Hz` : '—';
    $('harmonicsStatus').textContent = !frame ? '当前模式不适用' : !frame.valid ? '基波超出 THD 分析范围' : state.paused ? '模拟帧已保持' : '稳定正弦 · 模拟回环';
    $('harmonicsNote').textContent = !frame
      ? 'THD 适用于稳定正弦；实时麦克风、扫频和衰减视图不输出 THD。'
      : !frame.valid ? 'THD 支持基波 100 Hz～4 kHz；超过 20 kHz 的谐波不在分析带宽内。'
      : '模拟数据，非硬件实测。幅度比以 H1 为 100%；THD 不含噪声。开启测试音可调整基波频率和电平。';
    $('harmonicsRows').innerHTML = !frame ? '<tr><td colspan="5">选择电气回环，查看稳定正弦的谐波示例。</td></tr>'
      : frame.components.map(item => `<tr><th scope="row">H${item.order}${item.order === 1 ? ' · 基波' : ''}</th><td>${item.frequencyHz.toLocaleString('en-US')}</td><td>${item.inBand ? item.levelDbfs.toFixed(1) : '超出带宽'}</td><td>${item.inBand ? item.dbc.toFixed(1) : '—'}</td><td><span class="harmonics-ratio">${item.inBand ? (item.amplitudeRatio * 100).toFixed(item.order === 1 ? 1 : 3) + ' %' : '—'}<span class="harmonics-meter" aria-hidden="true"><i style="width:${item.inBand ? item.amplitudeRatio * 100 : 0}%"></i></span></span></td></tr>`).join('');
    if (frame) {
      $('chartRateValue').textContent = `${frame.fundamentalHz} Hz`;
      $('spectrumDescription').textContent = 'DAC → ADC · 模拟正弦回环 · H1～H5 峰值包络';
      setMetricValue('freqValue', frame.fundamentalHz.toLocaleString('en-US'), 'Hz');
      const power = frame.components.filter(item => item.inBand).reduce((sum, item) => sum + 10 ** (item.levelDbfs / 10), 0);
      setMetricValue('rmsValue', (10 * Math.log10(power / 2)).toFixed(1), 'dBFS');
      setMetricLabel('peakLabel', '基波电平');
      setMetricValue('peakValue', frame.components[0].levelDbfs.toFixed(1), 'dBFS');
      $('peakFoot').textContent = '当前模拟帧 · H1';
    }
  }

  const modeProfiles = {
    rta: {
      title: '实时频谱', tag: '实时预览', description: '完整 FFT 分析 · 页面显示 256 点峰值包络', metric: '幅度', unit: 'dBFS',
      axis: 'frequency', min: -120, max: 0, yTicks: [0, -20, -40, -60, -80, -100, -120],
      xTicks: [[20, '20 Hz'], [50, '50 Hz'], [100, '100 Hz'], [500, '500 Hz'], [1000, '1 kHz'], [5000, '5 kHz'], [10000, '10 kHz'], [20000, '20 kHz']],
      baseline: -92, noise: 2.0, color: '#55c8dc', fill: 'rgba(75,198,222,.2)',
      peaks: [[180, 14, .045], [410, 12, .035], [1002, 67, .026], [2030, 23, .035], [4100, 17, .05], [8900, 12, .07]],
      annotation: { title: '主峰', x: 1002, valueText: '1.00 kHz', level: -24.8, unit: 'dBFS' },
      readings: { rms: ['RMS 电平', '−18.2', 'dBFS', '较上一窗口', '+0.4 dB'], peak: ['Peak 峰值', '−10.7', 'dBFS', '峰值保持', '2.4 s'], freq: ['主频', '1,002', 'Hz', '完整 FFT 插值'], clip: ['削顶样本', '0', '个', '输入范围正常'], input: 'MAX4466 麦克风' },
      wave: '麦克风采样波形 · 约 10.67 ms'
    },
    loop: {
      title: '电气回环响应', tag: '回环预览', description: 'DAC → 1 kΩ → ADC · 模拟 1 kHz 正弦回环', metric: '幅度', unit: 'dBFS',
      axis: 'frequency', min: -120, max: 0, yTicks: [0, -20, -40, -60, -80, -100, -120],
      xTicks: [[20, '20 Hz'], [50, '50 Hz'], [100, '100 Hz'], [500, '500 Hz'], [1000, '1 kHz'], [5000, '5 kHz'], [10000, '10 kHz'], [20000, '20 kHz']],
      baseline: -105, noise: .7, color: '#ae9bff', fill: 'rgba(164,143,255,.17)',
      peaks: [[1000, 91, .013], [2000, 54, .018], [3000, 33, .021], [5000, 19, .028]],
      annotation: { title: '回环基波', x: 1000, valueText: '1.00 kHz', level: -14, unit: 'dBFS' },
      readings: { rms: ['RMS 电平', '−17.0', 'dBFS', '回环输入', '稳定'], peak: ['Peak 峰值', '−14.0', 'dBFS', '峰值保持', '2.4 s'], freq: ['主频', '1,000', 'Hz', '回环基波'], clip: ['削顶样本', '0', '个', '回环正常'], input: 'DAC → ADC 回环' },
      wave: '回环正弦波 · 约 10.67 ms'
    },
    sweep: {
      title: '逐点扫频响应', tag: '扫频预览', description: '121 个对数频点 · 显示模拟相对幅频响应', metric: '相对幅度', unit: 'dB',
      axis: 'frequency', min: -12, max: 6, yTicks: [6, 3, 0, -3, -6, -9, -12],
      xTicks: [[20, '20 Hz'], [50, '50 Hz'], [100, '100 Hz'], [500, '500 Hz'], [1000, '1 kHz'], [5000, '5 kHz'], [10000, '10 kHz'], [20000, '20 kHz']],
      color: '#efbd72', fill: 'rgba(239,189,114,.16)',
      annotation: { title: '峰值响应', x: 2100, valueText: '2.10 kHz', level: 2.3, unit: 'dB' },
      readings: { rms: ['扫频频点', '121', '点', '扫描范围', '20 Hz–20 kHz'], peak: ['峰峰变化', '4.8', 'dB', '模拟响应', '相对幅度'], freq: ['峰值频点', '2,100', 'Hz', '扫频预览'], clip: ['扫频进度', '100', '%', '预览已完成'], input: '扫频响应（模拟）' },
      wave: '扫频输出片段 · 约 10.67 ms'
    },
    decay: {
      title: '衰减曲线', tag: '衰减预览', description: '拍手事件 · 反向积分能量衰减（EDC）', metric: '衰减电平', unit: 'dB',
      axis: 'time', min: -60, max: 0, duration: 8, yTicks: [0, -10, -20, -30, -40, -50, -60],
      xTicks: [[0, '0 s'], [1.6, '1.6 s'], [3.2, '3.2 s'], [4.8, '4.8 s'], [6.4, '6.4 s'], [8, '8 s']],
      color: '#68d8af', fill: 'rgba(104,216,175,.16)',
      annotation: { title: 'T20 外推', x: 2.8, valueText: '0.84 s', level: -45, unit: 'EDC' },
      readings: { rms: ['背景 RMS', '−48.3', 'dBFS', '事件前背景', '模拟值'], peak: ['事件峰值', '−8.3', 'dBFS', '检测状态', '已触发'], freq: ['T20 估算', '0.84', 's', '衰减时间预览'], clip: ['记录时长', '8.0', 's', '曲线已完成'], input: '拍手衰减（模拟）' },
      wave: '衰减事件尾段 · 局部波形预览'
    }
  };

  function fitCanvas(canvas) {
    const rect = canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) return null;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const width = Math.round(rect.width * dpr), height = Math.round(rect.height * dpr);
    if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx, width: rect.width, height: rect.height };
  }

  function xFraction(value, profile) {
    if (profile.axis === 'time') return value / profile.duration;
    return Math.log10(value / 20) / 3;
  }

  function yFraction(value, profile) {
    return (profile.max - value) / (profile.max - profile.min);
  }

  function makePlotData() {
    const profile = modeProfiles[state.mode], result = [];
    harmonicFrame = makeHarmonics();
    for (let i = 0; i < 256; i++) {
      const progress = i / 255;
      if (state.mode === 'decay') {
        const seconds = progress * profile.duration;
        result.push(Math.max(profile.min, -seconds * (60 / .84)));
        continue;
      }
      const frequency = 20 * Math.pow(1000, progress);
      let db;
      if (state.mode === 'sweep') {
        const log = Math.log(frequency);
        db = .65 * Math.sin(log * 1.8) + .38 * Math.sin(log * 4.2 + .4);
        db += 2.05 * Math.exp(-.5 * Math.pow((log - Math.log(2100)) / .32, 2));
        db -= 1.55 * Math.exp(-.5 * Math.pow((log - Math.log(7200)) / .22, 2));
        db += .07 * Math.sin(i * .28);
      } else {
        db = profile.baseline + profile.noise * Math.sin(i * .17) + profile.noise * .56 * Math.sin(i * .53 + state.tick * .11);
        if (state.mode === 'rta') db += 5.5 * Math.exp(-Math.pow((Math.log(frequency) - Math.log(260)) / .48, 2));
        for (const [peakFrequency, amplitude, bandwidth] of state.mode === 'loop' ? [] : profile.peaks || []) {
          const offset = (Math.log(frequency) - Math.log(peakFrequency)) / bandwidth;
          db += amplitude * Math.exp(-.5 * offset * offset);
        }
        if (harmonicFrame) {
          for (const component of harmonicFrame.components.filter(item => item.inBand)) {
            // Peak envelope over each display bin preserves narrow harmonic peaks.
            const distance = Math.max(0, Math.abs(Math.log(frequency / component.frequencyHz)) - Math.log(1000) / 510);
            const level = component.levelDbfs - 10 * Math.LOG10E * (distance / .016) ** 2;
            db = 10 * Math.log10(10 ** (db / 10) + 10 ** (level / 10));
          }
        }
        if (state.toneOn && state.mode !== 'loop') {
          const toneFrequency = Math.max(20, Math.min(20000, Number($('toneFrequency').value) || 1000));
          const offset = (Math.log(frequency) - Math.log(toneFrequency)) / .025;
          const toneDb = Number($('toneLevel').value) - 10 * Math.LOG10E * offset * offset;
          db = 10 * Math.log10(10 ** (db / 10) + 10 ** (toneDb / 10));
        }
      }
      result.push(Math.max(profile.min, Math.min(profile.max, db)));
    }
    return result;
  }

  function setAxisLabels(profile) {
    const yAxis = $('spectrumYAxis');
    yAxis.replaceChildren(...profile.yTicks.map((value, index) => {
      const label = document.createElement('span');
      label.textContent = value > 0 ? `+${value}` : `${value}`.replace('-', '−');
      label.style.top = `${yFraction(value, profile) * 100}%`;
      label.style.transform = 'translateY(-50%)';
      return label;
    }));
    const xAxis = $('spectrumXAxis');
    xAxis.dataset.axis = profile.axis;
    xAxis.replaceChildren(...profile.xTicks.map(([value, text], index) => {
      const label = document.createElement('span');
      label.textContent = text;
      label.style.left = `${xFraction(value, profile) * 100}%`;
      if (index === 0) label.style.transform = 'none';
      else if (index === profile.xTicks.length - 1) label.style.transform = 'translateX(-100%)';
      else label.style.transform = 'translateX(-50%)';
      return label;
    }));
  }

  function setMetricValue(id, value, unit) {
    $(id).innerHTML = `${value}<span>${unit}</span>`;
  }

  function setMetricLabel(id, text) {
    $(id).innerHTML = `${text} <span>ⓘ</span>`;
  }

  function applyMode() {
    const profile = modeProfiles[state.mode];
    $('spectrumTitle').textContent = profile.title;
    $('spectrumModeTag').textContent = profile.tag;
    $('spectrumDescription').textContent = profile.description;
    $('chartMetricName').textContent = profile.metric;
    $('chartUnit').textContent = profile.unit;
    $('chartRateLabel').textContent = state.mode === 'rta' ? '更新' : '模式';
    $('chartRateValue').textContent = state.mode === 'rta' ? '10 Hz' : (state.mode === 'sweep' ? '121 点' : state.mode === 'decay' ? '8 s' : '1 kHz');
    $('waveformTitle').textContent = state.mode === 'decay' ? '事件波形' : state.mode === 'sweep' ? '输出波形' : '波形包络';
    $('waveformDescription').textContent = profile.wave;
    $('inputLabel').textContent = profile.readings.input;
    $('readingsTitle').textContent = '测量读数';
    $('readingsDescription').textContent = `${profile.title} · ${profile.unit} · 模拟数据`;

    const r = profile.readings;
    setMetricLabel('rmsLabel', r.rms[0]); setMetricValue('rmsValue', r.rms[1], r.rms[2]);
    $('rmsFoot').innerHTML = `<span class="trend-icon">↗</span>${r.rms[3]} <b>${r.rms[4]}</b>`;
    setMetricLabel('peakLabel', r.peak[0]); setMetricValue('peakValue', r.peak[1], r.peak[2]);
    $('peakFoot').innerHTML = `<span class="peak-indicator"></span>${r.peak[3]} <b>${r.peak[4]}</b>`;
    setMetricLabel('freqLabel', r.freq[0]); setMetricValue('freqValue', r.freq[1], r.freq[2]);
    $('freqFoot').innerHTML = `<span class="mini-wave">∿</span>${r.freq[3]}`;
    setMetricLabel('clipLabel', r.clip[0]); setMetricValue('clipValue', r.clip[1], r.clip[2]);
    $('clipFoot').innerHTML = `<span class="ok-check">✓</span>${r.clip[3]}`;
    $('spectrumCanvas').setAttribute('aria-label', `模拟${profile.title}图`);
    setAxisLabels(profile);
    plotData = [];
    harmonicFrame = null;
  }

  function drawSpectrum() {
    const fit = fitCanvas(spectrumCanvas);
    if (!fit) return;
    const { ctx, width: w, height: h } = fit;
    const profile = modeProfiles[state.mode];
    if (!state.paused || plotData.length === 0) plotData = makePlotData();
    renderHarmonics();
    ctx.clearRect(0, 0, w, h);

    for (const value of profile.yTicks) {
      const y = yFraction(value, profile) * h;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y);
      ctx.strokeStyle = 'rgba(128,153,174,.16)'; ctx.lineWidth = 1; ctx.stroke();
    }
    for (const [value] of profile.xTicks) {
      const x = xFraction(value, profile) * w;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h);
      ctx.strokeStyle = 'rgba(128,153,174,.09)'; ctx.lineWidth = 1; ctx.stroke();
    }
    const points = plotData.map((value, i) => {
      const progress = i / (plotData.length - 1);
      return { x: progress * w, y: yFraction(value, profile) * h, value };
    });
    const fill = ctx.createLinearGradient(0, 0, 0, h);
    fill.addColorStop(0, profile.fill); fill.addColorStop(1, 'rgba(20,35,45,0)');
    ctx.beginPath(); ctx.moveTo(points[0].x, h);
    for (const p of points) ctx.lineTo(p.x, p.y);
    ctx.lineTo(points.at(-1).x, h); ctx.closePath(); ctx.fillStyle = fill; ctx.fill();
    ctx.beginPath(); points.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y));
    ctx.strokeStyle = profile.color; ctx.lineWidth = 1.7; ctx.lineJoin = 'round'; ctx.stroke();

    if (harmonicFrame) {
      ctx.font = '11px monospace'; ctx.textAlign = 'center';
      for (const item of harmonicFrame.components.filter(item => item.inBand)) {
        const x = xFraction(item.frequencyHz, profile) * w;
        ctx.beginPath(); ctx.moveTo(x, w < 500 ? 40 : 26); ctx.lineTo(x, h);
        ctx.setLineDash([2, 5]); ctx.strokeStyle = '#ae9bff33'; ctx.stroke(); ctx.setLineDash([]);
        ctx.fillStyle = '#c5b8ff'; ctx.fillText(`H${item.order}`, Math.max(10, Math.min(w - 10, x)), w < 500 && item.order % 2 === 0 ? 32 : 16);
      }
    }

    const annotation = profile.annotation;
    const annotationIndex = state.mode === 'decay'
      ? Math.round(.28 / profile.duration * (plotData.length - 1))
      : plotData.reduce((best, value, index) => value > plotData[best] ? index : best, 0);
    const annotationLevel = harmonicFrame ? harmonicFrame.components[0].levelDbfs : plotData[annotationIndex];
    const annotationFraction = annotationIndex / (plotData.length - 1);
    const annotationFrequency = harmonicFrame ? harmonicFrame.fundamentalHz : 20 * Math.pow(1000, annotationFraction);
    const annotationX = harmonicFrame ? xFraction(annotationFrequency, profile) * w : annotationFraction * w;
    const annotationY = yFraction(annotationLevel, profile) * h;
    ctx.beginPath(); ctx.moveTo(annotationX, Math.min(h, annotationY + 9)); ctx.lineTo(annotationX, h);
    ctx.setLineDash([3, 4]); ctx.strokeStyle = profile.color + '88'; ctx.lineWidth = 1; ctx.stroke(); ctx.setLineDash([]);
    ctx.beginPath(); ctx.arc(annotationX, annotationY, 3.2, 0, Math.PI * 2); ctx.fillStyle = profile.color; ctx.fill();

    $('tooltipTitle').textContent = annotation.title;
    $('tooltipValue').textContent = state.mode === 'decay' ? '0.84 s' : `${annotationFrequency >= 1000 ? (annotationFrequency / 1000).toFixed(2) + ' kHz' : annotationFrequency.toFixed(1) + ' Hz'}`;
    $('tooltipLevel').textContent = `${annotationLevel > 0 ? '+' : ''}${annotationLevel.toFixed(1)} ${profile.unit}`.replace('-', '−');
    const tooltip = $('spectrumTooltip');
    const tooltipWidth = tooltip.offsetWidth, tooltipHeight = tooltip.offsetHeight;
    tooltip.style.transform = 'none';
    const tooltipX = annotationX >= tooltipWidth + 16 ? annotationX - tooltipWidth - 12 : annotationX + 12;
    tooltip.style.left = `${Math.max(4, Math.min(w - tooltipWidth - 4, tooltipX))}px`;
    tooltip.style.top = `${Math.max(4, Math.min(h - tooltipHeight - 4, annotationY - tooltipHeight - 10))}px`;
    tooltip.style.borderColor = profile.color + '55';
    $('tooltipValue').style.color = profile.color;
  }

  function drawWaveform() {
    const fit = fitCanvas(waveformCanvas);
    if (!fit) return;
    const { ctx, width: w, height: h } = fit;
    ctx.clearRect(0, 0, w, h);
    const mid = h / 2, amp = h / 2, mins = [], maxs = [];
    for (let i = 0; i < 64; i++) {
      const phase0 = (i / 64) * Math.PI * 2 * 10.67 + state.tick * .05;
      let lo = Infinity, hi = -Infinity;
      for (let j = 0; j < 8; j++) {
        const phase = phase0 + j * (Math.PI * 2 * 10.67 / 512);
        const pure = Math.sin(phase);
        let sample = state.mode === 'loop' ? .68 * pure : .48 * pure + .055 * Math.sin(phase * 2.12 + .3) + .018 * Math.sin(phase * .35);
        if (harmonicFrame) sample = harmonicFrame.components.filter(item => item.inBand).reduce((sum, item) => sum + 10 ** (item.levelDbfs / 20) * Math.sin(phase * item.order * harmonicFrame.fundamentalHz / 1000), 0);
        if (state.mode === 'decay') sample *= Math.exp(-(i / 64) * 2.1);
        if (state.mode === 'sweep') sample = .5 * Math.sin(phase) + .15 * Math.sin(phase * 1.73);
        lo = Math.min(lo, sample); hi = Math.max(hi, sample);
      }
      mins.push(lo); maxs.push(hi);
    }
    ctx.beginPath();
    for (let i = 0; i < 64; i++) {
      const x = (i + .5) / 64 * w;
      ctx.moveTo(x, mid - maxs[i] * amp); ctx.lineTo(x, mid - mins[i] * amp);
    }
    const color = modeProfiles[state.mode].color;
    ctx.lineCap = 'round'; ctx.lineWidth = 1.35; ctx.strokeStyle = color + 'dd'; ctx.stroke();
    ctx.beginPath();
    for (let i = 0; i < 64; i++) {
      const x = (i + .5) / 64 * w, y = mid - (maxs[i] + mins[i]) * .5 * amp;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = color; ctx.lineWidth = 1; ctx.stroke();
  }

  function updateScopeReadouts() {
    const scaleText = `${state.verticalScale.toFixed(1)} FS/div`;
    const timeText = `${state.timebase.toFixed(2)} ms/div`;
    const levelText = `${state.triggerLevel >= 0 ? '+' : ''}${state.triggerLevel.toFixed(2)} FS`.replace('-', '−');
    const arrow = state.triggerSlope === 'rising' ? '↑' : '↓';
    const frequency = scopeData.length ? scopeFrequency : 1000;
    const periodMs = 1000 / frequency;
    const formatTime = (value) => Math.abs(value) < 10 ? value.toFixed(2) : value.toFixed(1);

    $('scopeTimeReadout').textContent = timeText;
    $('scopeScaleReadout').textContent = scaleText;
    $('scopeTriggerReadout').textContent = `CH1 ${arrow} ${levelText}`;
    $('scopeFreqOverlay').textContent = `${(frequency / 1000).toFixed(3)} kHz`;
    $('scopeAcqStatus').textContent = state.single ? 'SINGLE' : state.scopePaused ? 'STOP' : 'RUN';
    $('scopeAcquisitionLabel').textContent = `${state.triggerMode === 'auto' ? '自动触发' : '普通触发'} · ${state.single ? '单次采集' : '连续采集'}`;
    $('triggerLevelValue').textContent = levelText;

    const extent = state.verticalScale * 4;
    const yValues = [extent, extent / 2, 0, -extent / 2, -extent];
    $('scopeYAxis').replaceChildren(...yValues.map((value, index) => {
      const label = document.createElement('span');
      label.textContent = value === 0 ? '0' : `${value > 0 ? '+' : '−'}${Math.abs(value).toFixed(2)}`;
      label.style.top = `${index * 25}%`;
      label.style.transform = 'translateY(-50%)';
      return label;
    }));

    const halfWindow = state.timebase * 5;
    const timeValues = [-halfWindow, -halfWindow / 2, 0, halfWindow / 2, halfWindow];
    $('scopeXAxis').replaceChildren(...timeValues.map((value, index) => {
      const label = document.createElement('span');
      const shown = value === 0 ? '0' : `${value > 0 ? '+' : '−'}${formatTime(Math.abs(value))}`;
      label.textContent = index === timeValues.length - 1 ? `${shown} ms` : shown;
      label.style.left = `${index * 25}%`;
      return label;
    }));

    if (state.view === 'scope') {
      $('topScaleLabel').textContent = '时基'; $('topScaleValue').textContent = state.timebase.toFixed(2);
      $('topScaleUnit').textContent = 'ms/div'; $('topScaleHint').textContent = `总窗口 ${formatTime(state.timebase * 10)} ms · 48 kS/s`;
      $('readingsTitle').textContent = '示波器测量';
      $('readingsDescription').textContent = 'CH1 输入 · 相对满量程 FS';
      const values = scopeData.map(point => point.level);
      const vpp = values.length ? Math.max(...values) - Math.min(...values) : 0;
      const rms = values.length ? Math.sqrt(values.reduce((sum, value) => sum + value * value, 0) / values.length) : 0;
      setMetricLabel('rmsLabel', '峰峰值'); setMetricValue('rmsValue', vpp.toFixed(3), 'FS');
      $('rmsFoot').textContent = '由当前模拟采样帧计算';
      setMetricLabel('peakLabel', 'RMS 有效值'); setMetricValue('peakValue', rms.toFixed(3), 'FS');
      $('peakFoot').textContent = `${state.coupling.toUpperCase()} 耦合 · 当前帧`;
      setMetricLabel('freqLabel', '频率'); setMetricValue('freqValue', (frequency / 1000).toFixed(3), 'kHz');
      $('freqFoot').innerHTML = '<span class="mini-wave">∿</span>CH1 周期测量';
      setMetricLabel('clipLabel', '周期'); setMetricValue('clipValue', periodMs.toFixed(2), 'ms');
      $('clipFoot').textContent = state.scopePaused ? '采样帧已保持' : '模拟信号周期';
    }
  }

  function updateTopForSpectrum() {
    $('topScaleLabel').textContent = 'FFT 长度'; $('topScaleValue').textContent = state.fft.toLocaleString('en-US');
    $('topScaleUnit').textContent = '点'; $('topScaleHint').textContent = `频率分辨率 ${(48000 / state.fft).toFixed(2)} Hz`;
  }

  function drawScope() {
    const fit = fitCanvas(scopeCanvas);
    if (!fit) return;
    const { ctx, width: w, height: h } = fit;
    ctx.clearRect(0, 0, w, h);

    const divisionsX = 10, divisionsY = 8;
    ctx.save();
    ctx.setLineDash([1, 4]);
    ctx.lineWidth = 1;
    for (let i = 0; i <= divisionsX * 5; i++) {
      const x = i / (divisionsX * 5) * w;
      const major = i % 5 === 0;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h);
      ctx.strokeStyle = major ? 'rgba(143,184,137,.24)' : 'rgba(143,184,137,.095)'; ctx.stroke();
    }
    for (let i = 0; i <= divisionsY * 5; i++) {
      const y = i / (divisionsY * 5) * h;
      const major = i % 5 === 0;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y);
      ctx.strokeStyle = major ? 'rgba(143,184,137,.24)' : 'rgba(143,184,137,.095)'; ctx.stroke();
    }
    ctx.setLineDash([]);
    const zeroY = h / 2;
    ctx.beginPath(); ctx.moveTo(0, zeroY); ctx.lineTo(w, zeroY);
    ctx.strokeStyle = 'rgba(174,218,145,.33)'; ctx.lineWidth = 1; ctx.stroke();

    const triggerY = h / 2 - (state.triggerLevel / state.verticalScale) * (h / divisionsY);
    ctx.beginPath(); ctx.moveTo(0, triggerY); ctx.lineTo(w, triggerY);
    ctx.setLineDash([5, 5]); ctx.strokeStyle = 'rgba(255,206,91,.42)'; ctx.lineWidth = 1; ctx.stroke(); ctx.setLineDash([]);

    const frequency = state.toneOn ? (Number($('toneFrequency').value) || 1000) : 1000;
    const totalSeconds = state.timebase * divisionsX / 1000;
    const samples = Math.min(4800, Math.max(512, Math.round(totalSeconds * 48000)));
    const amplitude = state.toneOn ? 10 ** (Number($('toneLevel').value) / 20) : .48;
    const levelRatio = Math.max(-.98, Math.min(.98, state.triggerLevel / amplitude));
    const crossing = Math.asin(levelRatio);
    const phaseAtCenter = state.triggerSlope === 'rising' ? crossing : Math.PI - crossing;
    const dcOffset = state.coupling === 'dc' ? .10 : 0;
    if (!state.scopePaused || scopeData.length === 0) {
      scopeData = [];
      scopeFrequency = frequency;
      for (let i = 0; i < samples; i++) {
        const secondsFromCenter = (i / (samples - 1) - .5) * totalSeconds;
        const phase = (2 * Math.PI * frequency * secondsFromCenter) + phaseAtCenter;
        const signal = amplitude * Math.sin(phase) + amplitude * .073 * Math.sin(2 * phase + .25) + dcOffset;
        scopeData.push({x: secondsFromCenter, level: signal});
      }
    }
    ctx.beginPath();
    scopeData.forEach((point, index) => {
      const x = (point.x / totalSeconds + .5) * w;
      const y = h / 2 - (point.level / state.verticalScale) * (h / divisionsY);
      if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.strokeStyle = '#a7f36c'; ctx.lineWidth = 1.5; ctx.shadowColor = 'rgba(155,239,93,.55)'; ctx.shadowBlur = 7; ctx.stroke(); ctx.restore();

    updateScopeReadouts();
    syncDisplayStatus();
  }

  function syncDisplayStatus() {
    const paused = state.view === 'scope' ? state.scopePaused : state.paused;
    $('readingsUpdate').textContent = paused ? '画面已保持' : '演示更新中';
    document.querySelector('.scope-live').innerHTML = paused ? '已停止' : '<i></i>采集';
    document.querySelector('.scope-live').classList.toggle('is-paused', paused);
    document.querySelector('.live-tag').classList.toggle('is-paused', paused);
  }

  function setView(view) {
    state.view = view;
    const isScope = view === 'scope';
    $('scopePanel').classList.toggle('is-hidden', !isScope);
    $('spectrumPanel').classList.toggle('is-hidden', isScope);
    $('harmonicsPanel').classList.toggle('is-hidden', isScope);
    $('waveformPanel').classList.toggle('is-hidden', isScope);
    $('scopeControlsPanel').classList.toggle('is-hidden', !isScope);
    $('spectrumControlsPanel').classList.toggle('is-hidden', isScope);
    document.querySelector('.workspace-grid').classList.toggle('scope-view-active', isScope);
    $('scopeTab').classList.toggle('is-active', isScope); $('scopeTab').setAttribute('aria-selected', String(isScope));
    $('spectrumTab').classList.toggle('is-active', !isScope); $('spectrumTab').setAttribute('aria-selected', String(!isScope));
    $('viewHint').textContent = isScope ? '时域 · CH1 输入波形' : '频域 · 256 点频谱预览';
    if (isScope) updateScopeReadouts(); else { applyMode(); updateTopForSpectrum(); }
    redraw();
    syncDisplayStatus();
  }

  function redraw() {
    if (state.view === 'scope') drawScope();
    else { drawSpectrum(); drawWaveform(); }
  }
  let lastFrame = 0;
  function animate(now) {
    if (now - lastFrame > 100) {
      lastFrame = now;
      if (!(state.view === 'scope' ? state.scopePaused : state.paused)) { state.tick++; redraw(); }
    }
    window.requestAnimationFrame(animate);
  }

  function showToast(message) {
    const toast = $('toast'); toast.textContent = message; toast.classList.add('show');
    window.clearTimeout(state.toastTimer);
    state.toastTimer = window.setTimeout(() => toast.classList.remove('show'), 2300);
  }

  function download(filename, content, type) {
    const blob = new Blob([content], { type });
    const url = URL.createObjectURL(blob), link = document.createElement('a');
    link.href = url; link.download = filename; document.body.appendChild(link); link.click(); link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  function getExportPayload() {
    if (state.view === 'scope') {
      return {
        product: 'AcousticLab', demo: true, timestamp: new Date().toISOString(),
        mode: 'scope', axis: 'seconds', unit: 'FS', calibrated: false,
        timebaseMsPerDiv: state.timebase, scaleFsPerDiv: state.verticalScale,
        sampleCount: scopeData.length, spectrumPreview: scopeData.map(point => ({...point}))
      };
    }
    const profile = modeProfiles[state.mode];
    return {
      product: 'AcousticLab', demo: true, timestamp: new Date().toISOString(),
      device: '未连接 · 模拟数据', mode: profile.title,
      sampleRateHz: 48000, fftLength: state.fft, previewPoints: 256,
      axis: profile.axis === 'time' ? 'seconds' : 'frequencyHz', unit: profile.unit, calibrated: false,
      harmonics: harmonicFrame,
      spectrumPreview: plotData.map((value, i) => {
        const x = profile.axis === 'time' ? i / 255 * profile.duration : 20 * Math.pow(1000, i / 255);
        return { x: Number(x.toFixed(2)), level: Number(value.toFixed(1)) };
      })
    };
  }

  $('fftSelect').addEventListener('change', (event) => {
    state.fft = Number(event.target.value);
    $('windowStatus').textContent = `${(state.fft / 48000 * 1000).toFixed(2)} ms`;
    if (state.view === 'spectrum') updateTopForSpectrum();
    showToast(`演示设置已切换为 ${state.fft.toLocaleString('en-US')} 点 FFT`);
  });

  document.querySelectorAll('.view-tab').forEach((button) => button.addEventListener('click', () => setView(button.dataset.view)));

  $('timebaseSelect').addEventListener('change', (event) => {
    state.timebase = Number(event.target.value); updateScopeReadouts(); redraw();
  });
  $('verticalScaleSelect').addEventListener('change', (event) => {
    state.verticalScale = Number(event.target.value); updateScopeReadouts(); redraw();
  });
  $('triggerLevel').addEventListener('input', (event) => {
    state.triggerLevel = Number(event.target.value); updateScopeReadouts(); redraw();
  });
  $('triggerSlopeSelect').addEventListener('change', (event) => {
    state.triggerSlope = event.target.value; updateScopeReadouts(); redraw();
  });
  $('couplingSelect').addEventListener('change', (event) => {
    state.coupling = event.target.value; redraw();
    showToast(`${state.coupling.toUpperCase()} 耦合（演示）`);
  });
  document.querySelectorAll('[data-trigger-mode]').forEach((button) => button.addEventListener('click', () => {
    state.triggerMode = button.dataset.triggerMode;
    document.querySelectorAll('[data-trigger-mode]').forEach((item) => item.classList.toggle('is-selected', item === button));
    updateScopeReadouts();
    syncDisplayStatus();
  }));
  $('scopeRunButton').addEventListener('click', () => {
    state.scopePaused = !state.scopePaused; state.single = false;
    $('scopeRunButton').classList.toggle('is-stopped', state.scopePaused);
    $('scopeRunText').textContent = state.scopePaused ? '运行' : '停止';
    updateScopeReadouts();
    syncDisplayStatus();
    showToast(state.scopePaused ? '采集已停止，波形保持' : '采集已恢复');
  });
  $('scopeSingleButton').addEventListener('click', () => {
    scopeData = [];
    state.tick++; state.scopePaused = true; state.single = true;
    $('scopeRunButton').classList.add('is-stopped'); $('scopeRunText').textContent = '运行';
    updateScopeReadouts(); redraw(); showToast('单次采集完成，波形已保持');
  });
  $('scopeAutoButton').addEventListener('click', () => {
    state.timebase = 1; state.verticalScale = .2; state.triggerLevel = 0;
    state.triggerSlope = 'rising'; state.triggerMode = 'auto'; state.coupling = 'ac';
    state.scopePaused = false; state.single = false;
    $('timebaseSelect').value = '1'; $('verticalScaleSelect').value = '0.2'; $('triggerLevel').value = '0';
    $('triggerSlopeSelect').value = 'rising'; $('couplingSelect').value = 'ac';
    document.querySelectorAll('[data-trigger-mode]').forEach((item) => item.classList.toggle('is-selected', item.dataset.triggerMode === 'auto'));
    $('scopeRunButton').classList.remove('is-stopped'); $('scopeRunText').textContent = '停止';
    updateScopeReadouts(); redraw(); showToast('AUTOSET：已恢复 1 ms/div、0.2 FS/div 和上升沿触发');
  });

  $('modeSelect').addEventListener('change', (event) => {
    state.mode = event.target.value;
    state.paused = false;
    state.tick++;
    $('analysisButton').classList.remove('is-start'); $('analysisButtonText').textContent = '暂停绘图';
    $('analysisState').classList.remove('paused'); $('analysisState').innerHTML = '<span></span>运行中';
    applyMode(); redraw();
    syncDisplayStatus();
    showToast(`已切换到「${modeProfiles[state.mode].title}」演示`);
  });

  $('analysisButton').addEventListener('click', () => {
    state.paused = !state.paused;
    $('analysisButton').classList.toggle('is-start', state.paused);
    $('analysisButtonText').textContent = state.paused ? '继续绘图' : '暂停绘图';
    $('analysisState').classList.toggle('paused', state.paused);
    $('analysisState').innerHTML = state.paused ? '<span></span>绘图已暂停' : '<span></span>运行中';
    if (!state.paused) { state.tick++; redraw(); }
    syncDisplayStatus();
    showToast(state.paused ? '已暂停页面绘图' : '已恢复页面绘图');
    renderHarmonics();
  });

  $('resetButton').addEventListener('click', () => {
    state.tick = 0; state.paused = false;
    $('analysisButton').classList.remove('is-start'); $('analysisButtonText').textContent = '暂停绘图';
    $('analysisState').classList.remove('paused'); $('analysisState').innerHTML = '<span></span>运行中';
    applyMode(); redraw(); syncDisplayStatus(); showToast('当前模式的演示图表已重置');
  });

  $('toneButton').addEventListener('click', () => {
    state.toneOn = !state.toneOn;
    $('toneButton').classList.toggle('is-on', state.toneOn);
    $('toneButtonText').textContent = state.toneOn ? `正在模拟输出 ${Number($('toneFrequency').value || 1000).toLocaleString()} Hz` : '开启测试音';
    $('toneButtonIcon').textContent = state.toneOn ? '■' : '▶';
    updateScopeReadouts(); redraw(); showToast(state.toneOn ? '已开启测试音模拟；不会产生实际声音' : '已停止测试音模拟');
  });
  $('toneFrequency').addEventListener('change', () => {
    $('toneFrequency').value = String(Math.max(20, Math.min(20000, Number($('toneFrequency').value) || 1000)));
    if (state.toneOn) { $('toneButtonText').textContent = `正在模拟输出 ${Number($('toneFrequency').value || 1000).toLocaleString()} Hz`; updateScopeReadouts(); redraw(); }
  });
  $('toneLevel').addEventListener('input', (event) => {
    const value = Number(event.target.value), percent = ((value + 40) / 34) * 100;
    $('toneLevelLabel').textContent = `−${Math.abs(value)} dBFS`;
    event.target.style.background = `linear-gradient(90deg,#50c3bf 0%,#50c3bf ${percent}%,#2c3a46 ${percent}%)`;
    redraw();
  });

  $('harmonicsDemoButton').addEventListener('click', () => {
    $('modeSelect').value = 'loop';
    $('modeSelect').dispatchEvent(new Event('change'));
  });
  $('harmonicsCsvButton').addEventListener('click', () => {
    if (!harmonicFrame?.valid) return;
    const rows = ['order,frequency_Hz,level_dBFS,relative_dBc,amplitude_percent,THD_percent,demo',
      ...harmonicFrame.components.map(item => `${item.order},${item.frequencyHz},${item.levelDbfs},${item.dbc},${(item.amplitudeRatio * 100).toFixed(6)},${harmonicFrame.thdPercent.toFixed(6)},true`)];
    download('acousticlab-demo-harmonics.csv', '\ufeff' + rows.join('\r\n'), 'text/csv;charset=utf-8');
    showToast('H1～H5 模拟谐波数据已导出');
  });

  $('csvButton').addEventListener('click', () => {
    const data = getExportPayload();
    const rows = [`${data.axis},level_${data.unit}`, ...data.spectrumPreview.map((point) => `${point.x},${point.level}`)];
    download(`acousticlab-demo-${state.view === 'scope' ? 'scope' : state.mode}.csv`, '\ufeff' + rows.join('\r\n'), 'text/csv;charset=utf-8');
    showToast(`当前画面的 ${data.spectrumPreview.length} 个示例数据点已导出`);
  });
  $('jsonButton').addEventListener('click', () => {
    download(`acousticlab-demo-${state.view === 'scope' ? 'scope' : state.mode}.json`, JSON.stringify(getExportPayload(), null, 2), 'application/json');
    showToast('当前模式的 JSON 示例已导出');
  });

  const modal = $('aboutModal'), closeModal = () => { modal.hidden = true; };
  $('aboutButton').addEventListener('click', () => { modal.hidden = false; $('closeAbout').focus(); });
  $('closeAbout').addEventListener('click', closeModal);
  $('closeAboutAction').addEventListener('click', closeModal);
  modal.addEventListener('click', (event) => { if (event.target === modal) closeModal(); });
  document.addEventListener('keydown', (event) => { if (event.key === 'Escape') closeModal(); });
  $('dismissNote').addEventListener('click', () => $('dismissNote').closest('.demo-note').remove());

  const observer = new ResizeObserver(redraw);
  observer.observe(scopeCanvas); observer.observe(spectrumCanvas); observer.observe(waveformCanvas);
  applyMode(); setView('scope'); window.requestAnimationFrame(animate);
})();


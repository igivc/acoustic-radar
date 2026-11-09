/* Управление графиками. Все сигналы синтетические: доступ к аудиоустройствам не нужен. */
(() => {
  'use strict';
  const D = window.ChirpDSP;
  const $ = id => document.getElementById(id);
  const colors = { grid: '#253242', text: '#8d9eb3', teal: '#64e5c0', amber: '#ffbf69', purple: '#aa9bff', input: '#9fb3ca' };
  const controls = ['f0', 'f1', 'duration', 'delay', 'gain', 'noise', 'threshold', 'marker'];
  const format = (value, digits = 0) => Number.isFinite(value) ? value.toLocaleString('ru-RU', { maximumFractionDigits: digits, minimumFractionDigits: digits }) : '—';
  let model, seed = 20261108, probe = 0, animation = null, updateRequest = null, drawRequest = null;

  function configuration() {
    return { f0: +$('f0').value, f1: +$('f1').value, durationMs: +$('duration').value,
      delayMs: +$('delay').value, gain: +$('gain').value, noise: +$('noise').value,
      threshold: +$('threshold').value, marker: $('marker').checked, seed };
  }
  function setup(canvasId, xMax, yMin, yMax, xStep, yTicks, unit) {
    const canvas = $(canvasId);
    const width = Math.max(100, canvas.getBoundingClientRect().width);
    const height = canvas.getBoundingClientRect().height;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(width * ratio); canvas.height = Math.round(height * ratio);
    const ctx = canvas.getContext('2d'); ctx.scale(ratio, ratio);
    const box = { left: 45, right: width - 10, top: 13, bottom: height - 25 };
    const x = value => box.left + value / xMax * (box.right - box.left);
    const y = value => box.bottom - (value - yMin) / (yMax - yMin) * (box.bottom - box.top);
    ctx.font = '10px Consolas, monospace'; ctx.lineWidth = 1;
    for (let tick = 0; tick <= xMax + .0001; tick += xStep) {
      ctx.strokeStyle = colors.grid; ctx.beginPath(); ctx.moveTo(x(tick), box.top); ctx.lineTo(x(tick), box.bottom); ctx.stroke();
      ctx.fillStyle = colors.text; ctx.textAlign = 'center'; ctx.fillText(format(tick, Number.isInteger(tick) ? 0 : 1) + (tick === 0 ? ' ' + unit : ''), x(tick), height - 7);
    }
    for (const tick of yTicks) {
      ctx.strokeStyle = colors.grid; ctx.beginPath(); ctx.moveTo(box.left, y(tick)); ctx.lineTo(box.right, y(tick)); ctx.stroke();
      ctx.fillStyle = colors.text; ctx.textAlign = 'right'; ctx.fillText(format(tick, Math.abs(tick) < 1 && tick !== 0 ? 2 : 0), box.left - 7, y(tick) + 3);
    }
    function path(values, color, stepMs, startMs = 0, scale = 1, dashed = false) {
      ctx.save(); ctx.beginPath(); ctx.rect(box.left, box.top, box.right - box.left, box.bottom - box.top); ctx.clip();
      ctx.strokeStyle = color; ctx.lineWidth = 1.15; ctx.setLineDash(dashed ? [3, 4] : []); ctx.beginPath();
      let started = false;
      for (let i = 0; i < values.length; i++) {
        if (!Number.isFinite(values[i])) { started = false; continue; }
        const px = x(startMs + i * stepMs), py = y(values[i] * scale);
        if (started) ctx.lineTo(px, py); else ctx.moveTo(px, py);
        started = true;
      }
      ctx.stroke(); ctx.restore();
    }
    return { ctx, box, x, y, path, width, height };
  }
  function line(plot, value, color, dashed = true) {
    const { ctx, box, x } = plot; ctx.save(); ctx.strokeStyle = color; ctx.setLineDash(dashed ? [4, 4] : []);
    ctx.beginPath(); ctx.moveTo(x(value), box.top); ctx.lineTo(x(value), box.bottom); ctx.stroke(); ctx.restore();
  }
  function draw() {
    if (!model) return;
    const { settings: s } = model;
    const reference = setup('reference', s.durationMs, -.12, .12, s.durationMs / 4, [-.1, 0, .1], 'мс');
    reference.path(model.envelope, '#697b8d', 1000 / D.FS, 0, .1, true);
    reference.path(model.envelope, '#697b8d', 1000 / D.FS, 0, -.1, true);
    reference.path(model.reference, colors.teal, 1000 / D.FS);
    const ramp = setup('frequency', s.durationMs, 0, 20, s.durationMs, [0, 10, 20], 'мс');
    ramp.path([s.f0 / 1000, s.f1 / 1000], colors.teal, s.durationMs);
    ramp.ctx.fillStyle = colors.text; ramp.ctx.textAlign = 'left'; ramp.ctx.fillText('кГц', 2, 10);
    const peakAmplitude = Math.max(...model.input.map(Math.abs));
    const limit = Math.max(.1, Math.ceil(peakAmplitude * 10) / 10);
    const input = setup('received', 80, -limit * 1.05, limit * 1.05, 10, [-limit, 0, limit], 'мс');
    const startMs = probe / D.FS * 1000;
    input.ctx.fillStyle = '#ffbf6910'; input.ctx.fillRect(input.x(startMs), input.box.top, input.x(startMs + s.durationMs) - input.x(startMs), input.box.bottom - input.box.top);
    input.path(model.input, colors.input, 1000 / D.FS);
    input.path(model.reference, colors.amber, 1000 / D.FS, startMs, s.gain);
    line(input, startMs, colors.amber, false); line(input, startMs + s.durationMs, '#ffbf6970');
    if (model.accepted) line(input, model.best.position / D.FS * 1000, colors.teal);
    const maximumMs = (model.values.length - 1) / D.FS * 1000;
    const corr = setup('correlation', maximumMs, -1.08, 1.08, 10, [-1, 0, 1], 'мс');
    corr.path(model.values, colors.purple, 1000 / D.FS);
    corr.ctx.save(); corr.ctx.strokeStyle = colors.teal; corr.ctx.setLineDash([5, 5]); corr.ctx.beginPath();
    corr.ctx.moveTo(corr.box.left, corr.y(s.threshold)); corr.ctx.lineTo(corr.box.right, corr.y(s.threshold)); corr.ctx.stroke(); corr.ctx.restore();
    corr.ctx.fillStyle = colors.teal; corr.ctx.textAlign = 'right'; corr.ctx.fillText('порог ' + format(s.threshold, 2), corr.box.right, corr.y(s.threshold) - 5);
    line(corr, startMs, colors.amber, false);
    const score = model.values[probe];
    if (Number.isFinite(score)) { corr.ctx.fillStyle = colors.amber; corr.ctx.beginPath(); corr.ctx.arc(corr.x(startMs), corr.y(score), 4, 0, Math.PI * 2); corr.ctx.fill(); }
    if (Number.isFinite(model.best.correlation)) {
      const bestTime = model.best.position / D.FS * 1000;
      corr.ctx.fillStyle = model.accepted ? colors.teal : colors.purple;
      corr.ctx.beginPath(); corr.ctx.arc(corr.x(bestTime), corr.y(model.best.correlation), 4, 0, Math.PI * 2); corr.ctx.fill();
    }
    $('probe-value').textContent = format(probe);
    $('probe-score').textContent = format(score, 3);
  }
  function requestDraw() { if (!drawRequest) drawRequest = requestAnimationFrame(() => { drawRequest = null; draw(); }); }
  function setProbe(value) {
    probe = Math.max(0, Math.min(model.values.length - 1, Math.round(value)));
    $('probe').value = probe; requestDraw();
  }
  function stopScan() {
    if (animation) cancelAnimationFrame(animation);
    animation = null; $('scan').textContent = '▶ Показать поиск'; $('scan').setAttribute('aria-pressed', 'false');
  }
  function scan() {
    if (animation) { stopScan(); return; }
    $('scan').textContent = 'Ⅱ Пауза'; $('scan').setAttribute('aria-pressed', 'true');
    const from = probe >= model.values.length - 1 ? 0 : probe;
    const started = performance.now();
    function advance(now) {
      const progress = Math.min(1, (now - started) / 5500);
      setProbe(from + progress * (model.values.length - 1 - from));
      if (progress < 1) animation = requestAnimationFrame(advance);
      else { stopScan(); setProbe(model.best.correlation === -Infinity ? 0 : model.best.position); }
    }
    animation = requestAnimationFrame(advance);
  }
  function updateFrame() {
    let base = Math.round(+$('base').value);
    if (!Number.isFinite(base)) base = 0;
    base = Math.max(0, Math.min(1000000000, base));
    const { accepted, best } = model;
    const coords = D.frameCoordinates(base, best.position);
    $('frame-timeline').classList.toggle('unavailable', !accepted);
    $('frame-description').textContent = accepted
      ? `${!model.settings.marker || model.settings.gain === 0 ? 'Детектор принял шум за маркер и назначил ложное t = 0. ' : 'Маркер в INPUT 1 задаёт t = 0. '}Первый центр FFT сигнала INPUT 2 находится на +${format(coords.firstTimeMs, 2)} мс от него. В секундном кадре — ${coords.columns} измерения спектра.`
      : 'Порог не пройден: начало спектрального кадра не устанавливается. Уменьшите шум, верните маркер или измените порог.';
    $('absolute-formula').textContent = accepted
      ? `marker = ${format(base)} + ${format(best.position)} = ${format(coords.marker)}; кадр [${format(coords.marker)}, ${format(coords.end)})`
      : 'marker = не определён; кадр не выделяется';
  }
  function calculate() {
    stopScan(); model = D.simulate(configuration());
    const s = model.settings;
    $('f0-value').textContent = format(s.f0 / 1000, 1) + ' кГц';
    $('f1-value').textContent = format(s.f1 / 1000, 1) + ' кГц';
    $('duration-value').textContent = format(s.durationMs) + ' мс · ' + model.reference.length;
    $('delay-value').textContent = format(model.delay / 48, 2) + ' мс';
    $('gain-value').textContent = '×' + format(s.gain, 2);
    $('noise-value').textContent = format(s.noise, 3);
    $('threshold-value').textContent = format(s.threshold, 2);
    $('probe').max = model.values.length - 1;
    setProbe(probe);
    $('best-index').textContent = Number.isFinite(model.best.correlation) ? format(model.best.position) + ' отсч.' : '—';
    $('best-score').textContent = format(model.best.correlation, 3);
    $('best-time').textContent = model.accepted ? format(model.best.position / 48, 3) + ' мс' : '—';
    $('error').textContent = model.accepted && s.marker && s.gain > 0 ? format(model.best.position - model.delay) + ' отсч.' : '—';
    const falsePositive = model.accepted && (!s.marker || s.gain === 0);
    $('result-title').textContent = falsePositive ? 'Ложное обнаружение' : model.accepted ? 'Начало найдено' : 'Кадр не выделен';
    $('decision').textContent = falsePositive ? 'Маркер отсутствует · порог слишком низкий' : model.accepted ? 'rmax ≥ порог · маркер принят' : 'Нет совпадения выше порога';
    $('decision').classList.toggle('rejected', !model.accepted || falsePositive);
    $('peak').disabled = !Number.isFinite(model.best.correlation);
    updateFrame(); requestDraw();
  }
  function requestCalculation() {
    if (!updateRequest) updateRequest = requestAnimationFrame(() => { updateRequest = null; calculate(); });
  }
  for (const id of controls) $(id).addEventListener('input', requestCalculation);
  $('base').addEventListener('input', updateFrame);
  $('probe').addEventListener('input', () => { stopScan(); setProbe(+$('probe').value); });
  $('scan').addEventListener('click', scan);
  $('peak').addEventListener('click', () => { stopScan(); setProbe(model.best.position); });
  $('new-noise').addEventListener('click', () => { seed++; calculate(); });
  $('reset').addEventListener('click', () => {
    for (const id of controls) { const el = $(id); if (el.type === 'checkbox') el.checked = el.defaultChecked; else el.value = el.defaultValue; }
    $('base').value = 100000; seed = 20261108; probe = 0; calculate();
  });
  $('correlation').addEventListener('click', event => {
    const bounds = event.currentTarget.getBoundingClientRect();
    const fraction = (event.clientX - bounds.left - 45) / (bounds.width - 55);
    stopScan(); setProbe(fraction * (model.values.length - 1));
  });
  $('correlation').addEventListener('keydown', event => {
    if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
      event.preventDefault(); stopScan(); setProbe(probe + (event.key === 'ArrowLeft' ? -1 : 1));
    }
  });
  document.addEventListener('keydown', event => {
    if (event.code === 'Space' && !['INPUT', 'BUTTON', 'SUMMARY', 'TEXTAREA'].includes(event.target.tagName)) { event.preventDefault(); scan(); }
  });
  $('fullscreen').addEventListener('click', async () => {
    try { if (document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen(); }
    catch { $('fullscreen').textContent = 'Используйте F11'; }
  });
  document.addEventListener('fullscreenchange', () => { $('fullscreen').textContent = document.fullscreenElement ? 'Выйти из экрана ↙' : 'На весь экран ↗'; });
  new ResizeObserver(requestDraw).observe(document.querySelector('.workspace'));
  calculate();
})();

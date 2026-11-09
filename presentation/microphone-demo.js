/* Показ одного синтетического кадра INPUT 2. FFT рассчитана до начала анимации. */
(() => {
  'use strict';
  const D = window.MicrophoneDSP, $ = id => document.getElementById(id);
  const color = { grid: '#253242', text: '#8d9eb3', input: '#b4c5d7', teal: '#64e5c0', purple: '#aa9bff', amber: '#ffbf69' };
  const format = (n, digits = 0) => n === null ? '—' : n.toLocaleString('ru-RU', { minimumFractionDigits: digits, maximumFractionDigits: digits });
  const text = (id, value) => { if ($(id).textContent !== value) $(id).textContent = value; };
  let model, column = 0, showAll = false, seed = 20261108, running = false, animation = null, lastTime = null, elapsed = 0, queued = null, drawing = null;
  function plot(id, minX, maxX, minY, maxY, yTicks, xSteps = 5, unit = 'мс', digits = 0) {
    const canvas = $(id), bounds = canvas.getBoundingClientRect(), ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(bounds.width * ratio); canvas.height = Math.round(bounds.height * ratio);
    const ctx = canvas.getContext('2d'); ctx.scale(ratio, ratio);
    const box = { left: 49, right: bounds.width - 13, top: 18, bottom: bounds.height - 26 };
    const x = v => box.left + (v - minX) / (maxX - minX) * (box.right - box.left);
    const y = v => box.bottom - (v - minY) / (maxY - minY) * (box.bottom - box.top);
    ctx.font = '10px Consolas, monospace'; ctx.lineWidth = 1;
    for (let i = 0; i <= xSteps; i++) {
      const value = minX + (maxX - minX) * i / xSteps;
      ctx.strokeStyle = color.grid; ctx.beginPath(); ctx.moveTo(x(value), box.top); ctx.lineTo(x(value), box.bottom); ctx.stroke();
      ctx.fillStyle = color.text; ctx.textAlign = 'center'; ctx.fillText(format(value, digits) + (i === 0 ? ' ' + unit : ''), x(value), bounds.height - 6);
    }
    for (const value of yTicks) {
      ctx.strokeStyle = color.grid; ctx.beginPath(); ctx.moveTo(box.left, y(value)); ctx.lineTo(box.right, y(value)); ctx.stroke();
      ctx.fillStyle = color.text; ctx.textAlign = 'right'; ctx.fillText(format(value, Math.abs(value) < 1 && value !== 0 ? 2 : 0), box.left - 6, y(value) + 3);
    }
    function trace(points, stroke) {
      ctx.save(); ctx.beginPath(); ctx.rect(box.left, box.top, box.right - box.left, box.bottom - box.top); ctx.clip();
      ctx.strokeStyle = stroke; ctx.lineWidth = 1.3; ctx.beginPath();
      points.forEach(([a, b], i) => { if (i) ctx.lineTo(x(a), y(b)); else ctx.moveTo(x(a), y(b)); });
      ctx.stroke(); ctx.restore();
    }
    function vertical(value, stroke, dashed = false) {
      ctx.save(); ctx.setLineDash(dashed ? [4, 4] : []); ctx.strokeStyle = stroke; ctx.beginPath(); ctx.moveTo(x(value), box.top); ctx.lineTo(x(value), box.bottom); ctx.stroke(); ctx.restore();
    }
    return { ctx, box, x, y, trace, vertical };
  }
  // Сохраняем диапазон значений каждого столбца пикселей, а не прореживаем сигнал:
  // это не превращает высокочастотную волну в ложный низкочастотный рисунок.
  function minmax(p, samples, begin, end, stroke) {
    const columns = Math.max(1, Math.floor(p.box.right - p.box.left));
    p.ctx.strokeStyle = stroke; p.ctx.lineWidth = 1; p.ctx.beginPath();
    for (let px = 0; px < columns; px++) {
      const a = begin + Math.floor((end - begin) * px / columns);
      const b = begin + Math.floor((end - begin) * (px + 1) / columns);
      let lo = Infinity, hi = -Infinity;
      for (let n = a; n < b; n++) { lo = Math.min(lo, samples[n]); hi = Math.max(hi, samples[n]); }
      if (b > a) { p.ctx.moveTo(p.box.left + px + .5, p.y(lo)); p.ctx.lineTo(p.box.left + px + .5, p.y(hi)); }
    }
    p.ctx.stroke();
  }
  function amplitude(samples) { let peak = .02; for (const value of samples) peak = Math.max(peak, Math.abs(value)); return Math.ceil(peak * 100) / 100; }
  function drawWave(selected) {
    const limit = amplitude(model.input);
    const p = plot('waveform', 0, 1000, -limit * 1.1, limit * 1.1, [-limit, 0, limit]);
    minmax(p, model.input, 0, D.PERIOD, color.input);
    if ($('components').checked) {
      minmax(p, model.direct, 0, D.PERIOD, '#64e5c080');
      minmax(p, model.ringing, 0, D.PERIOD, '#aa9bffb0');
    }
    p.ctx.fillStyle = '#ffbf6918'; p.ctx.fillRect(p.x(selected.offset / 48), p.box.top, p.x((selected.offset + D.SIZE) / 48) - p.x(selected.offset / 48), p.box.bottom - p.box.top);
    p.vertical(selected.offset / 48, color.amber); p.vertical((selected.offset + D.SIZE) / 48, color.amber);
    p.vertical(selected.centerMs, '#ffbf6970', true); p.vertical(model.excitationEndMs, '#9badbf', true);
  }
  function drawWindow(selected) {
    const raw = model.input.subarray(selected.offset, selected.offset + D.SIZE);
    const weighted = Float32Array.from(raw, (value, j) => value * D.window[j]);
    const limit = amplitude(raw);
    const p = plot('window', 0, D.SIZE / 48, -limit * 1.1, limit * 1.1, [-limit, 0, limit], 4, 'мс', 1);
    minmax(p, raw, 0, D.SIZE, '#71859c'); minmax(p, weighted, 0, D.SIZE, color.teal);
  }
  function drawSpectrum(selected) {
    const p = plot('spectrum', 1900, 2900, -100, 0, [-80, -40, 0], 4, 'Гц');
    const points = [];
    for (let b = 162; b <= 248; b++) points.push([b * D.FS / D.SIZE, selected.levels[b]]);
    p.trace(points, color.teal); p.vertical(model.config.frequency, color.purple, true);
    p.ctx.fillStyle = color.purple; p.ctx.textAlign = 'right'; p.ctx.fillText('fᵣ = ' + format(model.config.frequency) + ' Гц', p.box.right, 11);
    if (selected.levels[selected.peakBin] >= -100) {
      p.ctx.fillStyle = color.amber; p.ctx.beginPath(); p.ctx.arc(p.x(selected.peakBin * D.FS / D.SIZE), p.y(selected.levels[selected.peakBin]), 3.5, 0, 2 * Math.PI); p.ctx.fill();
    }
  }
  function drawSpectrogram(selected) {
    const p = plot('spectrogram', 0, 1000, 1900, 2900, [1900, 2100, 2300, 2500, 2700, 2900]);
    p.ctx.save(); p.ctx.beginPath(); p.ctx.rect(p.box.left, p.box.top, p.box.right - p.box.left, p.box.bottom - p.box.top); p.ctx.clip();
    const width = (p.box.right - p.box.left) * D.HOP / D.PERIOD;
    const height = (p.box.bottom - p.box.top) * (D.FS / D.SIZE) / 1000;
    const last = showAll ? model.columns.length - 1 : column;
    for (let index = 0; index <= last; index++) {
      const item = model.columns[index];
      for (let b = 163; b <= 247; b++) {
        const frequency = b * D.FS / D.SIZE, fraction = (frequency - 1900) / 1000, hue = fraction * 4;
        const alpha = Math.max(0, Math.min(1, (item.levels[b] + 80) / 65));
        if (alpha <= 0) continue;
        const red = Math.max(0, Math.min(1, 2 - hue)), green = Math.max(0, Math.min(1, Math.min(hue, 4 - hue))), blue = Math.max(0, Math.min(1, hue - 2));
        p.ctx.fillStyle = `rgba(${Math.round(red * 255)},${Math.round(green * 255)},${Math.round(blue * 255)},${alpha})`;
        const left = Math.round(p.x(item.centerMs) - width / 2), right = Math.round(p.x(item.centerMs) + width / 2);
        const top = Math.round(p.y(frequency) - height / 2), bottom = Math.round(p.y(frequency) + height / 2);
        p.ctx.fillRect(left, top, right - left, bottom - top);
      }
    }
    p.ctx.restore(); p.vertical(selected.centerMs, color.amber);
  }
  function updateIndices(selected) {
    let base = Math.round(+$('base').value);
    if (!Number.isFinite(base)) base = 0;
    base = Math.max(0, Math.min(1000000000, base));
    text('index-formula', `В буфере: [${format(selected.start)}, ${format(selected.end)}) · offset = ${format(selected.offset)}`);
    text('absolute-formula', `В потоке: [${format(base + selected.start)}, ${format(base + selected.end)})`);
  }
  function render() {
    if (!model) return;
    const selected = model.columns[column];
    drawWave(selected); drawWindow(selected); drawSpectrum(selected); drawSpectrogram(selected);
    $('column').value = column; text('column-number', `${column + 1} / ${model.columns.length}`);
    text('start-time', format(selected.offset / 48, 2) + ' мс'); text('center-time', format(selected.centerMs, 2) + ' мс');
    const peak = selected.levels[selected.peakBin];
    text('peak-frequency', peak <= -199 ? '—' : format(selected.peakBin * D.FS / D.SIZE, 2) + ' Гц');
    text('peak-level', format(peak, 1) + ' dBFS');
    const start = selected.offset / 48, end = (selected.offset + D.SIZE) / 48;
    const state = end <= model.excitationEndMs ? 'Во время возбуждения' : start >= model.excitationEndMs ? 'Послезвучие и шум' : 'Граница возбуждения и послезвучия';
    text('state-title', state); text('window-state', `${format(start, 2)}–${format(end, 2)} мс от маркера`);
    updateIndices(selected);
  }
  function requestDraw() { if (!drawing) drawing = requestAnimationFrame(() => { drawing = null; render(); }); }
  function stop() {
    running = false; lastTime = null; elapsed = 0;
    if (animation !== null) cancelAnimationFrame(animation);
    animation = null; text('play', '▶ Двигать окно FFT'); $('play').setAttribute('aria-pressed', 'false');
  }
  function choose(index) { column = Math.max(0, Math.min(model.columns.length - 1, Math.round(index))); requestDraw(); }
  function tick(now) {
    if (!running) return;
    if (lastTime !== null) elapsed += now - lastTime;
    lastTime = now;
    const step = +$('speed').value;
    while (elapsed >= step && column < model.columns.length - 1) { column++; elapsed -= step; }
    render();
    if (column === model.columns.length - 1) { stop(); return; }
    animation = requestAnimationFrame(tick);
  }
  function play() {
    if (running) { stop(); return; }
    if (column === model.columns.length - 1) { column = 0; showAll = false; updateShowAll(); }
    running = true; lastTime = null; elapsed = 0;
    text('play', 'Ⅱ Пауза'); $('play').setAttribute('aria-pressed', 'true'); animation = requestAnimationFrame(tick);
  }
  function updateShowAll() { text('show-all', showAll ? 'Скрыть будущие окна' : 'Показать весь кадр'); $('show-all').setAttribute('aria-pressed', String(showAll)); }
  function calculate() {
    stop();
    model = D.simulate({ frequency: +$('resonance').value, tauMs: +$('tau').value, resonanceGain: +$('strength').value,
      directGain: +$('direct').value, delayMs: +$('delay').value, noise: +$('noise').value, seed });
    const c = model.config;
    text('resonance-value', format(c.frequency) + ' Гц'); text('tau-value', format(c.tauMs) + ' мс'); text('strength-value', '×' + format(c.resonanceGain, 1));
    text('direct-value', '×' + format(c.directGain, 2)); text('delay-value', format(c.delayMs) + ' мс'); text('noise-value', format(c.noise, 4));
    const water = c.frequency === 2250;
    text('water', water ? 'Без воды: пример 2450 Гц' : 'С водой: пример 2250 Гц'); $('water').setAttribute('aria-pressed', String(water));
    requestDraw();
  }
  for (const id of ['resonance', 'tau', 'strength', 'direct', 'delay', 'noise']) $(id).addEventListener('input', () => {
    if (!queued) queued = requestAnimationFrame(() => { queued = null; calculate(); });
  });
  $('components').addEventListener('input', requestDraw);
  $('base').addEventListener('input', () => updateIndices(model.columns[column]));
  $('column').addEventListener('input', () => { stop(); choose(+$('column').value); });
  $('play').addEventListener('click', play);
  $('previous').addEventListener('click', () => { stop(); choose(column - 1); });
  $('next').addEventListener('click', () => { stop(); choose(column + 1); });
  $('show-all').addEventListener('click', () => { showAll = !showAll; updateShowAll(); requestDraw(); });
  $('ringdown').addEventListener('click', () => { stop(); choose(Math.ceil((model.excitationEndMs + 50 - D.SIZE / 96) / (D.HOP / 48))); });
  $('water').addEventListener('click', () => { $('resonance').value = model.config.frequency === 2250 ? 2450 : 2250; calculate(); });
  $('direct-only').addEventListener('click', () => { $('strength').value = 0; calculate(); });
  $('new-noise').addEventListener('click', () => { seed++; calculate(); });
  $('speed').addEventListener('change', () => { lastTime = null; elapsed = 0; });
  $('reset').addEventListener('click', () => {
    for (const id of ['resonance', 'tau', 'strength', 'direct', 'delay', 'noise']) $(id).value = $(id).defaultValue;
    $('components').checked = true; $('base').value = 100000; $('speed').value = '200'; seed = 20261108; column = 0; showAll = false; updateShowAll(); calculate();
  });
  $('spectrogram').addEventListener('keydown', event => {
    if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') { event.preventDefault(); stop(); choose(column + (event.key === 'ArrowLeft' ? -1 : 1)); }
  });
  document.addEventListener('keydown', event => {
    if (event.code === 'Space' && !['INPUT', 'SELECT', 'BUTTON', 'SUMMARY'].includes(event.target.tagName)) { event.preventDefault(); play(); }
  });
  $('fullscreen').addEventListener('click', async () => {
    try { if (document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen(); }
    catch { text('fullscreen', 'Используйте F11'); }
  });
  document.addEventListener('fullscreenchange', () => text('fullscreen', document.fullscreenElement ? 'Выйти из экрана ↙' : 'На весь экран ↗'));
  document.addEventListener('visibilitychange', () => { lastTime = null; });
  new ResizeObserver(requestDraw).observe(document.querySelector('.workspace'));
  calculate();
})();

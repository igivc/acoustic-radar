/* Графики и управление временной моделью. Страница не создаёт AudioContext. */
(() => {
  'use strict';
  const D = window.ExcitationDSP;
  const $ = id => document.getElementById(id);
  const colors = { grid: '#253242', text: '#8d9eb3', teal: '#64e5c0', amber: '#ffbf69', purple: '#aa9bff' };
  const format = (n, digits = 0) => n === null ? '—' : n.toLocaleString('ru-RU', { minimumFractionDigits: digits, maximumFractionDigits: digits });
  const text = (id, value) => { if ($(id).textContent !== value) $(id).textContent = value; };
  let model, position = 0, running = false, animation = null, lastTime = null, fractionalPosition = 0, drawRequest = null, settingsRequest = null;

  function plot(id, minX, maxX, minY, maxY, yTicks, xSteps = 4, xDigits = 0) {
    const canvas = $(id), bounds = canvas.getBoundingClientRect();
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(bounds.width * ratio); canvas.height = Math.round(bounds.height * ratio);
    const ctx = canvas.getContext('2d'); ctx.scale(ratio, ratio);
    const box = { left: 48, right: bounds.width - 12, top: 16, bottom: bounds.height - 25 };
    const x = t => box.left + (t - minX) / (maxX - minX) * (box.right - box.left);
    const y = value => box.bottom - (value - minY) / (maxY - minY) * (box.bottom - box.top);
    ctx.font = '10px Consolas, monospace'; ctx.lineWidth = 1;
    for (let i = 0; i <= xSteps; i++) {
      const t = minX + i * (maxX - minX) / xSteps;
      ctx.strokeStyle = colors.grid; ctx.beginPath(); ctx.moveTo(x(t), box.top); ctx.lineTo(x(t), box.bottom); ctx.stroke();
      ctx.fillStyle = colors.text; ctx.textAlign = 'center'; ctx.fillText(format(t, xDigits) + (i === 0 ? ' мс' : ''), x(t), bounds.height - 6);
    }
    for (const value of yTicks) {
      ctx.strokeStyle = colors.grid; ctx.beginPath(); ctx.moveTo(box.left, y(value)); ctx.lineTo(box.right, y(value)); ctx.stroke();
      ctx.fillStyle = colors.text; ctx.textAlign = 'right'; ctx.fillText(format(value, Math.abs(value) < 1 && value !== 0 ? 2 : 0), box.left - 6, y(value) + 3);
    }
    function stroke(points, color, dashed = false) {
      ctx.save(); ctx.beginPath(); ctx.rect(box.left, box.top, box.right - box.left, box.bottom - box.top); ctx.clip();
      ctx.strokeStyle = color; ctx.lineWidth = 1.25; ctx.setLineDash(dashed ? [4, 4] : []); ctx.beginPath();
      points.forEach(([t, value], i) => { if (i) ctx.lineTo(x(t), y(value)); else ctx.moveTo(x(t), y(value)); });
      ctx.stroke(); ctx.restore();
    }
    function cursor(t) {
      ctx.strokeStyle = colors.amber; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(x(t), box.top); ctx.lineTo(x(t), box.bottom); ctx.stroke();
    }
    return { ctx, box, x, y, stroke, cursor };
  }

  function drawCycle(current) {
    const { config: c, signal, shape } = model;
    const limit = Math.max(.1, c.amplitude);
    const p = plot('cycle', 0, 1000, -limit * 1.15, limit * 1.15, [-limit, 0, limit], 5);
    p.ctx.fillStyle = '#64e5c009'; p.ctx.fillRect(p.x(0), p.box.top, p.x(c.durationMs) - p.x(0), p.box.bottom - p.box.top);
    // Обзор не соединяет редкие отсчёты линией: это создало бы ложную низкую частоту.
    // Для каждой колонки пикселей показываем минимум и максимум исходных отсчётов.
    p.ctx.strokeStyle = '#64e5c0'; p.ctx.lineWidth = 1; p.ctx.beginPath();
    const columns = Math.max(1, Math.floor(p.box.right - p.box.left));
    for (let px = 0; px < columns; px++) {
      const begin = Math.floor(px * D.PERIOD / columns), end = Math.floor((px + 1) * D.PERIOD / columns);
      let minimum = Infinity, maximum = -Infinity;
      for (let n = begin; n < end; n++) { minimum = Math.min(minimum, signal[n]); maximum = Math.max(maximum, signal[n]); }
      const x = p.box.left + px + .5;
      p.ctx.moveTo(x, p.y(minimum)); p.ctx.lineTo(x, p.y(maximum));
    }
    p.ctx.stroke();
    const upper = [], lower = [];
    for (let n = 0; n < model.activeSamples; n += 24) {
      upper.push([n / 48, c.amplitude * shape[n]]); lower.push([n / 48, -c.amplitude * shape[n]]);
    }
    upper.push([c.durationMs, c.smooth ? 0 : c.amplitude]); lower.push([c.durationMs, c.smooth ? 0 : -c.amplitude]);
    p.stroke(upper, '#8599ab', true); p.stroke(lower, '#8599ab', true); p.cursor(current.time * 1000);
  }

  function drawFrequency(current) {
    const c = model.config;
    const p = plot('frequency', 0, 1000, Math.max(0, c.startHz - 200), c.startHz + 1200, [c.startHz, c.startHz + 500, c.startHz + 1000], 4);
    // Конец развёртки — математическая граница. Последний реальный отсчёт чуть раньше T.
    p.stroke([[0, c.startHz], [c.durationMs, c.startHz + 1000]], colors.purple);
    p.ctx.fillStyle = colors.text; p.ctx.textAlign = 'left'; p.ctx.fillText('Гц', 4, 10);
    p.cursor(current.time * 1000);
    if (current.active) {
      p.ctx.fillStyle = colors.amber; p.ctx.beginPath(); p.ctx.arc(p.x(current.time * 1000), p.y(current.frequency), 4, 0, 2 * Math.PI); p.ctx.fill();
    }
  }

  function drawPhase(current) {
    const canvas = $('phase'), bounds = canvas.getBoundingClientRect(), ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(bounds.width * ratio); canvas.height = Math.round(bounds.height * ratio);
    const ctx = canvas.getContext('2d'); ctx.scale(ratio, ratio);
    const cx = bounds.width / 2, cy = bounds.height / 2 - 3, radius = Math.min(bounds.width / 2 - 16, bounds.height / 2 - 22);
    ctx.strokeStyle = '#344253'; ctx.lineWidth = 1; ctx.beginPath(); ctx.arc(cx, cy, radius, 0, 2 * Math.PI); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(cx - radius - 6, cy); ctx.lineTo(cx + radius + 6, cy); ctx.moveTo(cx, cy - radius - 6); ctx.lineTo(cx, cy + radius + 6); ctx.stroke();
    ctx.fillStyle = colors.text; ctx.font = '10px Consolas, monospace'; ctx.textAlign = 'center'; ctx.fillText('sin φ', cx, cy - radius - 10);
    if (current.active) {
      const x = cx + radius * Math.cos(current.phase), y = cy - radius * Math.sin(current.phase);
      ctx.strokeStyle = colors.purple; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(x, y); ctx.stroke();
      ctx.setLineDash([3, 3]); ctx.strokeStyle = colors.teal; ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(cx, y); ctx.stroke(); ctx.setLineDash([]);
      ctx.fillStyle = colors.amber; ctx.beginPath(); ctx.arc(x, y, 4, 0, 2 * Math.PI); ctx.fill();
      text('phase-caption', 'φ mod 2π = ' + format(((current.phase % (2 * Math.PI)) * 180 / Math.PI), 1) + '°');
    } else text('phase-caption', 'Пауза · генератор выдаёт 0');
  }

  function drawDetail(current) {
    const windowSamples = +$('zoom').value * 48;
    const begin = Math.max(0, Math.min(D.PERIOD - windowSamples, position - Math.floor(windowSamples / 2)));
    const end = begin + windowSamples;
    const limit = Math.max(.1, model.config.amplitude);
    const p = plot('detail', begin / 48, end / 48, -limit * 1.15, limit * 1.15, [-limit, 0, limit], 4, 2);
    const points = [], upper = [], lower = [];
    for (let n = begin; n < end; n++) {
      points.push([n / 48, model.signal[n]]);
      upper.push([n / 48, model.config.amplitude * model.shape[n]]); lower.push([n / 48, -model.config.amplitude * model.shape[n]]);
    }
    p.stroke(upper, '#8293a6', true); p.stroke(lower, '#8293a6', true); p.stroke(points, colors.teal);
    p.cursor(current.time * 1000);
    p.ctx.fillStyle = colors.amber; p.ctx.beginPath(); p.ctx.arc(p.x(current.time * 1000), p.y(current.sample), 4, 0, 2 * Math.PI); p.ctx.fill();
  }

  function render() {
    if (!model) return;
    const current = D.at(position, model.config), c = model.config;
    drawCycle(current); drawFrequency(current); drawPhase(current); drawDetail(current);
    let state = !current.active ? 'Пауза' : !c.smooth ? 'Без сглаживания' : current.time < .01 ? 'Нарастание' : current.time > c.durationMs / 1000 - .01 ? 'Спад' : 'Полный уровень';
    text('state-title', state === 'Пауза' ? 'Возбуждение завершено — слушаем отклик' : state);
    text('state', current.active ? 'OUTPUT 2 · генерация' : 'OUTPUT 2 · нулевой выход');
    text('sample-index', format(position));
    text('current-time', format(current.time * 1000, 3) + ' мс');
    text('current-frequency', current.frequency === null ? '—' : format(current.frequency, 1) + ' Гц');
    text('current-envelope', format(current.envelope, 3));
    text('current-sample', format(current.sample, 4));
    text('sample-formula', current.active ? `s[${format(position)}] = ${format(c.amplitude, 2)} × ${format(current.envelope, 3)} × sin(${format(current.phase, 3)}) = ${format(current.sample, 4)}` : `s[${format(position)}] = 0 · пауза до следующего цикла`);
    text('phase-value', current.phase === null ? 'Фаза в паузе не вычисляется' : 'Накопленная фаза: ' + format(current.phase / (2 * Math.PI), 3) + ' оборота');
    $('position').value = position;
  }
  function requestDraw() { if (!drawRequest) drawRequest = requestAnimationFrame(() => { drawRequest = null; render(); }); }
  function stop() {
    running = false; lastTime = null;
    if (animation !== null) cancelAnimationFrame(animation);
    animation = null; text('play', '▶ Запустить цикл'); $('play').setAttribute('aria-pressed', 'false');
  }
  function setPosition(index) {
    position = Math.max(0, Math.min(D.PERIOD - 1, Math.floor(index)));
    fractionalPosition = position; requestDraw();
  }
  function tick(now) {
    if (!running) return;
    if (lastTime !== null) fractionalPosition += (now - lastTime) / 1000 * D.FS * +$('speed').value;
    lastTime = now;
    if (fractionalPosition >= D.PERIOD) {
      if (!$('loop').checked) { setPosition(D.PERIOD - 1); stop(); render(); return; }
      fractionalPosition %= D.PERIOD;
    }
    position = Math.floor(fractionalPosition); render(); animation = requestAnimationFrame(tick);
  }
  function play() {
    if (running) { stop(); return; }
    if (position >= D.PERIOD - 1) setPosition(0);
    running = true; fractionalPosition = position; lastTime = null;
    text('play', 'Ⅱ Пауза'); $('play').setAttribute('aria-pressed', 'true'); animation = requestAnimationFrame(tick);
  }
  function calculate() {
    stop(); position = 0; fractionalPosition = 0;
    model = D.generate({ startHz: +$('start').value, durationMs: +$('duration').value, amplitude: +$('amplitude').value, smooth: $('smooth').checked });
    const c = model.config;
    text('start-value', format(c.startHz) + ' Гц'); text('end-frequency', format(c.startHz + 1000) + ' Гц');
    text('duration-value', format(c.durationMs) + ' мс'); text('amplitude-value', format(c.amplitude, 2));
    text('plateau-value', c.smooth ? format(c.durationMs - 20) + ' мс' : 'Без огибающей');
    text('envelope-hint', c.smooth ? 'Между короткими переходами сохраняется полная амплитуда. При длительности 20 мс плоского участка нет.' : 'Учебное отключение: сигнал обрывается без сглаживания. В C++ края всегда сглажены.');
    $('excitation-segment').style.width = c.durationMs / 10 + '%';
    text('excitation-label', format(c.durationMs) + ' мс'); text('silence-label', format(model.silenceMs) + ' мс');
    text('active-frames', format(model.activeSamples) + ' возбуждение + ' + format(D.PERIOD - model.activeSamples) + ' тишина');
    requestDraw();
  }
  for (const id of ['start', 'duration', 'amplitude', 'smooth']) $(id).addEventListener('input', () => {
    if (!settingsRequest) settingsRequest = requestAnimationFrame(() => { settingsRequest = null; calculate(); });
  });
  $('position').addEventListener('input', () => { stop(); setPosition(+$('position').value); });
  $('zoom').addEventListener('change', requestDraw);
  $('speed').addEventListener('change', () => { lastTime = null; });
  $('play').addEventListener('click', play);
  $('next').addEventListener('click', () => { stop(); setPosition(position + 1); });
  $('previous').addEventListener('click', () => { stop(); setPosition(position - 1); });
  document.querySelectorAll('[data-jump]').forEach(button => button.addEventListener('click', () => {
    const t = { attack: 5, middle: model.config.durationMs / 2, release: model.config.durationMs - 5, silence: model.config.durationMs + 50 }[button.dataset.jump];
    stop(); setPosition(Math.round(t * 48));
  }));
  $('detail').addEventListener('keydown', event => {
    if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') { event.preventDefault(); stop(); setPosition(position + (event.key === 'ArrowLeft' ? -1 : 1)); }
  });
  document.addEventListener('keydown', event => {
    if (event.code === 'Space' && !['INPUT', 'SELECT', 'BUTTON', 'SUMMARY'].includes(event.target.tagName)) { event.preventDefault(); play(); }
  });
  $('reset').addEventListener('click', () => {
    for (const id of ['start', 'duration', 'amplitude']) $(id).value = $(id).defaultValue;
    $('smooth').checked = true; $('loop').checked = true; $('speed').value = '0.25'; $('zoom').value = '4'; calculate();
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

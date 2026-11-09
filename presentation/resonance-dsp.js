/* Одномодовый резонатор. Масштаб движения бокала на экране условный. */
(function (root) {
  'use strict';
  const M = typeof module !== 'undefined' && module.exports ? require('./microphone-dsp.js') : root.MicrophoneDSP;
  const FS = 48000, LENGTH = 96000;
  function simulate(options = {}) {
    const config = { frequency: 2450, tauMs: 180, mode: 'sweep', ...options };
    if (!Number.isFinite(config.frequency) || config.frequency < 2000 || config.frequency > 2800 || !Number.isFinite(config.tauMs) || config.tauMs < 60 || config.tauMs > 500 || !['sweep','matched','off'].includes(config.mode)) throw new RangeError('Некорректные параметры резонатора');
    const drive = new Float32Array(LENGTH);
    for (let n = 0; n < FS; n++) {
      const t = n / FS;
      const phase = config.mode === 'sweep' ? 2 * Math.PI * (1900 * t + 500 * t * t) : 2 * Math.PI * (config.frequency + (config.mode === 'off' ? 400 : 0)) * t;
      const shape = t < .01 ? .5 * (1 - Math.cos(Math.PI * t / .01)) : t > .99 ? .5 * (1 - Math.cos(Math.PI * (1 - t) / .01)) : 1;
      drive[n] = .15 * shape * Math.sin(phase);
    }
    const response = M.resonator(drive, config.frequency, config.tauMs, 1);
    const envelope = new Float32Array(LENGTH);
    // Симметричная локальная RMS-оценка, 10 мс: нужна только для удобного обзора.
    const prefix = new Float64Array(LENGTH + 1);
    for (let n = 0; n < LENGTH; n++) prefix[n + 1] = prefix[n] + response[n] * response[n];
    for (let n = 0; n < LENGTH; n++) {
      const a = Math.max(0, n - 240), b = Math.min(LENGTH, n + 240);
      envelope[n] = Math.sqrt(2 * (prefix[b] - prefix[a]) / (b - a));
    }
    return { config, drive, response, envelope, cutoff: FS, length: LENGTH };
  }
  function gain(frequency, natural, tauMs) {
    const r = Math.exp(-1 / (FS * tauMs / 1000)), omega = 2 * Math.PI * natural / FS, w = 2 * Math.PI * frequency / FS;
    const a = 2 * r * Math.cos(omega), b = r * r;
    const re = 1 - a * Math.cos(w) + b * Math.cos(2 * w), im = a * Math.sin(w) - b * Math.sin(2 * w);
    return (1 - r) * 2 * Math.sin(omega) / Math.hypot(re, im);
  }
  const api = { FS, LENGTH, simulate, gain };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.ResonanceDSP = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);

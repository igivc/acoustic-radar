/* Модель правого канала ChirpGenerator. Не обращается к аудиоустройствам. */
(function (root) {
  'use strict';
  const FS = 48000, PERIOD = 48000, FADE = .01;
  function settings(options = {}) {
    const result = { startHz: 1900, durationMs: 250, amplitude: .5, smooth: true, ...options };
    for (const key of ['startHz', 'durationMs', 'amplitude']) if (!Number.isFinite(result[key])) throw new RangeError('Параметры должны быть конечными');
    if (result.startHz < 100 || result.startHz > 19000 || result.durationMs < 20 || result.durationMs > 590 ||
        !Number.isInteger(result.durationMs) || result.amplitude < 0 || result.amplitude > 1) throw new RangeError('Параметры вне допустимого диапазона');
    return result;
  }
  // Огибающая совпадает с acousticEnvelope в C++: два полукосинуса, а не окно Ханна на весь chirp.
  function envelope(t, duration, smooth = true) {
    if (t < 0 || t >= duration) return 0;
    if (!smooth) return 1;
    if (t < FADE) return .5 * (1 - Math.cos(Math.PI * t / FADE));
    if (t > duration - FADE) return .5 * (1 - Math.cos(Math.PI * (duration - t) / FADE));
    return 1;
  }
  function at(index, config) {
    const local = ((Math.floor(index) % PERIOD) + PERIOD) % PERIOD;
    const t = local / FS, duration = config.durationMs / 1000;
    const active = local < config.durationMs * 48;
    const sweepRate = 1000 / duration;
    // После окончания возбуждения генератор возвращает ноль. Его фаза и частота
    // в паузе не являются параметрами реально излучаемого сигнала: показываем null.
    const phase = active ? 2 * Math.PI * (config.startHz * t + .5 * sweepRate * t * t) : null;
    const gain = envelope(t, duration, config.smooth);
    return { index: local, time: t, active, phase, frequency: active ? config.startHz + sweepRate * t : null,
      envelope: gain, sample: active ? Math.fround(config.amplitude * gain * Math.sin(phase)) : 0 };
  }
  function generate(options = {}) {
    const config = settings(options);
    const signal = new Float32Array(PERIOD);
    const shape = new Float32Array(PERIOD);
    for (let n = 0; n < PERIOD; n++) {
      const value = at(n, config);
      signal[n] = value.sample; shape[n] = value.envelope;
    }
    return { config, signal, shape, activeSamples: config.durationMs * 48, silenceMs: 1000 - config.durationMs };
  }
  const api = { FS, PERIOD, FADE, settings, envelope, at, generate };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.ExcitationDSP = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);

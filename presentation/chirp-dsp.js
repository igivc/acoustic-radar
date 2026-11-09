/* Численная модель демонстрации. Доступна как в обычном HTML без сервера, так и в Node.js. */
(function (root) {
  'use strict';
  const FS = 48000;
  const PERIOD = 48000;
  const FFT_SIZE = 4096;
  const HOP = 1024;

  // Формула совпадает с ChirpGenerator: интеграл линейной частоты и симметричное окно Ханна.
  function chirp({ f0 = 8000, f1 = 12000, durationMs = 10 } = {}) {
    const length = Math.round(durationMs * FS / 1000);
    if (length < 2 || f0 < 0 || f1 >= FS / 2 || f1 < f0) throw new RangeError('Некорректные параметры chirp');
    const duration = length / FS;
    const reference = new Float32Array(length);
    const envelope = new Float32Array(length);
    for (let j = 0; j < length; j++) {
      const t = j / FS;
      envelope[j] = .5 * (1 - Math.cos(2 * Math.PI * j / (length - 1)));
      reference[j] = .1 * envelope[j] * Math.sin(2 * Math.PI * (f0 * t + (f1 - f0) * t * t / (2 * duration)));
    }
    return { reference, envelope, duration };
  }

  // Повторяемый шум: перемещение курсора не должно случайно менять входной сигнал.
  function normalNoise(length, seed) {
    let state = seed >>> 0 || 1;
    function random() {
      state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
      return (state + .5) / 4294967296;
    }
    const result = new Float32Array(length);
    for (let i = 0; i < length; i += 2) {
      const radius = Math.sqrt(-2 * Math.log(random()));
      const angle = 2 * Math.PI * random();
      result[i] = radius * Math.cos(angle);
      if (i + 1 < length) result[i + 1] = radius * Math.sin(angle);
    }
    return result;
  }

  // Как в findPeak: нормированное скалярное произведение, без вычитания среднего и без abs(r).
  function correlate(input, reference) {
    const count = input.length - reference.length + 1;
    if (count <= 0) throw new RangeError('Эталон длиннее входа');
    const values = new Float64Array(count);
    let referenceEnergy = 0;
    for (const sample of reference) referenceEnergy += sample * sample;
    let best = { position: 0, correlation: -Infinity };
    for (let position = 0; position < count; position++) {
      let dot = 0, energy = 0;
      for (let j = 0; j < reference.length; j++) {
        const sample = input[position + j];
        dot += sample * reference[j];
        energy += sample * sample;
      }
      // NaN в массиве оценок означает пропущенное окно, а не корреляцию 0.
      const correlation = energy < 1e-12 || !referenceEnergy ? NaN : dot / Math.sqrt(energy * referenceEnergy);
      values[position] = correlation;
      if (correlation > best.correlation) best = { position, correlation };
    }
    return { values, best };
  }

  function simulate(options = {}) {
    const settings = { f0: 8000, f1: 12000, durationMs: 10, delayMs: 1253 / 48,
      gain: .5, noise: .011, threshold: .75, marker: true, seed: 20261108, ...options };
    const { reference, envelope, duration } = chirp(settings);
    const input = normalNoise(Math.round(.08 * FS), settings.seed);
    const delay = Math.round(settings.delayMs * FS / 1000);
    if (delay < 0 || delay + reference.length > input.length) throw new RangeError('Маркер не помещается во входе');
    for (let i = 0; i < input.length; i++) input[i] *= settings.noise;
    if (settings.marker) for (let j = 0; j < reference.length; j++) input[delay + j] += settings.gain * reference[j];
    const { values, best } = correlate(input, reference);
    const accepted = Number.isFinite(best.correlation) && best.correlation >= settings.threshold;
    return { settings, reference, envelope, duration, input, delay, values, best, accepted };
  }
  function frameCoordinates(base, localMarker) {
    const marker = base + localMarker;
    return { marker, end: marker + PERIOD, firstCenter: marker + FFT_SIZE / 2,
      firstTimeMs: FFT_SIZE / 2 / FS * 1000, columns: Math.floor((PERIOD - FFT_SIZE) / HOP) + 1 };
  }
  const api = { FS, PERIOD, FFT_SIZE, HOP, chirp, correlate, simulate, frameCoordinates };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.ChirpDSP = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);

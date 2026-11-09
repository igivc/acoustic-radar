/* Учебная модель INPUT 2 и STFT. Реальный микрофон и FFTW не используются. */
(function (root) {
  'use strict';
  const E = typeof module !== 'undefined' && module.exports ? require('./excitation-dsp.js') : root.ExcitationDSP;
  const FS = 48000, SIZE = 4096, HOP = 1024, PERIOD = 48000, MARKER = 1253;
  function normalNoise(length, seed) {
    let state = seed >>> 0 || 1;
    const random = () => { state = (Math.imul(state, 1664525) + 1013904223) >>> 0; return (state + .5) / 4294967296; };
    const result = new Float32Array(length);
    for (let n = 0; n < length; n += 2) {
      const radius = Math.sqrt(-2 * Math.log(random())), angle = 2 * Math.PI * random();
      result[n] = radius * Math.cos(angle);
      if (n + 1 < length) result[n + 1] = radius * Math.sin(angle);
    }
    return result;
  }
  // Два полюса r*exp(±iω): без возбуждения амплитуда затухает примерно как exp(-t/τ).
  // Это один идеализированный резонанс, а не измеренная модель конкретного бокала.
  function resonator(input, frequency, tauMs, gain) {
    const radius = Math.exp(-1 / (FS * tauMs / 1000)), omega = 2 * Math.PI * frequency / FS;
    const feedback = 2 * radius * Math.cos(omega), radiusSquared = radius * radius;
    const feed = gain * (1 - radius) * 2 * Math.sin(omega);
    const output = new Float32Array(input.length);
    let previous = 0, older = 0;
    for (let n = 0; n < input.length; n++) {
      const sample = feedback * previous - radiusSquared * older + feed * input[n];
      output[n] = sample; older = previous; previous = sample;
    }
    return output;
  }
  function fft(real, imaginary) {
    const length = real.length;
    if (length < 2 || (length & (length - 1)) || imaginary.length !== length) throw new RangeError('FFT требует размер — степень двойки');
    // Сначала переставляем индексы по обращённым битам, затем объединяем блоки 2,4,8,... .
    for (let i = 1, reversed = 0; i < length; i++) {
      let bit = length >> 1;
      for (; reversed & bit; bit >>= 1) reversed ^= bit;
      reversed ^= bit;
      if (i < reversed) {
        [real[i], real[reversed]] = [real[reversed], real[i]];
        [imaginary[i], imaginary[reversed]] = [imaginary[reversed], imaginary[i]];
      }
    }
    for (let size = 2; size <= length; size <<= 1) {
      const angle = -2 * Math.PI / size;
      const stepReal = Math.cos(angle), stepImaginary = Math.sin(angle);
      for (let start = 0; start < length; start += size) {
        let wr = 1, wi = 0;
        for (let j = 0; j < size / 2; j++) {
          const even = start + j, odd = even + size / 2;
          const tr = wr * real[odd] - wi * imaginary[odd], ti = wr * imaginary[odd] + wi * real[odd];
          real[odd] = real[even] - tr; imaginary[odd] = imaginary[even] - ti;
          real[even] += tr; imaginary[even] += ti;
          const next = wr * stepReal - wi * stepImaginary;
          wi = wr * stepImaginary + wi * stepReal; wr = next;
        }
      }
    }
  }
  const windows = new Map();
  function hann(size) {
    if (!Number.isInteger(size) || size < 4 || size > 16384 || (size & (size - 1))) throw new RangeError('Размер FFT должен быть степенью двойки от 4 до 16384');
    if (!windows.has(size)) windows.set(size, Float64Array.from({ length: size }, (_, j) => .5 * (1 - Math.cos(2 * Math.PI * j / (size - 1)))));
    return windows.get(size);
  }
  const window = hann(SIZE);
  function spectrum(input, start = 0, size = SIZE) {
    const shape = hann(size), windowSum = shape.reduce((sum, value) => sum + value, 0);
    if (start < 0 || !Number.isInteger(start) || start + size > input.length) throw new RangeError('Неполное окно FFT');
    const real = new Float64Array(size), imaginary = new Float64Array(size);
    for (let j = 0; j < size; j++) {
      if (!Number.isFinite(input[start + j])) throw new RangeError('Неконечный отсчёт');
      real[j] = input[start + j] * shape[j];
    }
    fft(real, imaginary);
    return Float32Array.from({ length: size / 2 + 1 }, (_, bin) => {
      const scale = bin === 0 || bin === size / 2 ? 1 : 2;
      return 20 * Math.log10(Math.max(1e-10, scale * Math.hypot(real[bin], imaginary[bin]) / windowSum));
    });
  }
  function analyze(buffer, marker = MARKER) {
    if (!Number.isInteger(marker) || marker < 0 || marker + PERIOD > buffer.length) throw new RangeError('Неполный спектральный кадр');
    const columns = [];
    for (let offset = 0; offset + SIZE <= PERIOD; offset += HOP) {
      const levels = spectrum(buffer, marker + offset);
      let peakBin = 163;
      for (let b = 164; b <= 247; b++) if (levels[b] > levels[peakBin]) peakBin = b;
      columns.push({ offset, start: marker + offset, end: marker + offset + SIZE,
        centerMs: (offset + SIZE / 2) / FS * 1000, levels, peakBin });
    }
    return columns;
  }
  function simulate(options = {}) {
    const config = { frequency: 2450, tauMs: 180, resonanceGain: 8, directGain: .25, delayMs: 4, noise: .001, seed: 20261108, ...options };
    for (const key of ['frequency', 'tauMs', 'resonanceGain', 'directGain', 'delayMs', 'noise', 'seed']) if (!Number.isFinite(config[key])) throw new RangeError('Параметр должен быть конечным');
    if (config.frequency < 1900 || config.frequency > 2900 || config.tauMs <= 0 || config.tauMs > 1000 || config.directGain < 0 || config.resonanceGain < 0 || config.delayMs < 0 || config.delayMs > 50 || config.noise < 0) throw new RangeError('Параметры вне диапазона');
    const excitation = E.generate(), delaySamples = Math.round(config.delayMs * 48);
    const delayed = new Float32Array(PERIOD), direct = new Float32Array(PERIOD);
    for (let n = delaySamples; n < PERIOD; n++) {
      delayed[n] = excitation.signal[n - delaySamples]; direct[n] = delayed[n] * config.directGain;
    }
    const ringing = resonator(delayed, config.frequency, config.tauMs, config.resonanceGain);
    const noise = normalNoise(MARKER + PERIOD, config.seed), buffer = new Float32Array(noise.length);
    for (let n = 0; n < buffer.length; n++) buffer[n] = noise[n] * config.noise;
    for (let n = 0; n < PERIOD; n++) buffer[MARKER + n] += direct[n] + ringing[n];
    const input = buffer.subarray(MARKER);
    return { config, excitation, delaySamples, direct, ringing, input, buffer, marker: MARKER,
      excitationEndMs: 250 + delaySamples / 48, columns: analyze(buffer) };
  }
  const api = { FS, SIZE, HOP, PERIOD, MARKER, window, hann, fft, spectrum, resonator, analyze, simulate };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.MicrophoneDSP = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);

// Реальная FFT проверяется на известных сигналах, модель отклика — отдельно от рисования.
const assert = require('node:assert/strict');
const D = require('../microphone-dsp.js');
const close = (actual, expected, tolerance = 1e-6) => assert.ok(Math.abs(actual - expected) < tolerance, `${actual} != ${expected}`);
// Сверка быстрой FFT с прямой суммой DFT на небольшом комплексном входе.
const re = Float64Array.from([1, 2, -1, 0, .5, 4, 0, -2]);
const im = Float64Array.from([0, 1, 0, -.5, 0, 0, 0, 1]);
const originalRe = re.slice(), originalIm = im.slice();
D.fft(re, im);
for (let b = 0; b < 8; b++) {
  let expectedRe = 0, expectedIm = 0;
  for (let j = 0; j < 8; j++) {
    const angle = -2 * Math.PI * b * j / 8;
    expectedRe += originalRe[j] * Math.cos(angle) - originalIm[j] * Math.sin(angle);
    expectedIm += originalRe[j] * Math.sin(angle) + originalIm[j] * Math.cos(angle);
  }
  close(re[b], expectedRe, 1e-10); close(im[b], expectedIm, 1e-10);
}
const tone = Float32Array.from({ length: D.SIZE }, (_, n) => .5 * Math.sin(2 * Math.PI * 200 * n / D.SIZE));
const spectrum = D.spectrum(tone);
assert.equal(spectrum.indexOf(Math.max(...spectrum)), 200);
close(spectrum[200], -6.0206, .001);
close(D.spectrum(new Float32Array(D.SIZE).fill(.25))[0], 20 * Math.log10(.25), .001);
const nyquist = Float32Array.from({ length: D.SIZE }, (_, n) => n % 2 ? -.25 : .25);
close(D.spectrum(nyquist)[D.SIZE / 2], 20 * Math.log10(.25), .001);
assert.ok(D.spectrum(new Float32Array(D.SIZE)).every(value => value === -200));
const model = D.simulate({ noise: 0 });
assert.equal(model.columns.length, 43);
assert.equal(model.marker, 1253);
assert.equal(model.columns[0].start, 1253);
assert.equal(model.columns[0].end, 5349);
close(model.columns[0].centerMs, 42.666666666666664);
assert.equal(model.columns[42].offset, 43008);
assert.equal(model.columns[42].end, 48357);
assert.equal(model.input.length, 48000);
assert.deepEqual(model.buffer.subarray(model.marker), model.input);
assert.ok(model.direct.subarray(0, model.delaySamples).every(value => value === 0));
assert.ok(model.direct.subarray(12000 + model.delaySamples).every(value => value === 0));
assert.ok(model.ringing.subarray(12000 + model.delaySamples).some(value => Math.abs(value) > .001));
assert.ok(model.input.every(Number.isFinite));
for (const frequency of [2250, 2450, 2700]) {
  const m = D.simulate({ frequency, noise: 0 });
  const column = m.columns[15];
  close(column.peakBin * D.FS / D.SIZE, frequency, D.FS / D.SIZE);
}
const directOnly = D.simulate({ resonanceGain: 0, noise: 0 });
assert.ok(directOnly.columns[15].levels.every(value => value === -200));
const silence = D.simulate({ resonanceGain: 0, directGain: 0, noise: 0 });
assert.ok(silence.input.every(value => value === 0));
assert.ok(silence.columns.every(c => c.levels.every(value => value === -200)));
// RMS свободного отклика на двух целых периодах, разделённых ровно на τ.
const impulse = new Float32Array(48000); impulse[0] = 1;
const response = D.resonator(impulse, 2400, 180, 8);
const rms = start => Math.sqrt(response.subarray(start, start + 20).reduce((sum, n) => sum + n * n, 0) / 20);
close(rms(13440) / rms(4800), Math.exp(-1), 1e-6);
assert.deepEqual(D.simulate().input, D.simulate().input);
assert.notDeepEqual(D.simulate().input, D.simulate({ seed: 42 }).input);
assert.throws(() => D.spectrum(Float32Array.from({ length: D.SIZE }, () => NaN)), RangeError);
assert.throws(() => D.analyze(new Float32Array(48000)), RangeError);
assert.throws(() => D.simulate({ tauMs: 0 }), RangeError);
console.log('INPUT 2: FFT/DFT, dBFS, DC/Найквист, 43 окна, индексы, задержка, резонанс, затухание и тишина — OK');

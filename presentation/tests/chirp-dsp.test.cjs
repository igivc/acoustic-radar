// Проверки модели выполняются без браузера и без звукового оборудования.
const assert = require('node:assert/strict');
const D = require('../chirp-dsp.js');
const close = (actual, expected, tolerance = 1e-6) => assert.ok(Math.abs(actual - expected) < tolerance, `${actual} != ${expected}`);
const reference = D.chirp();
assert.equal(reference.reference.length, 480);
close(reference.envelope[0], 0);
close(reference.envelope[479], 0);
// Независимый расчёт по формуле C++ ChirpGenerator, без промежуточного округления окна в Float32.
for (let j = 0; j < 480; j++) {
  const t = j / 48000;
  const expected = .1 * .5 * (1 - Math.cos(2 * Math.PI * j / 479)) * Math.sin(2 * Math.PI * (8000 * t + .5 * 400000 * t * t));
  close(reference.reference[j], expected, 1e-8);
}
for (const delaySamples of [0, 1, 1253, 2880]) {
  for (const gain of [.1, .5, 1.5]) {
    const model = D.simulate({ delayMs: delaySamples / 48, gain, noise: 0 });
    assert.equal(model.best.position, delaySamples);
    close(model.best.correlation, 1);
    assert.equal(model.accepted, true);
  }
}
const normal = D.simulate();
assert.equal(normal.best.position, 1253);
assert.equal(normal.accepted, true);
const missing = D.simulate({ marker: false });
assert.equal(missing.accepted, false);
assert.equal(D.simulate({ noise: .06 }).accepted, false);
assert.equal(D.simulate({ threshold: .99 }).accepted, false);
const silence = D.simulate({ gain: 0, noise: 0 });
assert.equal(silence.best.correlation, -Infinity);
assert.equal(silence.accepted, false);
assert.ok(silence.values.every(Number.isNaN));
assert.deepEqual(normal.input, D.simulate().input);
assert.notDeepEqual(normal.input, D.simulate({ seed: 42 }).input);
const boundary = D.simulate({ durationMs: 20, delayMs: 60, noise: 0 });
assert.equal(boundary.best.position, 2880);
assert.equal(boundary.values.length, 2881);
// Порог включителен, как в C++: r == threshold принимается.
assert.equal(D.simulate({ threshold: normal.best.correlation }).accepted, true);
assert.deepEqual(D.frameCoordinates(100000, 1253), {
  marker: 101253, end: 149253, firstCenter: 103301, firstTimeMs: 42.666666666666664, columns: 43
});
assert.throws(() => D.simulate({ delayMs: 80 }), RangeError);
assert.throws(() => D.chirp({ f0: 13000, f1: 12000 }), RangeError);
console.log('Chirp: соответствие формуле C++; корреляция, задержка, усиление, шум, порог, тишина и индексы — OK');

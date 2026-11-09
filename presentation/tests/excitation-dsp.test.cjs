// Формула правого канала проверяется отдельно от рисования и анимации.
const assert = require('node:assert/strict');
const D = require('../excitation-dsp.js');
const close = (actual, expected, tolerance = 1e-7) => assert.ok(Math.abs(actual - expected) <= tolerance, `${actual} != ${expected}`);
function reference(n, startHz, durationMs) {
  const local = n % 48000, duration = durationMs / 1000, t = local / 48000;
  if (local >= durationMs * 48) return 0;
  let envelope = 1;
  if (t < .010) envelope = .5 * (1 - Math.cos(Math.PI * t / .010));
  else if (t > duration - .010) envelope = .5 * (1 - Math.cos(Math.PI * (duration - t) / .010));
  const phase = 2 * Math.PI * (startHz * t + .5 * (1000 / duration) * t * t);
  return Math.fround(.5 * envelope * Math.sin(phase));
}
for (const durationMs of [20, 250, 590]) {
  for (const startHz of [100, 1900, 19000]) {
    const model = D.generate({ durationMs, startHz });
    assert.equal(model.signal.length, 48000);
    assert.equal(model.activeSamples, durationMs * 48);
    assert.equal(model.silenceMs, 1000 - durationMs);
    // Все 48000 отсчётов совпадают с независимой записью формулы chirp.cpp.
    for (let n = 0; n < 48000; n++) assert.equal(model.signal[n], reference(n, startHz, durationMs));
    for (const n of [0, 1, 240, 479, 480, model.activeSamples - 480, model.activeSamples - 1]) {
      const current = D.at(n, model.config);
      close(current.frequency, startHz + 1000 * n / model.activeSamples, 1e-10);
      assert.ok(current.envelope >= 0 && current.envelope <= 1);
      assert.ok(Math.abs(current.sample) <= .5);
    }
    for (const n of [model.activeSamples, 47999]) {
      const current = D.at(n, model.config);
      assert.equal(current.active, false); assert.equal(current.sample, 0);
      assert.equal(current.frequency, null); assert.equal(current.phase, null);
    }
    assert.deepEqual(D.at(48000, model.config), D.at(0, model.config));
  }
}
const config = D.settings();
close(D.at(240, config).envelope, .5);
close(D.at(6000, config).frequency, 2400);
close(D.at(11760, config).envelope, .5);
assert.equal(D.at(480, config).envelope, 1);
assert.equal(D.at(11520, config).envelope, 1);
assert.equal(D.at(240, D.settings({ smooth: false })).envelope, 1);
const low = D.at(1024, D.settings({ amplitude: .25 }));
const high = D.at(1024, D.settings({ amplitude: .5 }));
close(high.sample, low.sample * 2);
assert.ok(D.generate({ amplitude: 0 }).signal.every(n => n === 0));
for (const options of [{ durationMs: 10 }, { startHz: 20000 }, { amplitude: NaN }, { durationMs: Infinity }]) assert.throws(() => D.generate(options), RangeError);
console.log('Возбуждение: формула C++, 20/250/590 мс, огибающая, частота, амплитуда, пауза и новый цикл — OK');

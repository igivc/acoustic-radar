/* Дискретная учебная модель архитектуры: без потоков ОС, аудиоданных и реальной корреляции. */
(function (root) {
  'use strict';
  const FS = 48000, BLOCK = 512, CAPACITY = 256, PERIOD = 48000, FIRST = 1253;
  class Pipeline {
    constructor() {
      this.produced = 0; this.write = 0; this.read = 0; this.queue = []; this.ready = []; this.events = [];
      this.dropped = 0; this.uiDropped = 0; this.gaps = 0; this.cycles = 0; this.displayed = 0;
      this.workerRate = 1.25; this.uiPaused = false; this.budget = 0; this.next = null; this.base = 0; this.count = 0; this.pending = null; this.lastUI = 0; this.lastCycle = null; this.lastDisplay = null;
    }
    log(message) { this.events.unshift({ time: this.produced / FS, message }); this.events.length = Math.min(this.events.length, 8); }
    accept(index) {
      if (this.next !== null && index !== this.next) {
        this.gaps++; this.log(`Пропуск ${index - this.next} кадров: сбрасываем незавершённый цикл`);
        this.count = 0; this.pending = null; this.base = index;
      } else if (this.next === null) this.base = index;
      this.next = index + 1; this.count++;
      if (this.count < PERIOD + 480 || this.count % BLOCK) return;
      if (this.pending === null) {
        const absolute = FIRST + Math.max(0, Math.ceil((this.base - FIRST) / PERIOD)) * PERIOD;
        const local = absolute - this.base;
        if (local >= 0 && local < PERIOD && local + 480 <= this.count) {
          this.pending = local; this.log(`Маркер: base ${this.base} + local ${local} = ${absolute}`);
        }
      }
      let discard = 0;
      if (this.pending === null) discard = PERIOD;
      else if (this.pending + PERIOD <= this.count) {
        const marker = this.base + this.pending;
        const result = { id: ++this.cycles, marker, end: marker + PERIOD };
        this.lastCycle = { ...result, base: this.base, local: this.pending };
        if (this.ready.length === 8) { this.ready.shift(); this.uiDropped++; this.log('SDL отстаёт: удаляем самый старый готовый спектр'); }
        this.ready.push(result); this.log(`Цикл №${result.id} готов: [${marker}, ${marker + PERIOD}) → FFT`);
        discard = this.pending + PERIOD - BLOCK; this.pending = null;
      }
      this.count -= discard; this.base += discard;
    }
    step() {
      const start = this.produced; this.produced += BLOCK;
      if (this.queue.length < CAPACITY) { this.queue.push(start); this.write++; }
      else { if (!this.dropped) this.log('Кольцо заполнено: callback отбрасывает новые кадры, звук продолжается'); this.dropped += BLOCK; }
      this.budget = this.workerRate === 0 ? 0 : Math.min(CAPACITY, this.budget + this.workerRate);
      while (this.budget >= 1 && this.queue.length) {
        const first = this.queue.shift(); this.read++; this.budget--;
        for (let j = 0; j < BLOCK; j++) this.accept(first + j);
      }
      if (!this.queue.length) this.budget = Math.min(this.budget, 1);
      if (!this.uiPaused && this.produced - this.lastUI >= 4800) {
        while (this.ready.length) { this.lastDisplay = this.ready.shift(); this.displayed++; }
        this.lastUI = this.produced;
      }
      return this;
    }
    advance(steps) { for (let n = 0; n < steps; n++) this.step(); return this; }
    preset(name) {
      if (name === 'overflow') { this.workerRate = 0; this.advance(300); }
      else if (name === 'slow-ui') { this.uiPaused = true; this.advance(1200); }
      return this;
    }
  }
  const api = { FS, BLOCK, CAPACITY, PERIOD, FIRST, Pipeline };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.PipelineDSP = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);

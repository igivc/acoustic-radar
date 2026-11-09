// Проверки моделей дополнительных опытов без браузера и звукового оборудования.
const assert=require('node:assert/strict');
const R=require('../resonance-dsp.js'),P=require('../pipeline-dsp.js'),F=require('../fft-comparison-dsp.js'),M=require('../microphone-dsp.js');
const close=(a,b,tolerance=1e-6)=>assert.ok(Math.abs(a-b)<tolerance,`${a} != ${b}`);
const matched=R.simulate({mode:'matched'}),off=R.simulate({mode:'off'});
assert.ok(matched.response.every(Number.isFinite));
assert.ok(matched.drive.subarray(48000).every(v=>v===0));
assert.ok(matched.envelope[52800]>.01);
assert.ok(matched.envelope[45000]>100*off.envelope[45000]);
close(matched.envelope[61440]/matched.envelope[52800],Math.exp(-1),1e-4);
assert.ok(R.gain(2450,2450,180)>100*R.gain(2850,2450,180));
assert.throws(()=>R.simulate({tauMs:0}),RangeError);
let pipe=new P.Pipeline();
for(let i=0;i<1500;i++){
  pipe.step();assert.equal(pipe.write-pipe.read,pipe.queue.length);assert.equal(pipe.produced,pipe.write*512+pipe.dropped);assert.ok(pipe.ready.length<=8);
}
assert.equal(pipe.dropped,0);assert.equal(pipe.gaps,0);assert.ok(pipe.cycles>=14);
assert.equal((pipe.lastCycle.marker-1253)%48000,0);assert.equal(pipe.lastCycle.base+pipe.lastCycle.local,pipe.lastCycle.marker);
pipe=new P.Pipeline().preset('overflow');assert.equal(pipe.queue.length,256);assert.ok(pipe.dropped>0);assert.equal(pipe.gaps,0);
const losses=pipe.dropped;pipe.workerRate=3;pipe.advance(600);assert.equal(pipe.gaps,1);assert.ok(pipe.cycles>0);assert.equal(pipe.queue.length,0);assert.ok(pipe.dropped>=losses);
pipe=new P.Pipeline().preset('slow-ui');assert.equal(pipe.ready.length,8);assert.ok(pipe.uiDropped>0);assert.equal(pipe.dropped,0);assert.equal(pipe.gaps,0);
assert.ok(pipe.ready.every((frame,i,array)=>!i||frame.id>array[i-1].id));pipe.uiPaused=false;pipe.advance(20);assert.ok(pipe.displayed>0);assert.equal(pipe.ready.length,0);
for(const size of [512,1024,2048,4096,8192]){
 const tone=Float32Array.from({length:size},(_,j)=>.5*Math.sin(2*Math.PI*50*j/size));const spectrum=M.spectrum(tone,0,size);assert.equal(spectrum.length,size/2+1);close(spectrum[50],-6.0206,.001);
}
const comparison=F.simulate(),spectra=F.inspect(comparison,500);
function peaks(item){const result=[];for(let b=Math.ceil(2300*item.size/48000);b<=Math.floor(2550*item.size/48000);b++)if(item.levels[b]>-25&&item.levels[b]>item.levels[b-1]&&item.levels[b]>item.levels[b+1])result.push(b*48000/item.size);return result;}
assert.equal(peaks(spectra[0]).length,1);assert.equal(peaks(spectra[1]).length,2);
const burst=F.simulate({mode:'burst'});
function span(curve){const max=Math.max(...curve.points.map(p=>p[1]));const active=curve.points.filter(p=>p[1]>max*.1);return active.at(-1)[0]-active[0][0];}
assert.ok(span(burst.curves[1])>span(burst.curves[0])+40);
assert.ok(burst.signal.subarray(0,490*48).every(v=>v===0));assert.ok(burst.signal.subarray(510*48).every(v=>v===0));
assert.equal(spectra[0].start+spectra[0].size/2,spectra[1].start+spectra[1].size/2);
assert.throws(()=>F.inspect(comparison,0),RangeError);assert.throws(()=>M.spectrum(new Float32Array(2),0,2),RangeError);
console.log('Резонанс, затухание, две перегрузки, восстановление, учёт индексов, размеры FFT и временное размытие — OK');

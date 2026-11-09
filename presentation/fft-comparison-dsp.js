/* Одинаковый тестовый сигнал для двух окон. Размер FFT не меняет исходные данные. */
(function(root){
  'use strict';
  const M=typeof module!=='undefined'&&module.exports?require('./microphone-dsp.js'):root.MicrophoneDSP;
  const FS=48000;
  function simulate(options={}){
    const config={mode:'tones',gap:40,shortSize:1024,longSize:8192,...options};
    if(!['tones','burst'].includes(config.mode)||!Number.isFinite(config.gap)||config.gap<20||config.gap>100||![512,1024,2048].includes(config.shortSize)||![4096,8192].includes(config.longSize))throw new RangeError('Некорректные параметры сравнения');
    const duration=config.mode==='tones'?200:20, onset=500-duration/2;
    const signal=new Float32Array(FS);
    for(let n=Math.round(onset*48);n<Math.round((onset+duration)*48);n++){
      const t=n/FS,local=n/48-onset;
      const envelope=local<1?.5*(1-Math.cos(Math.PI*local)):local>duration-1?.5*(1-Math.cos(Math.PI*(duration-local))):1;
      signal[n]=envelope*(.25*Math.sin(2*Math.PI*2400*t)+(config.mode==='tones'?.25*Math.sin(2*Math.PI*(2400+config.gap)*t):0));
    }
    const curves=[];
    // RMS после взвешивания: одинаковая сетка центров для обоих окон.
    for(const size of [config.shortSize,config.longSize]){
      const window=M.hann(size),norm=window.reduce((sum,v)=>sum+v*v,0),points=[];
      for(let center=300*48;center<=700*48;center+=240){let sum=0;const start=center-size/2;for(let j=0;j<size;j++){const v=signal[start+j]*window[j];sum+=v*v;}points.push([center/48,Math.sqrt(sum/norm)]);}
      curves.push({size,points});
    }
    return {config,signal,onset,end:onset+duration,duration,curves};
  }
  function inspect(model,centerMs){
    if(!Number.isFinite(centerMs)||centerMs<300||centerMs>700)throw new RangeError('Центр должен быть в диапазоне 300–700 мс');
    const center=Math.round(centerMs*48);
    return [model.config.shortSize,model.config.longSize].map(size=>({size,start:center-size/2,end:center+size/2,levels:M.spectrum(model.signal,center-size/2,size)}));
  }
  const api={FS,simulate,inspect};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.FFTComparisonDSP=api;
})(typeof globalThis!=='undefined'?globalThis:this);

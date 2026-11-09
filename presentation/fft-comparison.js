/* Дополнение страницы INPUT 2; основной спектральный анализ остаётся 4096/1024. */
(() => {
  'use strict';
  const $=id=>document.getElementById(id),U=window.TeachingUI,D=window.FFTComparisonDSP;
  let model,center=500,playing=false,raf=null,last=null,drawId=null;
  function draw(){
    if(!model)return;
    const spectra=D.inspect(model,center),colors=[U.colors.teal,U.colors.purple];
    const p=U.plot('fft-time',300,700,0,.4,[0,.2,.4]);
    p.ctx.fillStyle='#ffffff09';p.ctx.fillRect(p.x(model.onset),p.box.top,p.x(model.end)-p.x(model.onset),p.box.bottom-p.box.top);
    model.curves.forEach((curve,i)=>p.trace(curve.points,colors[i]));p.vertical(center);
    spectra.forEach((item,i)=>{
      const q=U.plot(i?'fft-long-spectrum':'fft-short-spectrum',2250,2600,-90,0,[-80,-40,0],'Гц',4,1);
      const points=[];for(let b=Math.ceil(2250*item.size/D.FS);b<=Math.floor(2600*item.size/D.FS);b++)points.push([b*D.FS/item.size,item.levels[b]]);
      q.trace(points,colors[i]);q.vertical(2400,'#9badbf',true);if(model.config.mode==='tones')q.vertical(2400+model.config.gap,'#9badbf',true);
      U.text(i?'fft-long-info':'fft-short-info',`${item.size} отсч. · ${U.format(item.size/48,2)} мс · Δf ${U.format(D.FS/item.size,2)} Гц`);
      U.text(i?'fft-long-interval':'fft-short-interval',`Окно [${U.format(item.start/48,2)}, ${U.format(item.end/48,2)}) мс`);
    });
    U.text('fft-center-value',U.format(center,1)+' мс');$('fft-center').value=center;
    U.text('fft-comparison-note',model.config.mode==='tones'?`Один сигнал: 2400 и ${2400+model.config.gap} Гц, от ${model.onset} до ${model.end} мс. При центре 500 мс сравните число различимых пиков.`:`Один тон 2400 Гц длительностью 20 мс. Границы события: 490–510 мс; длинное окно начинает реагировать раньше и заканчивает позже.`);
  }
  function requestDraw(){if(!drawId)drawId=requestAnimationFrame(()=>{drawId=null;draw();});}
  function stop(){playing=false;if(raf!==null)cancelAnimationFrame(raf);raf=null;last=null;U.text('fft-play','▶ Провести окно');$('fft-play').setAttribute('aria-pressed','false');}
  function tick(now){if(!playing)return;if(last!==null)center+=(now-last)*.05;last=now;if(center>=700){center=700;stop();draw();return;}draw();raf=requestAnimationFrame(tick);}
  function toggle(){if(playing){stop();return;}if(center>=699)center=300;playing=true;last=null;U.text('fft-play','Ⅱ Пауза');$('fft-play').setAttribute('aria-pressed','true');raf=requestAnimationFrame(tick);}
  function calculate(){stop();model=D.simulate({mode:$('fft-case').value,gap:+$('fft-gap').value,shortSize:+$('fft-short').value,longSize:+$('fft-long').value});U.text('fft-gap-value',U.format(model.config.gap)+' Гц');$('fft-gap').disabled=model.config.mode==='burst';requestDraw();}
  ['fft-case','fft-gap','fft-short','fft-long'].forEach(id=>$(id).addEventListener('input',calculate));
  $('fft-center').addEventListener('input',()=>{stop();center=+$('fft-center').value;requestDraw();});
  $('fft-play').addEventListener('click',toggle);$('fft-middle').addEventListener('click',()=>{stop();center=500;requestDraw();});
  $('fft-start').addEventListener('click',()=>{stop();center=400;requestDraw();});
  new ResizeObserver(requestDraw).observe($('fft-comparison'));document.addEventListener('visibilitychange',()=>{last=null;});
  calculate();
})();

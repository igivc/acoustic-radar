(() => {
  'use strict';
  const $=id=>document.getElementById(id),D=window.ResonanceDSP,U=window.TeachingUI;
  let model,time=0,playing=false,raf=null,last=null,request=null;
  function driveFrequency(){if(time>=1000)return null;const c=model.config;return c.mode==='sweep'?1900+time:c.frequency+(c.mode==='off'?400:0);}
  function glass(envelope){
    const el=$('glass'),r=el.getBoundingClientRect(),dpr=devicePixelRatio||1;el.width=r.width*dpr;el.height=r.height*dpr;const c=el.getContext('2d');c.scale(dpr,dpr);
    const scale=Math.min(r.width/540,1),cx=r.width*.7,cy=r.height*.44;
    const motion=16*Math.min(1,envelope/.04)*Math.sin(time/1000*2*Math.PI*5)*scale;
    c.lineWidth=2;c.strokeStyle=time<1000?U.colors.teal:'#56677c';c.strokeRect(r.width*.08,cy-32,45*scale,65);
    if(time<1000){for(let n=1;n<=3;n++){c.globalAlpha=.6/n;c.beginPath();c.arc(r.width*.08+45*scale,cy,25*n*scale,-.65,.65);c.stroke();}c.globalAlpha=1;}
    c.strokeStyle=U.colors.purple;c.beginPath();c.ellipse(cx+motion,cy-30,52*scale,12*scale,0,0,Math.PI*2);c.moveTo(cx+motion-52*scale,cy-30);c.bezierCurveTo(cx+motion-45*scale,cy+45,cx+35*scale,cy+48,cx+motion+52*scale,cy-30);c.stroke();
    c.beginPath();c.moveTo(cx,cy+36);c.lineTo(cx,cy+82);c.moveTo(cx-34*scale,cy+82);c.lineTo(cx+34*scale,cy+82);c.stroke();
    c.font='12px Consolas,monospace';c.fillStyle=U.colors.text;c.textAlign='center';c.fillText(time<1000?'Воздействие включено':'Динамик выключен',r.width*.23,r.height-25);c.fillText('fᵣ = '+U.format(model.config.frequency)+' Гц',cx,r.height-25);
  }
  function draw(){
    if(!model)return;const index=Math.min(model.length-1,Math.round(time*48)),amplitude=model.envelope[index];glass(amplitude);
    const c=model.config,p=U.plot('response-curve',1900,3300,0,1.15,[0,.5,1],'Гц');const pts=[];for(let f=1900;f<=3300;f+=2)pts.push([f,D.gain(f,c.frequency,c.tauMs)]);p.trace(pts,U.colors.purple);p.vertical(c.frequency,U.colors.muted,true);const frequency=driveFrequency();if(frequency!==null)p.vertical(frequency);
    const h=U.plot('history',0,2000,-.18,.18,[-.15,0,.15]);U.minmax(h,model.response,D.FS,'#64e5c060');const env=[];for(let n=0;n<model.length;n+=96)env.push([n/48,model.envelope[n]]);h.trace(env,U.colors.purple);h.vertical(1000,U.colors.muted,true);h.vertical(time);
    $('time').value=time;U.text('time-value',U.format(time)+' мс');U.text('drive-frequency',frequency===null?'—':U.format(frequency)+' Гц');U.text('natural-metric',U.format(c.frequency)+' Гц');U.text('amplitude',U.format(amplitude,4));U.text('state-title',time<1000?'Воздействие включено':'Свободное затухание');U.text('state',time<1000?'Энергия поступает извне':'Воздействия больше нет');
  }
  function redraw(){if(!request)request=requestAnimationFrame(()=>{request=null;draw();});}
  function stop(){playing=false;if(raf!==null)cancelAnimationFrame(raf);raf=null;last=null;U.text('play','▶ Запустить опыт');$('play').setAttribute('aria-pressed','false');}
  function tick(now){if(!playing)return;if(last!==null)time+=(now-last)*+$('speed').value;last=now;if(time>=1999){time=1999;stop();draw();return;}draw();raf=requestAnimationFrame(tick);}
  function toggle(){if(playing){stop();return;}if(time>=1999)time=0;playing=true;last=null;U.text('play','Ⅱ Пауза');$('play').setAttribute('aria-pressed','true');raf=requestAnimationFrame(tick);}
  function calculate(){stop();time=0;model=D.simulate({frequency:+$('natural').value,tauMs:+$('tau').value,mode:$('mode').value});U.text('natural-value',U.format(model.config.frequency)+' Гц');U.text('tau-value',U.format(model.config.tauMs)+' мс');redraw();}
  ['natural','tau','mode'].forEach(id=>$(id).addEventListener('input',calculate));$('time').addEventListener('input',()=>{stop();time=+$('time').value;redraw();});$('play').addEventListener('click',toggle);$('speed').addEventListener('change',()=>{last=null;});$('restart').addEventListener('click',()=>{stop();time=0;redraw();});$('release').addEventListener('click',()=>{stop();time=1100;redraw();});$('crossing').addEventListener('click',()=>{stop();time=model.config.mode==='sweep'?model.config.frequency-1900:500;redraw();});$('reset').addEventListener('click',()=>{$('natural').value=2450;$('tau').value=180;$('mode').value='sweep';$('speed').value='0.25';calculate();});document.addEventListener('visibilitychange',()=>{last=null;});U.fullscreen();U.keyboard(toggle);new ResizeObserver(redraw).observe(document.querySelector('.workspace'));calculate();
})();

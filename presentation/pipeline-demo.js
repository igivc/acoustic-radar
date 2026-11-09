(() => {
  'use strict';
  const $=id=>document.getElementById(id),D=window.PipelineDSP,U=window.TeachingUI;
  let model=new D.Pipeline(),playing=false,raf=null,last=null,elapsed=0;
  function ring(){
    const el=$('ring'),bounds=el.getBoundingClientRect(),ratio=devicePixelRatio||1;el.width=bounds.width*ratio;el.height=bounds.height*ratio;const c=el.getContext('2d');c.scale(ratio,ratio);
    const cx=bounds.width/2,cy=bounds.height/2,r=Math.min(bounds.width,bounds.height)*.32;
    for(let sector=0;sector<16;sector++){
      let occupied=0;for(let b=sector*16;b<(sector+1)*16;b++){const distance=(b-model.read%D.CAPACITY+D.CAPACITY)%D.CAPACITY;if(distance<model.queue.length)occupied++;}
      const angle=-Math.PI/2+sector*2*Math.PI/16;c.strokeStyle=occupied?(model.queue.length===D.CAPACITY?U.colors.amber:U.colors.teal):'#263446';c.globalAlpha=occupied?.2+.8*occupied/16:1;c.lineWidth=15;c.beginPath();c.arc(cx,cy,r,angle+.025,angle+2*Math.PI/16-.025);c.stroke();
    }
    c.globalAlpha=1;c.font='23px Consolas,monospace';c.fillStyle=U.colors.teal;c.textAlign='center';c.fillText(U.format(model.queue.length/D.CAPACITY*100,0)+'%',cx,cy+6);
    c.font='11px Consolas,monospace';
    for(const [counter,label,color,radius] of [[model.write,'W',U.colors.amber,r+27],[model.read,'R',U.colors.purple,r-27]]){const angle=-Math.PI/2+(counter%D.CAPACITY)/D.CAPACITY*2*Math.PI;c.fillStyle=color;c.fillText(label,cx+Math.cos(angle)*radius,cy+Math.sin(angle)*radius+4);}
  }
  function draw(){
    ring();U.text('produced',U.format(model.produced));U.text('occupancy',`${model.queue.length} / ${D.CAPACITY}`);U.text('cycles',U.format(model.cycles));U.text('displayed',U.format(model.displayed));
    U.text('ring-indices',`write = ${model.write} блоков; read = ${model.read} блоков`);U.text('ring-slots',`W = ${model.write%D.CAPACITY}; R = ${model.read%D.CAPACITY}; занято ${(model.write-model.read)*D.BLOCK} кадров`);
    U.text('buffer-indices',`base = ${model.base}; буфер = ${model.count} кадров; next = ${model.next===null?'—':model.next}`);U.text('pending-marker',model.pending===null?'Ожидающий маркер: нет':`Маркер: local ${model.pending} → absolute ${model.base+model.pending}`);
    $('ready').replaceChildren();if(!model.ready.length){const el=document.createElement('span');el.className='hint';el.textContent='Очередь пуста';$('ready').append(el);}else for(const item of model.ready){const el=document.createElement('span');el.className='ready-token';el.textContent='Кадр '+item.id;$('ready').append(el);}
    U.text('display-info',model.lastDisplay?`Последний показанный кадр №${model.lastDisplay.id}: marker = ${model.lastDisplay.marker}`:'Ни один полный спектр ещё не показан.');
    $('events').replaceChildren();for(const event of model.events){const el=document.createElement('li');el.textContent=U.format(event.time,3)+' с · '+event.message;$('events').append(el);}
    U.text('input-dropped',U.format(model.dropped)+' кадров');U.text('gaps',U.format(model.gaps));U.text('ui-dropped',U.format(model.uiDropped));U.text('buffer-count',U.format(model.count));U.text('virtual-time',U.format(model.produced/D.FS,3)+' с модели');
    U.text('state-title',model.queue.length===D.CAPACITY?'Входное кольцо заполнено':model.uiPaused?'SDL приостановлен, анализ продолжается':model.workerRate===0?'Рабочий поток приостановлен':model.gaps?'Обработка восстановлена после разрыва':'Поток обрабатывается');
    const cycle=model.lastCycle;U.text('cycle-example',cycle?`Последний цикл: base ${cycle.base} + local ${cycle.local} = marker ${cycle.marker}; интервал [${cycle.marker}, ${cycle.end})`:'Для первого результата нужно накопить полный цикл после маркера.');
    $('ring-stage').classList.toggle('warning',model.queue.length===D.CAPACITY);$('ui-stage').classList.toggle('warning',model.ready.length===8);
  }
  function stop(){playing=false;if(raf!==null)cancelAnimationFrame(raf);raf=null;last=null;elapsed=0;U.text('play','▶ Запустить поток');$('play').setAttribute('aria-pressed','false');}
  function tick(now){if(!playing)return;if(last!==null)elapsed+=Math.min(200,now-last);last=now;while(elapsed>=40){model.step();elapsed-=40;}draw();raf=requestAnimationFrame(tick);}
  function toggle(){if(playing){stop();return;}playing=true;last=null;U.text('play','Ⅱ Пауза');$('play').setAttribute('aria-pressed','true');raf=requestAnimationFrame(tick);}
  function controls(){$('worker-rate').value=String(model.workerRate);$('ui-paused').checked=model.uiPaused;}
  $('play').addEventListener('click',toggle);$('step').addEventListener('click',()=>{stop();model.step();draw();});$('second').addEventListener('click',()=>{stop();model.advance(94);draw();});
  $('normal').addEventListener('click',()=>{stop();model=new D.Pipeline();controls();draw();});
  $('overflow').addEventListener('click',()=>{stop();model=new D.Pipeline().preset('overflow');controls();draw();});$('slow-ui').addEventListener('click',()=>{stop();model=new D.Pipeline().preset('slow-ui');controls();draw();});
  $('recover').addEventListener('click',()=>{model.workerRate=3;model.uiPaused=false;controls();if(!playing)toggle();});
  $('worker-rate').addEventListener('change',()=>{model.workerRate=+$('worker-rate').value;draw();});$('ui-paused').addEventListener('change',()=>{model.uiPaused=$('ui-paused').checked;draw();});
  document.addEventListener('visibilitychange',()=>{last=null;});new ResizeObserver(draw).observe($('ring'));U.fullscreen();U.keyboard(toggle);draw();
})();

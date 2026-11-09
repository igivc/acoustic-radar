/* Общие функции графиков для дополнительных демонстраций. Все ресурсы локальные. */
(function (root) {
  'use strict';
  const colors = { grid:'#253242', text:'#8d9eb3', teal:'#64e5c0', purple:'#aa9bff', amber:'#ffbf69', muted:'#71859c' };
  const format = (value, digits = 0) => Number.isFinite(value) ? value.toLocaleString('ru-RU',{minimumFractionDigits:digits,maximumFractionDigits:digits}) : '—';
  const text = (id, value) => { const el = document.getElementById(id); if (el && el.textContent !== value) el.textContent = value; };
  function plot(id, xMin, xMax, yMin, yMax, ticks, unit='мс', xSteps=4, digits=0) {
    const el=document.getElementById(id), bounds=el.getBoundingClientRect(), ratio=devicePixelRatio||1;
    el.width=Math.round(bounds.width*ratio);el.height=Math.round(bounds.height*ratio);
    const ctx=el.getContext('2d');ctx.scale(ratio,ratio);
    const box={left:49,right:bounds.width-16,top:19,bottom:bounds.height-26};
    const x=v=>box.left+(v-xMin)/(xMax-xMin)*(box.right-box.left), y=v=>box.bottom-(v-yMin)/(yMax-yMin)*(box.bottom-box.top);
    ctx.font='10px Consolas,monospace';ctx.lineWidth=1;
    for(let i=0;i<=xSteps;i++){
      const v=xMin+(xMax-xMin)*i/xSteps;ctx.strokeStyle=colors.grid;ctx.beginPath();ctx.moveTo(x(v),box.top);ctx.lineTo(x(v),box.bottom);ctx.stroke();
      ctx.fillStyle=colors.text;ctx.textAlign='center';ctx.fillText(format(v,digits)+(i===0?' '+unit:''),x(v),bounds.height-6);
    }
    for(const v of ticks){ctx.strokeStyle=colors.grid;ctx.beginPath();ctx.moveTo(box.left,y(v));ctx.lineTo(box.right,y(v));ctx.stroke();ctx.fillStyle=colors.text;ctx.textAlign='right';ctx.fillText(format(v,Math.abs(v)<1&&v!==0?2:0),box.left-6,y(v)+3);}
    function trace(points,color){ctx.save();ctx.beginPath();ctx.rect(box.left,box.top,box.right-box.left,box.bottom-box.top);ctx.clip();ctx.strokeStyle=color;ctx.lineWidth=1.6;ctx.beginPath();points.forEach(([a,b],i)=>i?ctx.lineTo(x(a),y(b)):ctx.moveTo(x(a),y(b)));ctx.stroke();ctx.restore();}
    function vertical(v,color=colors.amber,dashed=false){ctx.save();ctx.setLineDash(dashed?[4,4]:[]);ctx.strokeStyle=color;ctx.beginPath();ctx.moveTo(x(v),box.top);ctx.lineTo(x(v),box.bottom);ctx.stroke();ctx.restore();}
    return {ctx,box,x,y,trace,vertical};
  }
  function minmax(p,data,fs,color){
    const cols=Math.max(1,Math.floor(p.box.right-p.box.left));p.ctx.strokeStyle=color;p.ctx.beginPath();
    for(let c=0;c<cols;c++){const a=Math.floor(c*data.length/cols),b=Math.floor((c+1)*data.length/cols);let lo=Infinity,hi=-Infinity;for(let j=a;j<b;j++){lo=Math.min(lo,data[j]);hi=Math.max(hi,data[j]);}if(b>a){const x=p.box.left+c+.5;p.ctx.moveTo(x,p.y(lo));p.ctx.lineTo(x,p.y(hi));}}
    p.ctx.stroke();
  }
  function fullscreen(id='fullscreen'){
    const el=document.getElementById(id);el.addEventListener('click',async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await document.documentElement.requestFullscreen();}catch{text(id,'Используйте F11');}});
    document.addEventListener('fullscreenchange',()=>text(id,document.fullscreenElement?'Выйти из экрана ↙':'На весь экран ↗'));
  }
  function keyboard(toggle){document.addEventListener('keydown',event=>{if(event.code==='Space'&&!['INPUT','SELECT','BUTTON','SUMMARY','TEXTAREA'].includes(event.target.tagName)){event.preventDefault();toggle();}});}
  root.TeachingUI={colors,format,text,plot,minmax,fullscreen,keyboard};
})(window);

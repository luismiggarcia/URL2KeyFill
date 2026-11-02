// Runs against the real interface HTML with tools/preview-ui-controls.py only.
async function testAudioControls() {
  const q = selector => document.querySelector(selector);
  const assert = (condition, message) => { if (!condition) throw Error(message); };
  const settle = () => new Promise(resolve => setTimeout(resolve, 25));
  const sent = () => JSON.parse(q('#test-events').textContent);
  const results = [];
  // The application and autonomous buttons consume the same graphite CSS.
  const compareButton=(native,attributes)=>{
    const component=document.createElement('studio-button');
    for(const [name,value] of Object.entries({skin:'graphite',...attributes}))component.setAttribute(name,value);
    component.textContent=native.textContent;
    component.style.cssText='position:fixed;left:-10000px;top:0';document.body.append(component);
    const a=getComputedStyle(native),b=getComputedStyle(component.shadowRoot.querySelector('button'));
    for(const property of ['backgroundImage','borderTopColor','borderRadius','boxShadow','color'])assert(a[property]===b[property],'Shared button paint: '+property);
    component.remove();
  };
  compareButton(q('[data-action=audio]'),{square:'',variant:'normal'});
  compareButton(q('[data-action=image]'),{square:'',variant:'amber'});
  compareButton(q('[data-action=delete-layer]'),{variant:'delete'});
  const savedDevice=fixture.device;
  fixture.device='';window.receiveState(fixture);
  const play=q('[data-action=play]');
  assert(play.disabled && getComputedStyle(play).opacity==='1','Unavailable device must disable PLAY without fading its body');
  assert(Number(getComputedStyle(play.querySelector('span')).opacity)<.3,'Disabled PLAY label is too bright');
  fixture.device=savedDevice;window.receiveState(fixture);
  const pause=q('[data-action=pause]');
  assert(pause.disabled && getComputedStyle(pause).opacity==='1','Disabled PAUSE faded its body');
  assert(Number(getComputedStyle(pause.querySelector('span')).opacity)<.3,'Disabled PAUSE label is too bright');
  results.push('botões nativos e autónomos com o mesmo acabamento; PAUSE com legenda apagada');
  const geometry=meter=>{
    const box=meter.shadowRoot.querySelector('.meter').getBoundingClientRect();
    const channel=meter.shadowRoot.querySelector('.channel').getBoundingClientRect();
    return {width:box.width,height:box.height,dx:channel.x+channel.width/2-box.x-box.width/2,dy:channel.y+channel.height/2-box.y-box.height/2};
  };
  const assertMono=(meter,before,horizontal=false)=>{
    const after=geometry(meter);
    assert(after.width===before.width&&after.height===before.height,'Mono changed stereo footprint');
    assert(Math.abs(horizontal?after.dy:after.dx)<.1,'Mono channel is not centered');
    const well=getComputedStyle(meter.shadowRoot.querySelector('.meter'),'::before');
    assert(parseFloat(horizontal?well.height:well.width)<(horizontal?after.height:after.width),'Mono background still spans stereo channels');
  };
  if(q('[data-action=edit]').getAttribute('aria-pressed')==='true')q('[data-action=edit]').click();
  if(q('[data-action=audio]').getAttribute('aria-expanded')==='true')q('[data-action=audio]').click();
  // Work from the same public native-state entry point as the application.
  fixture.layers.forEach(layer => Object.assign(layer, {gain:1,mono:false,muted:false,visible:true}));
  fixture.audioGain=1;fixture.audioMono=false;fixture.audioMuted=false;
  window.receiveState(fixture);paintFixture();
  const compact=q('.sr-layer-row audio-level-meter');
  assert(compact.getBoundingClientRect().width===17 && compact.getBoundingClientRect().height===33,'Compact meter dimensions');
  assert(Math.abs(q('.sr-lcd').getBoundingClientRect().height-q('.sr-matrix').getBoundingClientRect().height)<1,'Monitor height changed');
  const channel=compact.shadowRoot.querySelector('.channel');
  assert([...channel.children].filter(e=>getComputedStyle(e).display!=='none').length===10,'Compact segment count');
  assert(compact.shadowRoot.querySelector('[role=img]').getAttribute('aria-label').includes('-18.0 dBFS'),'RMS calibration changed');
  results.push('compacto 17 × 33, calibração RMS e altura do monitor');
  q('[data-action=edit]').click();q('.sr-layer-toggle').click();
  const horizontal=q('.sr-layer-gain');
  for(const db of [-Infinity,-12,-6,-3,0,2,4,5,6]) {
    const label=db===-Infinity?'−∞':db>0?'+'+db:db<0?'−'+Math.abs(db):'0';
    const tick=[...horizontal.shadowRoot.querySelectorAll('button')].find(b=>b.textContent===label);
    tick.click();
    const gain=db===-Infinity?0:10**(db/20);
    assert(sent().type==='layer' && sent().id==='a' && Math.abs(sent().gain-gain)<1e-9,'Horizontal marker route '+label);
    await settle();
  }
  horizontal.shadowRoot.querySelector('input').dispatchEvent(new MouseEvent('dblclick',{bubbles:true,composed:true}));
  assert(sent().gain===1,'Horizontal reset to unity');await settle();
  for(const orientation of ['horizontal','vertical']){
    const slider=document.createElement('gain-slider');slider.setAttribute('orientation',orientation);document.body.append(slider);
    const input=slider.shadowRoot.querySelector('input');
    slider.value=1;
    input.dispatchEvent(new KeyboardEvent('keydown',{key:orientation==='vertical'?'ArrowUp':'ArrowRight',cancelable:true}));
    assert(Math.abs(slider.decibels-.1)<1e-6,'Keyboard +0.1 dB '+orientation);
    input.dispatchEvent(new KeyboardEvent('keydown',{key:orientation==='vertical'?'ArrowDown':'ArrowLeft',cancelable:true}));
    assert(Math.abs(slider.decibels)<1e-6,'Keyboard -0.1 dB '+orientation);
    slider.remove();
  }
  results.push('setas horizontais e verticais em passos de 0,1 dB');
  const field=q('.sr-gain-db');field.value='2';field.dispatchEvent(new Event('change'));
  assert(Math.abs(sent().gain-10**.1)<1e-9,'Numeric dB input');await settle();
  const compactStereo=geometry(q('.sr-layer-row audio-level-meter')),editorStereo=geometry(q('.sr-drawer-audio-meter'));
  q('.sr-layer-mono').click();await settle();paintFixture();
  assertMono(q('.sr-layer-row audio-level-meter'),compactStereo);
  assertMono(q('.sr-drawer-audio-meter'),editorStereo,true);
  const editor=q('.sr-drawer-audio-meter');
  assert(editor.hasAttribute('mono') && getComputedStyle(editor.shadowRoot.querySelectorAll('.channel')[1]).display==='none','Mono channel visibility');
  q('.sr-layer-mute').click();await settle();paintFixture();
  assert(editor.hasAttribute('muted'),'Muted meter state');
  results.push('9 marcadores horizontais, reset, entrada em dB, mono e mute');
  q('.sr-layer-mono').click();await settle();q('.sr-layer-mute').click();await settle();
  q('[data-action=audio]').click();
  for(const id of ['a','output']) {
    const strip=q(`.sr-audio-strip[data-id="${id}"]`),meter=strip.querySelector('audio-level-meter'),stereo=geometry(meter);
    strip.querySelector('.sr-audio-mono').click();await settle();paintFixture();assertMono(meter,stereo);
    strip.querySelector('.sr-audio-mono').click();await settle();
    const pfl=strip.querySelector('.sr-audio-pfl'),afl=strip.querySelector('.sr-audio-afl');
    const pr=pfl.getBoundingClientRect(),ar=afl.getBoundingClientRect();assert(Math.abs(pr.top-ar.top)<1&&ar.left>=pr.right,'PFL/AFL must share a row');
    for(const button of [pfl,afl]){button.dispatchEvent(new MouseEvent('mouseenter'));const tip=q('.sr-monitor-tooltip');assert(tip.hidden,'Tooltip appeared without delay');await new Promise(resolve=>setTimeout(resolve,1050));assert(!tip.hidden&&tip.querySelector('strong').textContent===button.dataset.field.toUpperCase()+' MONITOR','Tooltip heading');assert(getComputedStyle(tip.querySelector('span')).display==='block','Tooltip description line');button.dispatchEvent(new MouseEvent('mouseleave'));}
    pfl.click();assert(sent().type==='audioMonitor'&&sent().id===id&&sent().mode==='pfl','PFL bridge');await settle();assert(pfl.getAttribute('aria-pressed')==='true','PFL selection');
    afl.click();await settle();assert(afl.getAttribute('aria-pressed')==='true'&&pfl.getAttribute('aria-pressed')==='false','Switch to AFL');
    afl.click();await settle();assert(afl.getAttribute('aria-pressed')==='false','Toggle monitor off');
    const slider=strip.querySelector('gain-slider');
    for(const label of ['−∞','−6','0','+6']) {
      [...slider.shadowRoot.querySelectorAll('button')].find(b=>b.textContent===label).click();
      const message=sent();
      assert(message.type===(id==='output'?'audioGlobal':'layer'),'Vertical route '+id);
      assert(id==='output'||message.id===id,'Layer identity');await settle();
    }
    slider.shadowRoot.querySelector('input').dispatchEvent(new MouseEvent('dblclick',{bubbles:true,composed:true}));
    assert(sent().gain===1,'Vertical reset '+id);await settle();
  }
  results.push('mono centrado: compacto, horizontal, layer e output');
  results.push('faders de layer/output: silêncio, valores intermédios, extremo e reset');
  paintFixture();
  const master=q('.sr-composite-audio-meter'),r=master.getBoundingClientRect(),host=q('.sr-master-meter-slot').getBoundingClientRect();
  assert(r.height<=host.height+.5,'Master meter overflows');
  assert(getComputedStyle(q('.sr-audio-output audio-level-meter').shadowRoot.querySelector('.meter')).backgroundColor===getComputedStyle(q('.sr-audio-lanes audio-level-meter').shadowRoot.querySelector('.meter')).backgroundColor,'Meter backgrounds differ');
  return 'PASS · '+results.join(' · ');
}

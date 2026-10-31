// Runs against the real interface HTML with tools/preview-ui-controls.py only.
async function testAudioControls() {
  const q = selector => document.querySelector(selector);
  const assert = (condition, message) => { if (!condition) throw Error(message); };
  const settle = () => new Promise(resolve => setTimeout(resolve, 25));
  const sent = () => JSON.parse(q('#test-events').textContent);
  const results = [];
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
  const field=q('.sr-gain-db');field.value='2';field.dispatchEvent(new Event('change'));
  assert(Math.abs(sent().gain-10**.1)<1e-9,'Numeric dB input');await settle();
  q('.sr-layer-mono').click();await settle();paintFixture();
  const editor=q('.sr-drawer-audio-meter');
  assert(editor.hasAttribute('mono') && getComputedStyle(editor.shadowRoot.querySelectorAll('.channel')[1]).display==='none','Mono channel visibility');
  q('.sr-layer-mute').click();await settle();paintFixture();
  assert(editor.hasAttribute('muted'),'Muted meter state');
  results.push('9 marcadores horizontais, reset, entrada em dB, mono e mute');
  q('.sr-layer-mono').click();await settle();q('.sr-layer-mute').click();await settle();
  q('[data-action=audio]').click();
  for(const id of ['a','output']) {
    const slider=q(`.sr-audio-strip[data-id="${id}"] gain-slider`);
    for(const label of ['−∞','−6','0','+6']) {
      [...slider.shadowRoot.querySelectorAll('button')].find(b=>b.textContent===label).click();
      const message=sent();
      assert(message.type===(id==='output'?'audioGlobal':'layer'),'Vertical route '+id);
      assert(id==='output'||message.id===id,'Layer identity');await settle();
    }
    slider.shadowRoot.querySelector('input').dispatchEvent(new MouseEvent('dblclick',{bubbles:true,composed:true}));
    assert(sent().gain===1,'Vertical reset '+id);await settle();
  }
  results.push('faders de layer/output: silêncio, valores intermédios, extremo e reset');
  paintFixture();
  const master=q('.sr-composite-audio-meter'),r=master.getBoundingClientRect(),host=q('.sr-master-meter-slot').getBoundingClientRect();
  assert(r.height<=host.height+.5,'Master meter overflows');
  assert(getComputedStyle(q('.sr-audio-output audio-level-meter').shadowRoot.querySelector('.meter')).backgroundColor===getComputedStyle(q('.sr-audio-lanes audio-level-meter').shadowRoot.querySelector('.meter')).backgroundColor,'Meter backgrounds differ');
  return 'PASS · '+results.join(' · ');
}

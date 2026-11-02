/** Gain slider extracted from assets/interface.html; no application dependencies. */
export class GainSlider extends HTMLElement {
  static observedAttributes = ['value', 'orientation', 'disabled', 'label'];
  #input;
  #scaleOrientation;
  constructor() {
    super();
    this.attachShadow({mode:'open'}).innerHTML = `<style>
    :host{display:inline-block;width:280px;color:#9aabbd;font:12px system-ui,sans-serif}
    .track{display:flow-root;position:relative;padding-bottom:24px;margin:0 10px}
    input{-webkit-appearance:none;appearance:none;box-sizing:border-box;display:block;width:100%;min-width:0;height:6px;margin:10px 0;border:1px solid #111820;border-radius:3px;background:linear-gradient(#0d141c,#354252);box-shadow:inset 0 1px 3px #0009;cursor:pointer}
    input::-webkit-slider-thumb{box-sizing:border-box;-webkit-appearance:none;width:13px;height:25px;border:1px solid #84909e;border-radius:3px;background:linear-gradient(#9da7b3,#56616e 45%,#d6dde5 48%,#d6dde5 52%,#53606e 55%,#727e8b);box-shadow:0 2px 4px #0008}
    input::-moz-range-thumb{box-sizing:border-box;width:13px;height:25px;border:1px solid #84909e;border-radius:3px;background:linear-gradient(#9da7b3,#56616e 45%,#d6dde5 48%,#d6dde5 52%,#53606e 55%,#727e8b);box-shadow:0 2px 4px #0008}
    .scale{position:absolute;left:7.5px;right:7.5px;top:29px;height:16px}
    .tick{position:absolute;transform:translateX(-50%);white-space:nowrap;border:0;padding:0;background:transparent;color:inherit;font:inherit;line-height:16px;cursor:pointer}
    .tick:disabled{cursor:default;opacity:.45}
    .tick:focus-visible{outline:1px solid #edc06b;outline-offset:2px}
    .tick:before{content:'';position:absolute;left:50%;top:-9px;height:5px;width:2px;background:#c6d3e4;transform:translateX(-50%)}
    :host([orientation="vertical"]){width:60px;height:200px}
    :host([orientation="vertical"]) .track{padding:0;margin:0;height:180px}
    :host([orientation="vertical"]) input{writing-mode:vertical-lr;direction:rtl;flex:none;width:6px;height:180px;margin:0 10px;background:linear-gradient(90deg,#0d141c,#354252);box-shadow:inset 1px 0 3px #0009}
    :host([orientation="vertical"]) input::-webkit-slider-thumb{width:25px;height:13px;background:linear-gradient(#9da7b3,#56616e 40%,#d6dde5 44%,#d6dde5 56%,#53606e 60%,#727e8b)}
    :host([orientation="vertical"]) input::-moz-range-thumb{width:25px;height:13px}
    :host([orientation="vertical"]) .scale{left:29px;right:auto;top:7.5px;bottom:7.5px;height:auto}
    :host([orientation="vertical"]) .tick{transform:translateY(50%)}
    :host([orientation="vertical"]) .tick:before{left:-9px;top:50%;width:4px;height:2px;transform:translateY(-50%)}
    :host([orientation="vertical"]) .tick.zero{padding-left:1ch}
    /* Horizontal track: remove native input borders/padding from thumb travel. */
    :host(:not([orientation="vertical"])) input{padding:0;border:0;background:transparent;box-shadow:none}
    :host(:not([orientation="vertical"])) input::-webkit-slider-runnable-track{box-sizing:border-box;height:6px;border:1px solid #111820;border-radius:3px;background:linear-gradient(#0d141c,#354252);box-shadow:inset 0 1px 3px #0009}
    :host(:not([orientation="vertical"])) input::-webkit-slider-thumb{margin-top:-10.5px}
    :host(:not([orientation="vertical"])) input::-moz-range-track{box-sizing:border-box;height:6px;border:1px solid #111820;border-radius:3px;background:linear-gradient(#0d141c,#354252);box-shadow:inset 0 1px 3px #0009}
    :host(:not([orientation="vertical"])) .scale{left:7.5px;right:7.5px}
    input:disabled{opacity:.45;cursor:default}
    </style><div class="track"><input type="range" min="0" max="2" step="any" value="1"><span class="scale"></span></div>`;
    this.#input = this.shadowRoot.querySelector('input');
    for (const type of ['input','change']) this.#input.addEventListener(type, event => {
      event.stopPropagation();
      this.setAttribute('value', this.#input.value);
      this.dispatchEvent(new Event(type,{bubbles:true,composed:true}));
    });
    this.#input.addEventListener('keydown',event=>{
      if(!['ArrowUp','ArrowDown','ArrowLeft','ArrowRight'].includes(event.key))return;
      event.preventDefault();
      if(this.disabled)return;
      const vertical=this.getAttribute('orientation')==='vertical';
      const up=vertical?'ArrowUp':'ArrowRight',down=vertical?'ArrowDown':'ArrowLeft';
      if(event.key!==up&&event.key!==down)return;
      const direction=event.key===up?1:-1;
      const db=this.decibels;
      // Silence has no finite dB value; the first upward step enters at −60 dB.
      const next=db===-Infinity?(direction>0?-60:-Infinity):Math.round((db+direction*.1)*10)/10;
      this.decibels=next < -60?-Infinity:Math.min(20*Math.log10(2),next);
      for(const type of ['input','change'])this.dispatchEvent(new Event(type,{bubbles:true,composed:true}));
    });
    this.#input.addEventListener('dblclick',()=>{
      if(this.disabled) return;
      this.value=1;
      for(const type of ['input','change']) this.dispatchEvent(new Event(type,{bubbles:true,composed:true}));
    });
  }
  connectedCallback(){this.#render();}
  attributeChangedCallback(){this.#render();}
  get value(){return Number(this.#input.value);}
  set value(value){const n=Number(value);if(!Number.isFinite(n))throw new TypeError('Gain must be finite');this.setAttribute('value',String(Math.max(0,Math.min(2,n))));}
  get decibels(){return this.value>0?20*Math.log10(this.value):-Infinity;}
  set decibels(db){if(db!==-Infinity&&!Number.isFinite(db))throw new TypeError('Invalid dB');this.value=db===-Infinity?0:10**(db/20);}
  get disabled(){return this.hasAttribute('disabled');}
  set disabled(value){this.toggleAttribute('disabled',Boolean(value));}
  #render(){
    if(!this.#input)return;
    const raw=this.getAttribute('value');const n=raw===null?1:Number(raw);
    this.#input.value=String(Number.isFinite(n)?Math.max(0,Math.min(2,n)):1);
    this.#input.disabled=this.disabled;
    this.#input.setAttribute('aria-label',this.getAttribute('label')||'Ganho de áudio');
    this.#input.setAttribute('aria-valuetext',this.value===0?'−∞ dB':this.decibels.toFixed(1)+' dB');
    const vertical=this.getAttribute('orientation')==='vertical';
    const scale=this.shadowRoot.querySelector('.scale');
    if(this.#scaleOrientation===vertical){scale.querySelectorAll('button').forEach(b=>b.disabled=this.disabled);return;}
    this.#scaleOrientation=vertical;scale.replaceChildren();
    for(const db of [-Infinity,-12,-6,-3,0,2,4,5,6]){
      const tick=document.createElement('button');tick.type='button';tick.className=db===0?'tick zero':'tick';tick.disabled=this.disabled;
      const percent=db===-Infinity?0:Math.min(100,10**(db/20)*50);
      tick.style[vertical?'bottom':'left']=percent+'%';
      tick.textContent=db===-Infinity?'−∞':db>0?'+'+db:db<0?'−'+Math.abs(db):'0';
      tick.setAttribute('aria-label','Definir ganho para '+tick.textContent+' dB');
      tick.addEventListener('click',()=>{
        if(this.disabled)return;
        this.decibels=db;
        for(const type of ['input','change'])this.dispatchEvent(new Event(type,{bubbles:true,composed:true}));
      });
      scale.append(tick);
    }
  }
}
if(!customElements.get('gain-slider'))customElements.define('gain-slider',GainSlider);

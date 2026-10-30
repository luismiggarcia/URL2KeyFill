/** Buttons extracted from assets/interface.html; presentation and local state only. */
export class StudioButton extends HTMLElement {
  static observedAttributes=['variant','square','led','pressed','toggle','disabled','label'];
  #button;
  constructor(){
    super();
    this.attachShadow({mode:'open'}).innerHTML=`<style>
    :host{display:inline-block;vertical-align:middle;--studio-key-size:62px}
    button{box-sizing:border-box;font:500 11px -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;letter-spacing:.3px;cursor:default;background:linear-gradient(#4e5c6c,#354353);color:#ecf2f9;border:1px solid #7c8ea4;border-radius:4px;min-height:36px;padding:8px 14px;box-shadow:inset 0 1px 0 #ffffff15;display:inline-flex;align-items:center;justify-content:center;gap:10px}
    button:disabled{opacity:.45}
    :host([square]) button{width:var(--studio-key-size);height:var(--studio-key-size);min-height:0;border-radius:5px;border:1px solid #6c747f;box-shadow:inset 0 1px 0 #ffffff38,0 3px 0 #111417,0 5px 8px #0004;flex-direction:column;gap:7px;padding:5px;white-space:nowrap;background:linear-gradient(#4c535c,#353b43);color:#edf1f6}
    :host([square]) button:active:not(:disabled){transform:translateY(1px)}
    :host([variant="amber"]) button{background:linear-gradient(#f8dfa8,#d9a556);color:#453318;border-color:#e5c18b}
    :host([square][variant="play"][pressed]) button{background:linear-gradient(#d3978c,#aa695e);color:#fff0e9;border-color:#dcaa9b}
    :host([variant="delete"]) button{background:#403638;color:#e0b4ad;border-color:#756066}
    :host([variant="quit"]) button{background:linear-gradient(#d3978c,#aa695e);color:#fff0e9;border-color:#dcaa9b}
    :host([variant="refresh"]) button{gap:5px;font-size:11px;padding:0;min-height:22px}
    :host([led]) button{min-height:39px;padding:10px 16px;white-space:nowrap}
    :host([square][led]) button{padding:5px;min-height:0}
    .text{text-align:center;line-height:1.35}
    .power-icon{display:none;width:22px;height:22px;flex:0 0 22px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;color:#88f5a7;filter:drop-shadow(0 0 2px #69e78eaa) drop-shadow(0 0 5px #69e78e66)}
    :host([variant="power"]) .power-icon{display:block}
    :host([variant="power"]) .led{display:none}
    :host([variant="power"][disabled]) .power-icon{color:#91a298;filter:none}
    .led{display:none;width:8px;height:8px;flex:0 0 8px;border-radius:50%;background:radial-gradient(circle at 35% 30%,#fff,#d9dee4 55%,#939da9);box-shadow:0 0 4px #ffffff35,inset 0 0 1px #fff}
    :host([led]) .led{display:inline-block}
    :host([led="green"][pressed]) .led{background:radial-gradient(circle at 35% 30%,#dcffe5,#69e78e 55%,#27994b);box-shadow:0 0 5px #69e78e9c}
    :host([led="red"][pressed]) .led{background:radial-gradient(circle at 35% 30%,#ffd6ce,#f2796c 55%,#b42c28);box-shadow:0 0 5px #f2796c9c}
    </style><button type="button"><span class="led" aria-hidden="true"></span><svg class="power-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v9M6.35 5.65a9 9 0 1 0 11.3 0"/></svg><span class="text"><slot></slot></span></button>`;
    this.#button=this.shadowRoot.querySelector('button');
    this.#button.addEventListener('click',()=>{
      if(this.hasAttribute('toggle')){
        this.pressed=!this.pressed;
        this.dispatchEvent(new Event('change',{bubbles:true,composed:true}));
      }
    });
  }
  connectedCallback(){this.#render()}
  attributeChangedCallback(){this.#render()}
  get pressed(){return this.hasAttribute('pressed')}
  set pressed(value){this.toggleAttribute('pressed',Boolean(value))}
  get disabled(){return this.hasAttribute('disabled')}
  set disabled(value){this.toggleAttribute('disabled',Boolean(value))}
  focus(options){this.#button.focus(options)}
  #render(){
    if(!this.#button)return;
    this.#button.disabled=this.disabled;
    if(this.hasAttribute('toggle')||this.hasAttribute('led')||this.getAttribute('variant')==='play')this.#button.setAttribute('aria-pressed',String(this.pressed));
    else this.#button.removeAttribute('aria-pressed');
    const label=this.getAttribute('label');
    if(label)this.#button.setAttribute('aria-label',label);else this.#button.removeAttribute('aria-label');
  }
}
if(!customElements.get('studio-button'))customElements.define('studio-button',StudioButton);

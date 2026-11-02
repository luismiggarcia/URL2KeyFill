import {classicButtonCSS, nativeButtonCSS, graphiteButtonCSS} from './styles.generated.js';
/** Buttons extracted from assets/interface.html; presentation and local state only. */
export class StudioButton extends HTMLElement {
  static observedAttributes=['variant','square','led','pressed','toggle','disabled','label','skin'];
  #button;
  constructor(){
    super();
    this.attachShadow({mode:'open'}).innerHTML=`<style>${classicButtonCSS}</style><style id="graphite-style"></style><span id="studio-refined"><button type="button"><span class="led" aria-hidden="true"></span><svg class="power-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v9M6.35 5.65a9 9 0 1 0 11.3 0"/></svg><span class="text"><slot></slot></span></button></span>`;
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
    const graphite=this.getAttribute('skin')==='graphite';
    this.shadowRoot.querySelector('#graphite-style').textContent=graphite ? nativeButtonCSS+'\n'+graphiteButtonCSS : '';
    const variant=this.getAttribute('variant');
    this.#button.className=(this.hasAttribute('square')?'sr-key':'sr-small-button')+
      (this.hasAttribute('led')?' sr-led-button':'')+
      (variant==='amber'?' sr-amber':variant==='play'&&this.pressed?' sr-red':variant==='delete'?' sr-delete':variant==='quit'?' sr-quit':variant==='power'?' sr-power':'')+
      (this.getAttribute('led')==='red'?' sr-layer-mono':'');
    this.#button.setAttribute('aria-pressed',String(this.pressed));
    this.shadowRoot.querySelector('.led').classList.toggle('sr-status-led',this.hasAttribute('led')&&variant!=='power');
    this.shadowRoot.querySelector('.power-icon').classList.toggle('sr-power-icon',variant==='power');
    if(this.hasAttribute('toggle')||this.hasAttribute('led')||this.getAttribute('variant')==='play')this.#button.setAttribute('aria-pressed',String(this.pressed));
    else this.#button.removeAttribute('aria-pressed');
    const label=this.getAttribute('label');
    if(label)this.#button.setAttribute('aria-label',label);else this.#button.removeAttribute('aria-label');
  }
}
if(!customElements.get('studio-button'))customElements.define('studio-button',StudioButton);

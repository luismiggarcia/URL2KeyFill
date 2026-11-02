/** Isolated display component. Levels are supplied by the host in dBFS. */
export class AudioLevelMeter extends HTMLElement {
  static observedAttributes = ['orientation', 'compact', 'layer-compact', 'mono', 'muted', 'left', 'right'];
  #channels = [];
  #levels = [-Infinity, -Infinity];
  #peaks = [-Infinity, -Infinity];
  #expires = [0, 0];
  #timer;
  constructor() {
    super();
    const shadow = this.attachShadow({mode: 'open'});
    shadow.innerHTML = `<style>
      :host { display:inline-block; vertical-align:middle; --audio-led-size:8px; --audio-led-gap:3px; --audio-channel-gap:5px; --audio-meter-padding:7px; --audio-meter-background:#111820; --audio-meter-off:#303840; --audio-meter-green:#6bd78f; --audio-meter-yellow:#edc06b; --audio-meter-red:#ee8d80; }
      .meter { display:flex; width:max-content; min-width:calc(2 * var(--audio-led-width, var(--audio-led-size)) + var(--audio-channel-gap)); justify-content:center; gap:var(--audio-channel-gap); padding:var(--audio-meter-padding); background:var(--audio-meter-background); border-radius:3px; }
      .channel { display:flex; flex-direction:column-reverse; gap:var(--audio-led-gap); }
      .led { flex:none; width:var(--audio-led-size); height:var(--audio-led-size); border-radius:1px; background:var(--audio-meter-off); }
      .lit { background:var(--zone); opacity:.82; }
      .peak { background:var(--zone); opacity:1; box-shadow:0 0 3px var(--zone); }
      :host([orientation="horizontal"]) .meter { flex-direction:column; min-width:0; min-height:calc(2 * var(--audio-led-height, var(--audio-led-size)) + var(--audio-channel-gap)); }
      :host([orientation="horizontal"]) .channel { flex-direction:row; }
      :host([compact]) { --audio-led-size:2px; --audio-led-gap:1px; --audio-channel-gap:2px; --audio-meter-padding:4px; }
      :host([layer-compact]) { --audio-led-gap:1px; --audio-channel-gap:3px; --audio-meter-padding:2px; --audio-led-width:5px; --audio-led-height:2px; }
      :host([layer-compact]) .led { width:5px; height:2px; }
      :host([layer-compact]) .led:nth-child(n+11) { display:none; }
      /* Keep the stereo layout box; paint only the single visible channel's well. */
      :host([mono]) .meter { position:relative; isolation:isolate; background:transparent; }
      :host([mono]) .meter::before { content:''; position:absolute; z-index:-1; top:0; bottom:0; left:50%; transform:translateX(-50%); width:calc(var(--audio-led-width, var(--audio-led-size)) + 2 * var(--audio-meter-padding)); background:var(--audio-meter-background); border-radius:inherit; }
      :host([mono][orientation="horizontal"]) .meter::before { left:0; right:0; top:50%; bottom:auto; transform:translateY(-50%); width:auto; height:calc(var(--audio-led-height, var(--audio-led-size)) + 2 * var(--audio-meter-padding)); }
      :host([mono]) .channel:nth-child(2) { display:none; }
      :host([muted]) :is(.lit,.peak) { background:#a4adb8; box-shadow:none; }
    </style><div class="meter" role="img"></div>`;
    const meter = shadow.querySelector('.meter');
    this.#channels = [0, 1].map(() => {
      const channel = document.createElement('div');
      channel.className = 'channel';
      meter.append(channel);
      return Array.from({length:21}, (_, index) => {
        const led = document.createElement('span');
        led.className = 'led';
        const db = -60 + index * 3;
        led.style.setProperty('--zone', `var(--audio-meter-${db >= -3 ? 'red' : db >= -12 ? 'yellow' : 'green'})`);
        channel.append(led);
        return led;
      });
    });
  }
  connectedCallback() { this.#readAttributes(); }
  disconnectedCallback() { clearTimeout(this.#timer); }
  attributeChangedCallback(name) {
    if (name === 'left' || name === 'right') this.#readAttributes();
    else this.#render();
  }
  /** Set instantaneous [L, R] levels. Missing/non-finite values mean silence. */
  set levels(values) {
    if (!Array.isArray(values) || values.length !== 2) throw new TypeError('levels requires [leftDbFS, rightDbFS]');
    const now = performance.now();
    this.#levels = values.map(v => typeof v === 'number' && Number.isFinite(v) ? Math.min(0, v) : -Infinity);
    this.#levels.forEach((value, i) => {
      if (value >= this.#peaks[i] || now >= this.#expires[i]) {
        this.#peaks[i] = value;
        this.#expires[i] = now + 1000;
      }
    });
    this.#render();
    this.#schedulePeakExpiry();
  }
  get levels() { return [...this.#levels]; }
  /** Paint externally calibrated levels and held peaks (native audio adapter). */
  setReading(levels, peaks = levels) {
    if (![levels, peaks].every(v => Array.isArray(v) && v.length === 2)) throw new TypeError('Expected stereo levels and peaks');
    const clean = values => values.map(v => typeof v === 'number' && Number.isFinite(v) ? Math.min(0, v) : -Infinity);
    clearTimeout(this.#timer);
    this.#levels = clean(levels);
    this.#peaks = clean(peaks);
    this.#render();
  }
  resetPeaks() {
    this.#peaks = [...this.#levels];
    this.#expires = [0, 0];
    clearTimeout(this.#timer);
    this.#render();
  }
  #readAttributes() {
    const read = name => {
      const raw = this.getAttribute(name);
      return raw === null || raw.trim() === '' ? -Infinity : Number(raw);
    };
    this.levels = [read('left'), read('right')];
  }
  #schedulePeakExpiry() {
    clearTimeout(this.#timer);
    if (!this.isConnected) return;
    const now = performance.now();
    const pending = this.#expires.filter((expiry, i) => expiry > now && this.#peaks[i] > this.#levels[i]);
    if (!pending.length) return;
    this.#timer = setTimeout(() => {
      const time = performance.now();
      this.#peaks.forEach((_, i) => { if (time >= this.#expires[i]) this.#peaks[i] = this.#levels[i]; });
      this.#render();
      this.#schedulePeakExpiry();
    }, Math.max(1, Math.min(...pending) - now));
  }
  #render() {
    this.#channels.forEach((segments, channel) => {
      const thresholds = this.hasAttribute('layer-compact') ? [-60,-48,-36,-30,-24,-18,-12,-6,-3,0] : Array.from({length:21},(_,i)=>-60+i*3);
      const peakIndex = thresholds.findLastIndex(db=>this.#peaks[channel]>=db);
      segments.forEach((led, index) => {
        const db=thresholds[index];
        led.style.setProperty('--zone', `var(--audio-meter-${db >= -3 ? 'red' : db >= -12 ? 'yellow' : 'green'})`);
        led.classList.toggle('lit', db!==undefined && this.#levels[channel] >= db);
        led.classList.toggle('peak', peakIndex >= 0 && peakIndex === index);
      });
    });
    const text = this.#levels.map((value, i) => `${i ? 'R' : 'L'}: ${value < -60 ? 'silêncio' : value.toFixed(1) + ' dBFS'}`).join('; ');
    this.shadowRoot.querySelector('.meter').setAttribute('aria-label', text + (this.hasAttribute('muted') ? '; mute activo' : ''));
  }
}
if (!customElements.get('audio-level-meter')) customElements.define('audio-level-meter', AudioLevelMeter);

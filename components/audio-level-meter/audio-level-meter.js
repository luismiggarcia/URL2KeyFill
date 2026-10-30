/** Isolated display component. Levels are supplied by the host in dBFS. */
export class AudioLevelMeter extends HTMLElement {
  static observedAttributes = ['orientation', 'compact', 'muted', 'left', 'right'];
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
      .meter { display:flex; width:max-content; gap:var(--audio-channel-gap); padding:var(--audio-meter-padding); background:var(--audio-meter-background); border-radius:3px; }
      .channel { display:flex; flex-direction:column-reverse; gap:var(--audio-led-gap); }
      .led { flex:none; width:var(--audio-led-size); height:var(--audio-led-size); border-radius:1px; background:var(--audio-meter-off); }
      .lit { background:var(--zone); opacity:.82; }
      .peak { background:var(--zone); opacity:1; box-shadow:0 0 3px var(--zone); }
      :host([orientation="horizontal"]) .meter { flex-direction:column; }
      :host([orientation="horizontal"]) .channel { flex-direction:row; }
      :host([compact]) { --audio-led-size:2px; --audio-led-gap:1px; --audio-channel-gap:2px; --audio-meter-padding:4px; }
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
      const peakIndex = Math.floor((this.#peaks[channel] + 60) / 3);
      segments.forEach((led, index) => {
        led.classList.toggle('lit', this.#levels[channel] >= -60 + index * 3);
        led.classList.toggle('peak', peakIndex >= 0 && peakIndex === index);
      });
    });
    const text = this.#levels.map((value, i) => `${i ? 'R' : 'L'}: ${value < -60 ? 'silêncio' : value.toFixed(1) + ' dBFS'}`).join('; ');
    this.shadowRoot.querySelector('.meter').setAttribute('aria-label', text + (this.hasAttribute('muted') ? '; mute activo' : ''));
  }
}
if (!customElements.get('audio-level-meter')) customElements.define('audio-level-meter', AudioLevelMeter);

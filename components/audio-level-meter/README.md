# Controlo isolado de nível de áudio

Componente visual autónomo, sem dependências, acesso ao áudio ou ligação à aplicação. Não inclui faders, botões, escalas ou painéis. O Shadow DOM isola os estilos.

```html
<script type="module" src="./audio-level-meter.js"></script>
<audio-level-meter left="-21" right="-12"></audio-level-meter>
<audio-level-meter compact></audio-level-meter>
<audio-level-meter orientation="horizontal"></audio-level-meter>
```

```js
const meter = document.querySelector('audio-level-meter');
meter.levels = [-18, -9]; // L / R em dBFS; -Infinity significa silêncio
meter.toggleAttribute('muted', true); // representação cinzenta, conserva a leitura
meter.resetPeaks();
```

21 segmentos quadrados: −60 a 0 dBFS, em passos de 3 dB. Verde abaixo de −12, amarelo de −12 a −3, vermelho desde −3. Pico retido durante 1 segundo. L à esquerda / em cima; R à direita / em baixo. Os valores acima de 0 ficam limitados a 0; valores não finitos representam silêncio. A escala é linear em dB, independente da escala de ganho dos faders.

Dimensões por omissão (largura × altura): vertical 35 × 242 px; horizontal 242 × 35 px; compacto vertical 14 × 70 px. Podem ajustar-se com as variáveis CSS `--audio-led-size`, `--audio-led-gap`, `--audio-channel-gap` e `--audio-meter-padding`. As restantes variáveis `--audio-meter-*` definem as cores e o fundo.

A integração futura deverá fornecer os níveis e definir os locais de montagem. Nenhuma integração foi efectuada. Abrir `preview.html` através de um servidor HTTP local para experimentar (os módulos JavaScript podem ser bloqueados em URLs file://).

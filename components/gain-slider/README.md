# Slider de ganho autónomo

Cópia isolada do slider horizontal e do fader vertical de `assets/interface.html`. Conserva a pista, o puxador, as cores, o intervalo linear de ganho 0–2 e o duplo clique para repor 0 dB. O máximo exacto é +6,0206 dB. A escala apresenta +6 dB como na aplicação.

O Shadow DOM isola o CSS. As marcas são calculadas a partir do ganho linear. Acrescentou-se suporte visual Firefox e mantém-se o foco nativo para utilização por teclado.

```html
<script type="module" src="./gain-slider.js"></script>
<gain-slider value="1" label="Ganho do layer"></gain-slider>
<gain-slider orientation="vertical" value="1"></gain-slider>
```

API: `value` (ganho linear), `decibels`, `disabled`. Atributos: `value`, `orientation`, `disabled`, `label`. Eventos `input` e `change`, com propagação; ler o valor no próprio controlo. Alterações programáticas não emitem eventos. O tamanho horizontal pode definir-se no elemento via CSS (`width`).

Não controla áudio, não persiste valores e não comunica com a aplicação. Demonstração conjunta em `../preview.html`, através de um servidor HTTP local.

Os marcadores da escala são botões acessíveis por teclado e clique: definem o ganho indicado e emitem `input` e `change`. O afastamento perpendicular do traço ao eixo é igual nas duas orientações (7 px). Os traços mais espessos e o espaço até ao valor distinguem-nos do sinal menos tipográfico (−).

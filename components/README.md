# Biblioteca de controlos URL2KeyFill

A pasta `components/` é a fonte dos controlos partilhados. A aplicação incorpora uma cópia **gerada**, porque a WKWebView usa um único ficheiro local com uma política que não permite carregar módulos externos. Não editar os blocos `GENERATED` de `assets/interface.html`.

| Controlo | Fonte | Variantes |
| --- | --- | --- |
| Ganho | `gain-slider/gain-slider.js` | Slider horizontal e fader vertical |
| Nível de áudio | `audio-level-meter/audio-level-meter.js` | Vertical, horizontal, compacto, mono/estéreo e mute |
| Botões | `studio-button/studio-button.js` | Normal, quadrado, PLAY, AUDIO, DELETE, QUIT, POWER e LED |
| Geometria e LEDs dos botões | `studio-button/native.css` | Partilhados com os botões HTML da aplicação |
| Acabamento dos botões | `studio-button/graphite.css` | Grafite aprovado, legendas iluminadas e DELETE coral |
| Superfície e monitor | `studio-skin.css` | Fachada, rebaixo e barra de diagnósticos |

Os botões HTML da aplicação conservam os eventos e comandos nativos; a biblioteca fornece a geometria, o acabamento e os LEDs. O Web Component autónomo usa essas mesmas fontes com `skin="graphite"`. A variante clássica, em `studio-button/classic.css`, mantém o mostruário existente sem alterações de aspeto. São variantes de apresentação do mesmo componente.

## Utilização autónoma

```html
<script type="module" src="./components/index.js"></script>
<gain-slider value="1"></gain-slider>
<gain-slider orientation="vertical" value="1"></gain-slider>
<audio-level-meter layer-compact left="-18" right="-9"></audio-level-meter>
<studio-button skin="graphite" square variant="play" pressed>PLAY</studio-button>
<studio-button skin="graphite" led="red" toggle>MONO</studio-button>
<studio-button skin="graphite" variant="delete">DELETE</studio-button>
```

`index.js` exporta `GainSlider`, `AudioLevelMeter` e `StudioButton` e regista os três elementos. Não há dependências externas. Sem `skin="graphite"`, os botões mantêm o tema clássico do mostruário.

O ganho continua linear, de 0 a 2. O medidor recebe dBFS através de `setReading(levels, peaks)`; o adaptador mantém a calibração RMS e os comandos existentes. `layer-compact` mede 17 × 33 px e usa dez segmentos por canal. Em mono conserva o espaço do estéreo e centra o canal visível, também nas variantes grande e horizontal. O fundo acompanha apenas esse canal; a caixa de colocação mantém as dimensões estéreo.

## Atualizar e verificar

`library.json` enumera as fontes a incorporar. Depois de alterar um componente ou CSS:

```sh
python3 tools/sync-ui-controls.py
python3 tools/sync-ui-controls.py --check
```

O gerador atualiza `studio-button/styles.generated.js` e os blocos da interface. A compilação e o CTest verificam que estão sincronizados; não permitem compilar silenciosamente uma cópia antiga.

Os estilos de colocação específicos de cada painel continuam na interface. Os antigos desenhos de sliders e segmentos de medidor foram removidos: pertencem aos componentes, não aos painéis.

## Mostruário e testes

O mostruário mantém o seu conteúdo e aspeto. Para o servir, na raiz do projeto:

```sh
python3 -m http.server 8766 --bind 127.0.0.1 --directory components
```

Abrir http://127.0.0.1:8766/preview.html. Usa sinal simulado e não comunica com a aplicação.

Para testar a interface real sem dispositivos nem preferências:

```sh
python3 tools/preview-ui-controls.py
```

Abrir http://127.0.0.1:8767/interface.html e executar «Testar controlos». A ponte simulada só existe nesta ferramenta. Os testes verificam comandos de ganho, marcadores, mono/mute, dimensões, centramento e equivalência do acabamento entre botões HTML e autónomos.

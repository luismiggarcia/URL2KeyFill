# Colecção de controlos autónomos

Controlos sem dependências. Os sliders/faders e medidores são também usados pela aplicação; os botões mantêm-se autónomos. Cada pasta contém um Web Component com estilos isolados e a sua documentação.

- `audio-level-meter/`: medidor estéreo proposto, vertical, horizontal e compacto.
- `studio-button/`: botões normais, quadrados, âmbar, DELETE, QUIT LEDs verdes/vermelhos e POWER com símbolo iluminado a verde. Demonstração em `studio-button/preview.html`.
- `gain-slider/`: slider de ganho copiado da aplicação, horizontal e vertical.

`preview.html` demonstra ambos. Para abrir os módulos através de HTTP, executar na raiz do projecto:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory components
```

Abrir http://127.0.0.1:8765/preview.html. A demonstração usa apenas sinal simulado e não reproduz áudio. A demonstração não comunica com a aplicação.

## Integração na aplicação

`assets/interface.html` incorpora os componentes de áudio porque a interface WKWebView carrega um único ficheiro local, sem pedidos externos. Depois de alterar `gain-slider.js` ou `audio-level-meter.js`, executar:

```sh
python3 tools/sync-ui-controls.py
python3 tools/sync-ui-controls.py --check
```

O adaptador da aplicação mantém o ganho linear 0–2, a calibração RMS existente e os comandos nativos. A variante `layer-compact` usa 17 × 33 px, com 10 segmentos por canal. `mono` oculta o segundo canal; `muted` apresenta o sinal em cinzento. `setReading(levels, peaks)` recebe os níveis e picos já calibrados em dBFS pelo adaptador.

Para testar a interface real sem dispositivos nem preferências:

```sh
python3 tools/preview-ui-controls.py
```

Abrir http://127.0.0.1:8767/interface.html. Esta ponte simulada só existe na ferramenta de teste.

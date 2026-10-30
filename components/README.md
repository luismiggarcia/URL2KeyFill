# Colecção de controlos autónomos

Controlos sem dependências e sem integração na aplicação. Cada pasta contém um Web Component com estilos isolados e a sua documentação.

- `audio-level-meter/`: medidor estéreo proposto, vertical, horizontal e compacto.
- `studio-button/`: botões normais, quadrados, âmbar, DELETE, QUIT LEDs verdes/vermelhos e POWER com símbolo iluminado a verde. Demonstração em `studio-button/preview.html`.
- `gain-slider/`: slider de ganho copiado da aplicação, horizontal e vertical.

`preview.html` demonstra ambos. Para abrir os módulos através de HTTP, executar na raiz do projecto:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory components
```

Abrir http://127.0.0.1:8765/preview.html. A demonstração usa apenas sinal simulado e não reproduz áudio. Os ficheiros da aplicação não foram alterados.

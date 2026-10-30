# URL2KeyFill — logótipo e ícone

- `logo-normal.svg` / `logo-normal.png`: letras claras e dourado, para fundos escuros.
- `logo-fundos-claros.svg` / `logo-fundos-claros.png`: letras escuras e dourado com maior contraste, para fundos claros.
- Os logótipos têm fundo transparente. Os PNG têm 2400 px de largura; os SVG usam contornos vetoriais e não dependem de fontes externas.
- `app-icon.svg`: ícone quadrado vetorial com duas camadas de vídeo e monograma U2KF.
- `app-icon-*.png`: ícone de 16 a 1024 px, com transparência exterior aos cantos arredondados.
- `URL2KeyFill.icns`: ícone para aplicação macOS. É copiado para `Contents/Resources/URL2KeyFill.icns` durante a compilação e registado em `CFBundleIconFile`, para o Finder e o Dock.

Para regenerar no macOS, executar a partir da raiz do projeto:

```sh
swift -module-cache-path /tmp/url2keyfill-swift-cache assets/branding/generate.swift "$PWD/assets/branding"
```

O ficheiro ICNS é composto a partir dos PNG com a ferramenta `iconutil` do macOS.

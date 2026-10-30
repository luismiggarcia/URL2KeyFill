# Botões autónomos

Estilos copiados de `assets/interface.html`: botão normal (`sr-small-button`), quadrado (`sr-key`, 62 px), âmbar (`sr-amber`), DELETE (`sr-delete`), confirmação de saída (`sr-quit`), compacto (`sr-refresh`) e LEDs (`sr-led-button`). Sem dependências, estilos isolados em Shadow DOM.

```html
<script type="module" src="./studio-button.js"></script>
<studio-button led="green" toggle pressed>L</studio-button>
<studio-button led="red" toggle>MUTE</studio-button>
<studio-button square variant="power" label="Desligar">POWER</studio-button>
<studio-button square>PLAY</studio-button>
<studio-button square variant="amber">EDIT<br>LAYER</studio-button>
<studio-button>RELOAD</studio-button>
<studio-button variant="delete">DELETE</studio-button>
```

- `led="green|red"`: branco no estado inactivo, verde/vermelho quando `pressed` está presente. Cores, lente e brilho copiados da aplicação.
- `toggle`: alterna o estado local ao clicar e emite `change`. Sem este atributo, o estado é controlado externamente.
- `pressed` e `disabled`: atributos booleanos e propriedades JavaScript.
- `square`: formato quadrado; tamanho ajustável com `--studio-key-size`.
- `variant`: omitir para normal; `amber`, `delete`, `quit`, `refresh` ou `power`.
- `variant="power"` com `square`: símbolo POWER em SVG, iluminado a verde com brilho suave. O texto mantém a cor normal. Quando desactivado, o símbolo fica atenuado e sem brilho. Não alterna estado nem executa a saída da aplicação.
- `label`: nome acessível opcional, caso o conteúdo visível não seja suficiente.

Usa um botão nativo com teclado, foco e desactivação. O evento `click` propaga-se para o exterior. As alterações programáticas de estado não emitem `change`. O texto aceita quebras de linha e conteúdo através do slot.

RELOAD e DELETE são apresentações: não executam operações. Não se copiaram posicionamentos específicos dos painéis, ligações à aplicação nem a regra da aplicação que remove os contornos de foco. O símbolo POWER está incluído em SVG, sem bibliotecas externas.

Demonstração: `preview.html` através do servidor HTTP indicado no README da colecção.

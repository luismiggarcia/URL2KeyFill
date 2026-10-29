# Ligações e validação física

## Modelos distintos

| Equipamento | Finalidade nesta aplicação |
|---|---|
| UltraStudio **HD Mini** | Thunderbolt 3; duas saídas 3G-SDI com external Fill/Key em HD. Primeiro alvo de validação, 1920×1080. Não tem saída física UHD. |
| Antigo UltraStudio **Mini Monitor** | Produto distinto, antigo; não presumir que as capacidades do modelo 12G se aplicam. Apenas modos/píxeis/keying que o SDK confirme. |
| UltraStudio **Mini Monitor 12G** | Produto novo, Thunderbolt 4; especificações oficiais indicam duas saídas, Fill e Key, e UHD até 2160p60. Ainda não testado com esta aplicação. |

As fichas oficiais não substituem `DoesSupportVideoMode` para o perfil, formato de píxel e frequência selecionados. A lista da aplicação é limitada aos formatos que o backend implementa.

## Preparação Full HD — HD Mini

1. Instalar/autorizar Desktop Video, reiniciar se necessário e confirmar que **Desktop Video Setup** reconhece o HD Mini. Usar cabo Thunderbolt 3 apropriado. Fechar Media Express, Resolve ou outra aplicação que possa ocupar a saída.
2. Ligar **SDI OUT A → entrada Fill** da mesa e **SDI OUT B → entrada Key**. São duas ligações 75 Ω independentes; não ligar a SDI IN. A e B transportam imagem e máscara sincronizadas geradas pelo keyer a partir de um único fotograma com alfa.
3. Na mesa, escolher um keyer externo linear/luma (por exemplo, downstream key), com essas entradas como Fill e Key; Key não invertida. Não ativar chroma key. Confirmar que ambos os sinais e a mesa usam o mesmo formato e frequência exatos. 29,97 não é 30 e 59,94 não é 60.
4. Para 3G-SDI, configurar Level A/B em Desktop Video Setup de acordo com a mesa. Desativar conversões de saída, interpretação PsF e opções dual-link de imagem que sejam incompatíveis com duas saídas Fill/Key. A aplicação não altera essas configurações persistentes do utilizador nem o perfil automaticamente. Confirmar SDI como ligação de saída no Setup quando selecionável.
5. Se o sistema exigir genlock, distribuir referência compatível para a mesa e para REF IN do UltraStudio (tri-level/black burst conforme o modo). Cabos de comprimentos equivalentes ajudam a evitar diferenças de caminho. Confirmar lock/sincronismo no equipamento; a aplicação não mede a referência externa.
6. Depois de reiniciar, abrir `build/URL2KeyFill.app`, usar **SETUP → REFRESH**, selecionar HD Mini e um modo **1920×1080 progressivo** marcado **Fill/Key**, começando por 25 ou 30 fps suportado. Selecionar **BROWSER SIZE → Match SDI output** e a layer de teste, e premir **PLAY**.
7. Observar A e B individualmente antes de colocar a composição no ar: A deve mostrar Fill e B uma máscara em cinzento. Confirmar os conectores na unidade real. Uma janela de preview não comprova isto.

## Alfa, contornos e níveis

A origem CEF é pré-multiplicada; a aplicação entrega RGB com alfa independente ao SDK, conforme o exemplo oficial KeyerOutput. Começar com a mesa em Key externa linear e confirmar a opção **premultiplied/pre-multiplied key** pelos sinais físicos. A documentação do SDK não basta para afirmar que o Fill físico tem o mesmo tratamento que o buffer.

Usar um fundo da mesa de cor diferente da página. Testar zonas transparentes (fundo intacto), opacas, 25/50/75%, gradientes, sombras e texto. Se os contornos ficarem escuros, pode haver dupla multiplicação; se ficarem claros, pode faltar multiplicação. Não “corrigir” com clip/gain a ponto de destruir a transparência parcial. Registar a opção correta para o equipamento e a mesa usados. Medir preto/branco e cromas em waveform/vectorscope Rec.709; conferir níveis legais e ausência de clipping. A página é RGB8; a saída SDI de 10 bits não recupera precisão perdida.

## Protocolo de aceitação Full HD

Preencher para **cada modo efetivamente anunciado** que se pretenda utilizar:

| Ensaio | Evidência/critério |
|---|---|
| Identificação | Modelo, ID, driver, perfil, formato, frequência racional, pixel format e mesa registados. |
| Fill + Key | A e B presentes simultaneamente, corretamente encaminhados e sincronizados. |
| Proporções/orientação | Quatro cantos, círculo, texto e limites sem inversão ou deformação. |
| Alfa | Transparência 0/25/50/75/100%, sombra e antialiasing sem halos. |
| Cadência | Animação ≥10 minutos; anotar fps CEF e SDI, atrasados/perdidos e observação externa. Repetir 23,976/29,97/59,94 quando anunciados, além dos modos inteiros. |
| Imagem estática | Carregar uma página estática; SDI deve continuar sem interrupção mesmo quando CEF deixa de emitir paints. |
| Fora do ar | Retirar do ar: Key preta após os frames já agendados, SDI permanece locked; voltar ao ar recupera a imagem. |
| Parar/reiniciar | Repetir dez vezes; formato permanece correto, sem erros de recurso ocupado. |
| Alterar formato | Parar, selecionar outro modo, carregar/iniciar; nova cadência correta. |
| Falha do URL | URL inacessível: erro visível e novos frames transparentes. |
| Desligar/religar | Desligar Thunderbolt durante teste; erro/fim controlado, sem crash. Religar, Refresh e PLAY manualmente. |
| Ocupação | Outra aplicação ocupa a saída: mensagem clara e aplicação continua utilizável. |
| Encerramento | Fechar durante reprodução; outro programa consegue abrir a saída depois. |

Os contadores SDI vêm dos callbacks de reprodução; não são contadores de fotogramas efetivamente medidos no conector. Guardar também observação/captura externa. O backend sem external keying pode reproduzir imagem e mostrar máscara local, mas não satisfaz o ensaio Fill+Key físico.

## UHD futuro

Após adquirir equipamento compatível (por exemplo, o **novo Mini Monitor 12G**), instalar driver compatível, verificar Thunderbolt 4/5 conforme as especificações, usar cablagem 12G e mesa/monitor UHD. Repetir o protocolo com um modo **3840×2160 anunciado com Keying**. Medir desempenho progressivamente até à frequência pretendida; não há promessa de UHD60 nesta implementação CPU.

Renderizar UHD e reduzir para 1920×1080 no HD Mini testa apenas a redução e a saída HD. Não validar UHD com esse ensaio. Confirmar também se o equipamento futuro oferece BGRA8/ARGB8 para Keying; formatos adicionais exigem implementação própria.

## Fontes oficiais consultadas em 28/10/2026

- [UltraStudio e HD Mini — especificações](https://www.blackmagicdesign.com/products/ultrastudio/techspecs)
- [UltraStudio Mini Monitor 12G — especificações](https://www.blackmagicdesign.com/products/ultrastudiomini/techspecs)
- [SDK e versões](https://www.blackmagicdesign.com/developer/products/capture-and-playback/sdk-and-software)
- [Keying no SDK](https://sdk-doc.blackmagicdesign.com/decklink-sdk/HighLevel/keying.html)
- [IDeckLinkKeyer](https://sdk-doc.blackmagicdesign.com/decklink-sdk/DeckLinkAPI/interfaces/idecklinkkeyer.html)
- [Reprodução e validação de modos](https://sdk-doc.blackmagicdesign.com/decklink-sdk/DeckLinkAPI/interfaces/idecklinkoutput.html)
- [Acesso a buffers na API atual](https://sdk-doc.blackmagicdesign.com/decklink-sdk/DeckLinkAPI/interfaces/idecklinkvideobuffer.html)
- Headers e exemplos **KeyerOutput** e **SignalGenerator** da cópia oficial do SDK 16.0 colocada pelo utilizador em `third_party/DeckLinkSDK`.
- Headers e wrapper da distribuição CEF fixada; [exemplo oficial macOS](https://github.com/chromiumembedded/cef/blob/master/tests/cefsimple/cefsimple_mac.mm).


## Áudio estéreo — versão 0.2

A aplicação incorpora o áudio das páginas em SDI, a 48 kHz, estéreo, PCM de 32 bits entregue ao SDK. Validar no recetor os canais 1/2, nível, ausência de distorção e sincronismo com a imagem. A layer oculta fica silenciosa e PAUSE silencia a composição mantendo SDI. O áudio já agendado pode escoar por alguns fotogramas antes de ficar silencioso. Usar um URL de teste audiovisual conhecido e uma captura/mesa com medição; o contador de amostras agendadas não substitui a escuta ou o loopback físico.

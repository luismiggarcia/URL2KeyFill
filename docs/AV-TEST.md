# Diagnóstico de vídeo e áudio

As camadas da app não recebem cliques nem teclado. Todos os exemplos deste grupo arrancam e repetem automaticamente; não contêm botões, menus nem atalhos. A escolha faz-se em **EDIT LAYER → URL → Diagnostics**. **RELOAD** reinicia o teste. Mute, ganho, MONO e monitorização são controlados pela app.

## Exemplos disponíveis

| Asset em Diagnostics | Comportamento |
| --- | --- |
| AV Test | Ciclo completo de 96 s, com identificação, próxima etapa e contagem decrescente |
| 01 Barras RGB | Branco, amarelo, ciano, verde, magenta, vermelho, azul e preto; silêncio |
| 02 Escala de Cinzentos | 16 níveis; silêncio |
| 03 Geometria e Movimento | Grelha, círculo, linhas de um píxel e marcador em movimento; silêncio |
| 04 Transparencia Key | Fundo transparente com zonas alfa 0%, 25%, 50%, 75% e 100%; silêncio |
| 05 Audio Esquerdo | Sinusóide de 1 kHz apenas em L |
| 06 Audio Direito | Sinusóide de 1 kHz apenas em R |
| 07 Audio Estereo | Sinusóide de 1 kHz em L e R, em fase |
| 08 Audio Oposicao de Fase | Sinusóide de 1 kHz, L = −R; cancela em MONO |
| 09 Audio Varrimento | Varrimento logarítmico de 20 Hz a 20 kHz em 16 s, nos dois canais |
| 10 Sincronismo AV | Flash e impulso de 1 kHz, com 40 ms, a cada segundo |
| 11 Silencio Digital | Zeros nos dois canais, com imagem em movimento |

O ciclo automático segue a ordem: barras → L → R → estéreo → oposição de fase → cinzentos → geometria → transparência → varrimento → sincronismo → silêncio. Cada etapa dura 8 segundos, exceto o varrimento, com 16 segundos. Os testes individuais repetem a sua única etapa. Não é necessário editar ficheiros para escolher um teste.

O endereço principal mantém-se: `u2k://Diagnostics/AV_Test.html`. Cada teste individual usa o nome do respetivo HTML, por exemplo `u2k://Diagnostics/05_Audio_Esquerdo.html`. A app descobre estes ficheiros no arranque, depois de incluídos no pacote por compilação/instalação.

## Verificação no recetor

1. Carregar uma única camada de teste visível, com ganho a 0 dB, canais L/R ativos, MONO desligado e mute da camada e master desligados. Isolar outras camadas para não somar sinais.
2. Iniciar PLAY e observar/escutar o recetor SDI. Nos testes L e R, confirmar canal e ausência de estalidos. No teste em oposição de fase, MONO deve cancelar o tom. Os medidores da app devem indicar aproximadamente −18 dBFS nos canais ativos dos tons contínuos; o RMS digital convencional destes tons é −21,01 dBFS.
3. Comparar o flash e o impulso no recetor para avaliar sincronismo. Partilham o relógio Web Audio; a apresentação visual depende dos frames do browser e do dispositivo. Um flash de 40 ms pode ocupar apenas um frame a 25/30 FPS.
4. Verificar detalhe nos cinzentos, proporções do círculo, margens e continuidade do movimento. A moldura exterior marca os limites; a tracejada está a 5%. As linhas de um píxel revelam reamostragem.
5. No teste de transparência, comparar FILL e KEY, idealmente sobre um fundo no misturador. As legendas, a identificação e as molduras são opacas; o restante fundo é transparente.

Todos os sons usam −18 dBFS de pico por canal, com transições suaves. O varrimento inclui frequências pouco audíveis: manter um nível de escuta adequado. Silêncio digital permite procurar ruído a jusante. Ocultar/voltar a mostrar a camada não reinicia o seu relógio: usar RELOAD para começar do início.

## Indicadores e limites

A indicação distingue **SILÊNCIO DIGITAL · intencional** de **SINAL PREVISTO**, incluindo os canais usados. O estado do motor, por si só, não é apresentado como prova de som. A linha **Fonte medida** apresenta RMS convencional e pico em dBFS de cada canal, medidos por Web Audio depois do gerador e antes da app/SDI (janela de 2048 amostras). Silêncio lê −∞; um tom de −18 dBFS de pico lê cerca de −21 dBFS RMS. Nos impulsos, os valores alternam com o silêncio. Esta medição não confirma a captura pela app nem a entrega ao recetor: comparar com os medidores da app e o equipamento de saída.

A página mostra a taxa de amostragem aceite pelo contexto, as dimensões do canvas, FPS de `requestAnimationFrame` e o maior intervalo entre callbacks. Estes valores descrevem o gerador/browser; não medem frames SDI perdidos ou qualidade elétrica do sinal. As barras são RGB a 100% em sRGB, não uma mira SMPTE/EBU calibrada. O canvas acompanha a janela e o device pixel ratio.

O áudio é pré-gerado a 48 kHz num buffer estéreo e repete sem depender de temporizadores JavaScript para mudar de etapa. Imagem e som seguem o mesmo relógio enquanto o áudio está ativo. Se o áudio falhar ou o autoplay estiver bloqueado, a imagem continua a funcionar e apresenta o estado; o flash de referência AV fica desativado. Num browser externo, a política de autoplay pode bloquear o som; o alvo destes assets é o CEF da app, configurado para reprodução automática.

Não existem exportação ou outros controlos dentro da camada. O gerador não mede THD, SNR, loudness ou atraso físico AV: confirmar esses valores com captura ou instrumentação no recetor.

## Manutenção

Cada HTML é autónomo, sem rede ou ficheiros auxiliares. Para evitar divergências entre exemplos, a fonte comum está em `tools/av-test/template.html`. Depois de a alterar, executar `python3 tools/av-test/generate.py`, recompilar e instalar a app. O teste de integração da app verifica o catálogo Diagnostics e o carregamento com imagem e som do exemplo automático no CEF, sem interação.

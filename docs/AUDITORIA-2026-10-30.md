# Auditoria URL2KeyFill 0.4.0 — 30/10/2026

Âmbito: código em `development/v0.2` (commit `c7f0558`, versão 0.4.0), com foco no
roteamento de áudio. Sem hardware DeckLink ligado durante a auditoria
(`decklink_probe`: *No compatible SDI output device detected*), por isso a saída SDI
física não foi testada; o comportamento do callback DeckLink foi analisado no código e
simulado com as classes reais do mixer.

> **Como ler as respostas inline:** o texto original da auditoria foi preservado; os blocos “Resposta da revisão” registam a verificação independente, as ressalvas e as soluções revistas. Nos pontos A1–A10 e V1–T1, as linhas das tabelas foram apresentadas como entradas individuais para colocar a resposta imediatamente junto de cada achado.

## 1. O que foi verificado

| Verificação | Resultado |
|---|---|
| `core_tests` e `scene_tests` recompilados a partir do código atual | PASS (2/2) |
| `--smoke-test` da build Release atual (UI, composição, persistência, captura de áudio, lower third, shutdown) | PASS, exit 0 |
| Medição real dos pacotes de áudio CEF (cópia instrumentada da app no scratchpad, 65 s, 6549 pacotes) | Ver §3.1 |
| Simulação do callback de áudio DeckLink com `AudioLane`/`AudioMixer` reais, com e sem deriva de relógio | Ver §3.2 |
| Testes dirigidos ao mixer (mono, L/R, clipping, medidores, sobreposições) | Ver §4 |
| Leitura de `Main.mm`, `DeckLink.cpp`, `Browser.*`, `Scene.*`, `Core.*`, `interface.html` | Ver §4–§5 |

O repositório não foi alterado; os binários instrumentados e as simulações ficaram
apenas no scratchpad da sessão.

> **Resposta da revisão — validação independente:**
>
> - `core_tests` e `scene_tests` recompilados com Clang/C++20, diretamente das fontes: ambos passaram. CMake não está no PATH desta sessão; não se apresentam estes resultados como uma execução nova de CTest.
> - Harness temporário ligado às classes reais: confirmou medição truncada a 1,0 com sinal interno 1,6; 480 amostras intercaladas cortadas num bloco de 480 frames com apenas L acima do limite; MONO de L=0,8/R desligado produz 0,4; sobreposição de 1 ms soma o sinal; mixer desativado cala `sample()` mas mantém o medidor pré-mute.
> - `build/decklink_probe`: sem dispositivo SDI compatível detetado. É o binário existente, não uma recompilação nova do backend.
> - Não se repetiram o smoke test gráfico, os 65 s instrumentados, as simulações originais de deriva nem testes físicos de áudio/vídeo. Os ficheiros e dados brutos dessas medições não acompanham a auditoria. Percentagens e tempos resultantes dessas experiências são evidência relatada, não reproduzida nesta revisão.
> - A análise de riscos de Core Audio é estática; não demonstra cortes efetivamente audíveis ou falha com todos os dispositivos.

> **Evidência reproduzível (acrescentada a 30/10):** os traces, o simulador e o analisador estão em [tools/audio-audit](../tools/audio-audit/README.md). `run-models.sh` reproduz em ~10 s as verificações do mixer e os cenários de deriva sem CEF nem hardware. Foi capturado um segundo trace de 120 s (11 743 pacotes) com o PTS bruto e quatro relógios. Os números das secções 3.1–3.3 abaixo foram revistos com esta evidência.

## 2. Mapa do roteamento de áudio

```
CEF (por layer) ── OnAudioStreamPacket ──► AudioLane
                                           ├─ packets_      (consumida pelo SDI, com prune)
                                           └─ meterPackets_ (só leitura: medidores e monitor)

packets_ ──► AudioMixer "mixer" .mix()     ──► DeckLink ScheduleAudioSamples (SDI)
meterPackets_ ──► mixer.sample(respectMute=false) ──► medidor composto (OUTPUT)
meterPackets_ ──► lane.sourceLevels()      ──► medidor de cada layer
meterPackets_ ──► mixer.sample()           ──► Monitor local "OUTPUT"   (AVAudioEngine)
meterPackets_ ──► monitorMixer.sample()    ──► Monitor local de layers (AVAudioEngine)
```

### 2.1 Matriz: que controlo afeta que destino

| Controlo | SDI | Medidor composto | Medidor da layer | Monitor OUTPUT | Monitor de layer |
|---|---|---|---|---|---|
| Visibilidade da layer | sim (oculta = silêncio) | sim | **não** (pré-fader) | sim | **não** |
| MUTE da layer | sim | sim | não | sim | **não** |
| Ganho da layer (0–2) | sim | sim | não | sim | **não** (fixo a 1) |
| L / R da layer (seleção de entrada) | sim | sim | sim | sim | sim |
| MONO da layer | sim | sim | sim | sim | sim |
| Ganho / MONO / L / R de OUTPUT | sim | sim | não | sim | não |
| MUTE master | sim | não (fica a cinzento) | não | sim | não |
| PAUSE | sim (após ~3 frames já agendados) | não (cinzento) | não | sim | não |
| SDI parado | sem saída | continua | continua | continua | continua |

Confirmado no código: o roteamento corresponde ao descrito no README, com estas
nuances que não estão documentadas:

- O monitor de layer é efetivamente **pré-fader e pré-mute** (PFL), mas
  **pós-seleção L/R e pós-MONO** (`Main.mm:110`). Quem silencia uma layer continua a
  ouvi-la no monitor; o botão chama-se apenas "MONITOR".
- Os interruptores L/R têm **semânticas diferentes** na layer e no OUTPUT: na layer são
  seleção de entrada antes do MONO; no OUTPUT são mute de canal de saída depois do MONO
  (`Scene.cpp:33` vs `Scene.cpp:47`).
- Na gaveta AUDIO, a faixa de uma layer **oculta** não mostra que está silenciada no
  programa (só o MUTE altera o aspeto da faixa) — fader alto, MUTE desligado, e nada
  sai no SDI.

> **Resposta da revisão — roteamento:** A matriz de roteamento da auditoria corresponde ao código com estas condições: “medidor composto” é a amostragem pré-master-mute/pré-PAUSE do mixer, enquanto os medidores por layer são pré-fader; o comportamento após STOP tem a exceção A1. PAUSE escoa buffers já agendados; “~3 frames” é uma aproximação dependente da reserva, do instante do comando e da fase do vídeo.

## 3. Achados críticos — sincronização do áudio SDI

Estes três pontos têm a mesma raiz: o áudio do CEF chega no relógio do Mac, o SDI
consome no relógio do DeckLink, e a ligação entre os dois é feita uma única vez, com
margem quase nula e sem correção. O teste de hardware existente (≈5 s a 1080p25,
critério "> 4800 amostras não nulas") não consegue detetar nenhum deles.

> **Resposta da revisão — distinguir atraso, desfasamento e cortes:**
>
> 1. **Atraso total:** tempo entre um acontecimento na página e a saída. A reserva SDI é calculada a partir de três frames: 120 ms a 25p, 60 ms a 50p e cerca de 50 ms a 60p. Esses valores são o alvo de áudio agendado, não uma medição da latência completa. O vídeo também usa três frames SDK, com idade variável da imagem que foi copiada para cada frame.
> 2. **Desfasamento A/V:** diferença entre as apresentações de som e imagem do mesmo acontecimento. Os frames `Frame` não transportam timestamp de apresentação: `sequence` apenas os ordena. O SDI usa a imagem mais recente, enquanto o áudio é lido por timestamps. Há timeline comum de agendamento, mas não associação explícita de conteúdo A/V. Corrigir áudio isoladamente não garante lip sync.
> 3. **Jitter/cortes:** pacotes ainda não disponíveis quando pedidos ou callbacks que falham prazos. Uma margem pequena resolve a disponibilidade dentro de um orçamento medido; não corrige deriva indefinida.
> 4. **Monitor local:** `Main.mm:181` subtrai **4800 amostras = 100 ms** ao relógio, antes da latência de AVAudioEngine/dispositivo. O cursor depois avança pelo relógio de reprodução local e só é reposto com erro superior a 100 ms. Pode acumular deriva e recuperar por salto; o timestamp de hardware do callback é ignorado. Não é uma referência fiável para avaliar o sincronismo do SDI.
> 5. **Medidores:** analisam 300 ms de história; essa janela suaviza a leitura e não acrescenta 300 ms ao som SDI. O custo de varrer os buffers e bloquear mutexes pode, contudo, causar contenção.

> **Resposta da revisão — remover processamento nos casos comuns:**
>
> O código atual não contém reamostrador adaptativo, compressor, limitador com lookahead ou cadeia DSP pesada que se possa simplesmente desligar. Há mistura, cópias, conversão float→int32, medição e buffers. As oportunidades concretas são:
>
> | Situação | Percurso proposto | Limite/benefício esperado |
> |---|---|---|
> | Uma única fonte audível, estéreo, ganhos unitários, sem MONO | Selecionar diretamente a lane; leitura alinhada e conversão PCM numa passagem, sem buffers intermédios de soma nem multiplicações neutras. | Menor CPU, alocações e contenção. Mantém timestamps, proteção de nível e controlo de relógio; não promete menos N ms por si só. |
> | Nenhuma fonte audível | Bloco de silêncio pré-alocado, mantendo agendamento ativo e métricas coerentes. | Evita percorrer lanes e misturar; preservar captura se houver PFL/medidores ou necessidade de retoma. |
> | Uma única layer de imagem visível, já no tamanho de saída | Publicar a referência imutável dessa imagem, sem composição sobre um frame transparente nem resize. | Elimina uma passagem completa de imagem; conservar conversão de saída e semântica de alpha. |
> | Imagem nova disponível | Notificar composição em vez de polling a 16 ms; coalescer atualizações. | Retira uma espera variável e reduz idade do vídeo. Revalidar ajuste A/V depois. |
> | Tamanho do browser igual ao SDI | Conservar percurso sem resize, que já existe após a composição. | Não apresentar como otimização nova aquilo que já é evitado. |
> | Áudio com ajustes/múltiplas fontes | Mixer geral com buffers reutilizados e instantâneos de configuração fora do callback. | Custos proporcionais às fontes realmente ativas; transições entre percursos sem saltos. |
> | Monitor local desligado | Parar o motor e não produzir blocos de monitor. | Reduz trabalho concorrente. Não precisa de deixar de capturar áudio para SDI/medidores. |
> | Gaveta/medidores fora de vista | Reduzir cálculo de RMS/serialização de UI; manter apenas os indicadores visíveis. | Menos trabalho e mutexes concorrentes; não altera o orçamento temporal SDI. |
> | Fader/mute/monitor | Atualizar só áudio, sem recomposição nem escrita imediata de sessão. | Correção V3 com impacto transversal no jitter sob interação. |
>
> O percurso simples deve ser automático e produzir o mesmo resultado/roteamento do mixer geral. Uma fonte continua a ligar dois relógios independentes: não se deve desligar a correção de deriva só porque não há mistura. Pode existir razão 1:1 enquanto o erro está dentro de uma banda estável, retomando correção suavemente, sem acumular erro ilimitado. Reamostragem linear não é automaticamente transparente por a razão ser próxima de 1; medir resposta/aliasing e escolher implementação com qualidade adequada.

### 3.1 [CRÍTICO] O SDI lê áudio "do presente" — quase sem margem para jitter

Medição real (65 s, layer de teste incorporada):

| Métrica | Valor |
|---|---|
| Δpts entre pacotes | 10 ms em 6548/6548 transições (sem buracos nem sobreposições) |
| Chegada do pacote face ao seu timestamp de início | +1,4 ms (p50); pior caso: pacote atrasado ~16 ms (intervalo máximo entre chegadas 25,8 ms) |
| Deriva `system_clock` vs relógio monótono | 0,7 ms em 65 s (≈10 ppm — o `system_clock` é ajustado pelo NTP) |
| Deriva pts CEF vs relógio monótono | −2,4 ppm (dentro da quantização de 1 ms → é o mesmo relógio) |

No `AudioCallback` (`DeckLink.cpp:60-71`), o pedido ao mixer é
`mix(epoch_ + cursor_ - target_, count)`. Depois de encher o buffer até `target_`,
o fim da janela pedida é sempre `epoch_ + amostras_já_tocadas` ≈ **agora − δ**, em que
δ é só o tempo entre a construção do callback e o início real do consumo pelo
dispositivo. O preroll de "3 frames" acrescenta latência ao buffer do dispositivo,
mas **não dá margem nenhuma à fila de origem**: o fim pedido não depende de `target_`.

Como os pacotes chegam ~1,4 ms depois do seu timestamp, com δ < ~1,4 ms (ou com o
driver a puxar um bloco para o FIFO de hardware no arranque) faltam amostras no fim de
cada bloco. Simulação com relógios perfeitos: δ = 0,5 ms → 0,36 % de amostras em falta
(clicks); δ = 1 ms → 0,06 %; δ ≥ 2 ms → 0 %. Qualquer pico de carga no CEF (atraso
observado de ~16 ms) produz um buraco.

> **Resposta da revisão — 3.1:** **Confirmado no algoritmo; valores experimentais por reproduzir.** `AudioCallback` fixa `epoch_` e lê `epoch_+cursor_-target_`. Em regime, ao completar o enchimento, o extremo da leitura aproxima-se de `epoch_+tempo consumido`. Aumentar `target_` aumenta o áudio agendado mas não garante maior margem perante a chegada dos pacotes. O intervalo entre construir o callback e começar a reprodução também contribui para a margem real; não se pode afirmar que seja sempre quase zero.
>
> **Solução revista:** Medir chegada, cobertura e margem por fonte; definir atraso de origem explícito e limitado, separado da reserva no dispositivo. Dimensionar a margem a partir do jitter medido. 40 ms é um valor inicial de ensaio, não uma garantia.

> **Evidência reproduzível — 3.1:** o trace de 120 s confirma a regularidade (Δpts = 10 ms em todos os pacotes, sem sobreposições nem lacunas). A chegada face ao início do pacote dá p50 0,95 ms e p99 1,60 ms, mas houve um pacote 30,7 ms atrasado; com 20 ms de margem ainda falham 2 pacotes, com 40 ms nenhum. No simulador com estes atrasos medidos e relógios perfeitos: margem 0,5 ms → 0,178 % de amostras em falta; 2 ms → 0,024 %; 40 ms → 0 %. Os 40 ms continuam a ser um valor de ensaio: dois traces de 1–2 min num Mac sem carga não chegam para dimensionar o pior caso.

### 3.2 [CRÍTICO] Sem compensação da deriva entre o relógio DeckLink e o do Mac

O CEF produz 48 000 amostras por segundo do Mac; o DeckLink consome 48 000 por segundo
do seu cristal. A diferença acumula-se diretamente na posição de leitura. Simulação com
o mixer real (δ = 3 ms):

| Dispositivo vs Mac | Efeito |
|---|---|
| 0 ppm | estável |
| +20 ppm (mais rápido) | começa a perder amostras aos **~9–10 min**; 23 % em falta aos 11 min, 69 % aos 15 min (crepitação progressiva e depois quase silêncio) |
| −20 ppm (mais lento) | atraso a crescer **+72 ms/hora** (A/V a dessincronizar); **silêncio total ao fim de ~7 h** (sai da janela de 500 ms) |

±20 ppm é uma tolerância normal de cristal. Não há qualquer mecanismo (reamostragem
adaptativa, insert/drop, ressincronização) que corrija isto. O contador
"Audio buffer overruns" só deteta o caso lento, e só muito tarde.

> **Resposta da revisão — 3.2:** **Confirmada a ausência de controlo; resultados numéricos condicionais.** Não há medição de razão de relógios nem reamostragem. 20 ppm equivale matematicamente a 72 ms/h. Os 9–10 min, as percentagens de silêncio e as ~7 h dependem da margem inicial, dos blocos e da simulação. ±20 ppm não é uma especificação comprovada deste equipamento.
>
> **Solução revista:** Relacionar tempo de stream/hardware com tempo monótono; estimador filtrado de razão e fase, reamostragem com estado e limites, recuperação explícita de descontinuidades. Testar ambos os sentidos e suspensão. Uma margem fixa apenas adia a falha.

> **Evidência reproduzível — 3.2 (corrige a tabela acima):** o primeiro simulador chamava o callback a cada 10 ms exatos, em fase com a chegada dos pacotes, e isso escondia as falhas. Com período de 9,7 ms (a fase varre todas as posições), um dispositivo +20 ppm com margem de 3 ms começa a perder amostras quando a deriva ultrapassa margem − atraso de chegada (≈1,6 ms, **~80 s**): 4,5 % em falta aos 5 min, 41 % aos 10 min, 87 % aos 15 min. O caso −20 ppm mantém-se: atraso a crescer 72 ms/h, falhas a partir de ~7 h, quase silêncio às 8 h. Continua por medir o erro real do HD Mini. A inclinação do "source headroom" dá esse erro em poucos minutos (ms/h = ppm × 3,6), por isso o ensaio de 2 h chega para o diagnosticar; o ensaio longo serve para aceitação.
>
> **Correção à proposta 3 (§ Proposta):** "interpolação linear chega" está errado. Com a fase fracionária a variar lentamente, a resposta da interpolação linear aos 20 kHz oscila entre 0 e cerca de −11,7 dB (−2 dB aos 10 kHz): os agudos ficam modulados. É preciso um reamostrador polifásico/sinc de razão variável.

### 3.3 [ALTO] Base de tempo em `system_clock` (relógio de parede)

`Browser.hpp:36` calcula o offset do CEF com `system_clock` no início de cada stream;
`DeckLink.cpp:60` fixa `epoch_` com `system_clock` no PLAY; os medidores
(`Main.mm:211`) e o monitor local (`Main.mm:181`) também usam `system_clock`.
Medido: este relógio derivou ~10 ppm face ao monótono em 65 s (ajuste NTP), e pode dar
saltos (acerto manual, correção NTP grande, regresso de suspensão).

Consequência: o desalinhamento entre o momento em que a página começou a emitir áudio
e o PLAY passa a fazer parte do offset. Uma layer carregada às 09:00 e posta no ar às
18:00, com 10 ppm, fica ~0,3 s desalinhada: ou ganha 300 ms de atraso A/V, ou fica
**em silêncio** desde o primeiro instante (se o desvio for no sentido do futuro). Um
salto de relógio entre os dois momentos tem o mesmo efeito, de imediato. Os pts do CEF
já são monótonos; a heurística `abs(now-pts) > 60000` só existe porque se mistura o
relógio de parede.

> **Resposta da revisão — 3.3:** **Confirmado, com ressalva sobre o domínio CEF.** `Browser.hpp`, `DeckLink.cpp`, monitor e medidores usam `system_clock`; os offsets fixos não acompanham saltos/ajustes posteriores. 10 ppm durante 9 h dá 324 ms, mas a medição de 65 s não prova que tal ajuste se mantenha durante 9 h.
>
> **Solução revista:** Domínio monótono comum, conversão CEF documentada e validada, deteção de reinício/suspensão. Não basta trocar nomes de relógios: origens e comportamento em suspensão têm de coincidir ou ser calibrados.

> **Resposta da revisão — domínio dos timestamps CEF:** O header CEF incluído no projeto documenta PTS em epoch; a [documentação CEF](https://cef-builds.spotifycdn.com/docs/142.0/classCefAudioHandler.html) repete esse contrato. O [código upstream do capturador](https://github.com/chromiumembedded/cef/blob/master/libcef/browser/audio_capturer.cc) calcula o valor a partir de `base::TimeTicks`. Isto corrobora a necessidade de validar a implementação real e não confiar só no comentário. O upstream consultado não é prova da origem numérica exata do binário fixado `g4fa4ab5`; não se deve assumir sem validação que `steady_clock::time_since_epoch()` coincide numericamente com os PTS.

> **Evidência reproduzível — domínio dos PTS (resolve a ressalva):** no trace de 120 s, `mach_absolute_time − PTS bruto` fica entre 21,0 e 22,0 ms (truncagem a 1 ms), com −6,6 ppm de diferença de ritmo, dentro da quantização. O binário fixado carimba os pacotes em `mach_absolute_time` (uptime, que para durante a suspensão), cerca de 21,5 ms antes de chegarem ao handler. O `steady_clock` do libc++ coincide com `mach_continuous_time` (inclui suspensão) e afastava-se dos PTS em 24 452 s, o sono acumulado desde o arranque. A revisão tinha razão: a proposta 1 ("`steady_clock` / `mach_absolute_time`, o mesmo domínio") estava errada quanto ao `steady_clock`. A timeline comum deve usar `mach_absolute_time` (ou `CLOCK_UPTIME_RAW`), ou calibrar o offset explicitamente, e tratar o acordar da suspensão como descontinuidade.

### Proposta para 3.1–3.3 (prioridade 1)

1. **Um único relógio monótono** em todo o percurso (`steady_clock` /
   `mach_absolute_time`, o mesmo domínio dos pts do CEF). Retirar a heurística de 60 s.
2. **Margem de jitter explícita** na fila de origem: pedir ao mixer
   `agora − J` com J configurável (sugestão: 40 ms por defeito), independente de
   `target_`. Custa 40 ms de latência e elimina 3.1. Compensar o mesmo J no vídeo para
   manter o A/V.
3. **Controlo de deriva**: medir a taxa real do dispositivo
   (`GetScheduledStreamTime`/`GetHardwareReferenceClock` vs relógio monótono) e manter
   o nível de enchimento da fila de origem centrado em J com um pequeno reamostrador
   adaptativo (±100 ppm; interpolação linear chega para esta gama) — o padrão habitual
   de PLL de áudio.
4. **Métrica de diagnóstico "source headroom"** (ms entre o fim da janela pedida e o
   fim do último pacote recebido), mín/máx por segundo, visível em Diagnostics. Mostra
   3.1 e 3.2 no próprio hardware antes de qualquer correção.
5. **Teste de longa duração** no HD Mini (≥ 2 h) a registar esse headroom e os
   contadores; é o único teste que distingue o comportamento correto do atual.

> **Resposta da revisão — proposta de sincronização e baixa latência:**
>
> **Primeiro, tornar mensurável.** Registar timestamps monótonos de chegada/PTS, fim efetivo dos pacotes, início/fim das leituras, cobertura por intervalo, profundidade SDI, tempo de callback, recuperações e frames tardios. `latestTimestamp` hoje é início do último pacote, não fim. A diferença “último fim − fim pedido” é útil, mas não deteta lacunas internas nem identifica áudio esperado: combinar com cobertura e estado da fonte. Não deixar métricas de uma layer sem stream dominar o controlador.
>
> **Depois, unificar a timeline.** Usar domínio monótono validado e mapear relógios CEF e DeckLink; acrescentar timestamps/idade aos frames e identificar a relação entre PTS de áudio e imagens. `OnPaint` fornece chegada da imagem, não PTS exato do conteúdo multimédia; timestamp de chegada é um avanço de instrumentação, não uma prova de lip sync. Usar flash e impulso sonoro produzidos pelo mesmo evento na página e medição na saída para calibrar.
>
> **Separar três parâmetros:** (a) reserva de agendamento no DeckLink; (b) margem de disponibilidade na origem; (c) ajuste relativo A/V. Alterar um não deve deslocar implicitamente os outros. A margem de origem deve ter mínimo/máximo e política de recuperação; valores iniciais só ficam aprovados depois de ensaio sob carga.
>
> **Reduzir reserva com medição.** Ensaiar duas e eventualmente uma frame em vez de três, com áudio/vídeo coordenados. A redução teórica de três para duas é 40 ms a 25p ou 20 ms a 50p no alvo de fila; não é uma melhoria física garantida. Manter o perfil atual como ponto de comparação e recuar se surgirem prazos falhados. Não retirar preroll indiscriminadamente.
>
> **Corrigir deriva suavemente.** Estimar razão Mac/dispositivo e filtrar jitter antes de atuar; razão comum para as fontes alinhadas e estado de fase contínuo. Dimensionar a faixa de correção com dados reais; ±100 ppm é uma hipótese de projeto, não um limite universal. Saída da faixa, sleep, reinício e dispositivo desligado exigem recuperação explícita, não uma razão extrema ou fila a crescer.
>
> **Não acrescentar cegamente 40 ms ao vídeo.** O áudio já usa um mapeamento histórico relativo a `target_`; o vídeo é amostrado mais tarde por um worker e apresentado segundo a sua fila. Primeiro medir o desfasamento existente e depois ajustar ambos para o mesmo acontecimento/instante de apresentação. Uma fila histórica de vídeo tem custo: a 1080p BGRA cada imagem ocupa ~8,3 MB; evitar retenção desnecessária, sobretudo em UHD.
>
> **Monitor local independente.** Substituir os 100 ms fixos por margem de origem calibrada e cursor relacionado com host/sample time do dispositivo local. Auscultadores com fio e AirPlay não têm o mesmo orçamento de latência. Oferecer escuta de diagnóstico claramente identificada; monitor de programa sincronizado precisa de associar a imagem local ao mesmo instante, não apenas repetir a amostragem do mixer.

## 4. Outros achados no áudio

### A1 — Média

**Achado da auditoria:** **PAUSE → PLAY (parar) deixa o mixer desativado**: `onAir(false)` põe `mixer->enabled=false` e `stop()` não o repõe. Com o SDI parado, o **Monitor OUTPUT fica mudo** até ao próximo PLAY, enquanto o medidor composto continua a mexer.

**Prova / localização:** `DeckLink.cpp:188`, `:190`; `Scene.cpp` `sample()` respeita `enabled`

**Proposta da auditoria:** Repor `mixer_->enabled=true` (e `air_=true`) em `stop()`, ou separar o estado de PAUSE do mixer partilhado.

> **Resposta da revisão — A1:** **Confirmado.** `onAir(false)` altera o mixer partilhado; `stop()` não repõe `enabled`; `configureScene` também não o faz. O medidor ignora esse estado.
>
> **Solução revista:** Preferir estado de PAUSE exclusivo da saída SDI, sem contaminar o mixer usado pelo monitor. Correção mínima: repor o estado em STOP e testar a sequência completa.

### A2 — Média

**Achado da auditoria:** **Monitor local morre ao trocar de dispositivo de áudio** (auscultadores, AirPlay, sleep): o AVAudioEngine para com a mudança de configuração e não há observador; `audioEngine` fica não-nulo e nunca é recriado.

**Prova / localização:** `Main.mm:181`

**Proposta da auditoria:** Observar `AVAudioEngineConfigurationChangeNotification` e reiniciar o motor; parar o motor quando não há fontes monitorizadas.

> **Resposta da revisão — A2:** **Confirmada a falta de recuperação; “morre” em todas as trocas não está demonstrado.** Não existe observador e o arranque depende de `audioEngine` ser nulo.
>
> **Solução revista:** Observar mudança de configuração, reconstruir/religar o grafo e reiniciar de forma serializada fora do render callback; tratar falhas, suspensão e fim de monitorização. Testar auscultadores e dispositivos com formatos distintos.

### A3 — Média

**Achado da auditoria:** **Trabalho não-tempo-real na thread de áudio do Core Audio**: o render block aloca vetores, copia `shared_ptr` e bloqueia mutexes partilhados com o CEF e o DeckLink → risco de cortes no monitor por inversão de prioridade.

**Prova / localização:** `Main.mm:181` → `AudioMixer::sample`

**Proposta da auditoria:** Buffers pré-alocados, fila lock-free (SPSC) por destino, ou um thread intermédio que prepara blocos.

> **Resposta da revisão — A3:** **Confirmado como risco.** `sample()` aloca buffers, copia inputs/shared pointers e toma mutexes também usados pelos produtores e pelo SDI. Não há medição de inversão de prioridade.
>
> **Solução revista:** Preparar blocos fora do callback e entregar por ring buffer pré-alocado; callback só copia ou preenche silêncio. Uma SPSC por consumidor/destino, não uma única fila consumida por SDI e monitor. Medir o atraso adicional introduzido pelo worker.

### A4 — Média

**Achado da auditoria:** **Clipping invisível**: o mixer faz *hard clip* (não é um limitador) e os medidores fazem `clamp` a ±1 antes de medir, por isso um mix a +4 dBFS lê no máximo 0 dBFS. Teste: ganho ×2 sobre 0,8 → medidor 1,000, 960 amostras cortadas. Só o contador em Diagnostics revela.

**Prova / localização:** `Scene.cpp:35` (`measureAudio`), `:47`

**Proposta da auditoria:** Medir sem `clamp`, LED "OVER" com retenção por layer e no OUTPUT; opcionalmente um limitador suave com teto −1 dBFS.

> **Resposta da revisão — A4:** **Confirmado e reproduzido.** `measureAudio` faz clamp antes de calcular pico/RMS; a conversão PCM faz saturação. O número 960 da auditoria depende dos canais que excedem o limite. O medidor da layer é pré-ganho: por desenho não revela clipping causado pelo seu fader.
>
> **Solução revista:** Medição pré-saturação, OVER com retenção no OUTPUT e indicação pós-ganho por layer se desejada; conservar medidor de origem identificado. Saturação final continua a proteger a conversão. Limitador opcional, sem impor lookahead ao percurso normal.

### A5 — Média

**Achado da auditoria:** **Transições abruptas**: mute, visibilidade, PAUSE e ganho mudam em degrau no início do bloco (até 1024 amostras) → clicks e "zipper noise" a mexer o fader.

**Prova / localização:** `Scene.cpp:47`, `DeckLink.cpp:188`

**Proposta da auditoria:** Rampas de 5–20 ms (ganho interpolado por amostra; fade-out/in no PAUSE e no mute).

> **Resposta da revisão — A5:** **Confirmado no código; audibilidade depende do sinal.** Não há rampas. O bloco de 1024 é do SDI, não uma constante de todos os consumidores.
>
> **Solução revista:** Rampas curtas de ganho/mute por destino, sem buffer de antecipação; definir duração e comportamento para PAUSE, remoção e fim de stream. Não implica 5–20 ms extra de latência de transporte.

### A6 — Baixa

**Achado da auditoria:** **MONO com um só canal ligado perde 6 dB**: L=0,5, R desligado, MONO → 0,25 em ambos.

**Prova / localização:** `Scene.cpp:33`; teste dirigido

**Proposta da auditoria:** Se só um canal está ativo, duplicá-lo a ganho unitário; média só com os dois.

> **Resposta da revisão — A6:** **Comportamento confirmado e reproduzido; classificação como defeito depende da intenção.** A média inclui o canal desligado como zero: atenua 6,02 dB.
>
> **Solução revista:** Se MONO significa “usar o canal selecionado em ambos”, normalizar pelo número de canais ativos; com ambos ativos manter a média. Atualizar ajuda e testes para a semântica escolhida.

### A7 — Baixa

**Achado da auditoria:** Semântica L/R diferente entre layer e OUTPUT e monitor de layer pré-fader sem indicação (ver §2.1). Faixa de layer oculta sem indicação na gaveta AUDIO.

**Prova / localização:** `interface.html` (`drawAudio`)

**Proposta da auditoria:** Etiqueta "PFL" ou opção pré/pós-fader; marcar faixa como "HIDDEN" quando a layer está oculta.

> **Resposta da revisão — A7:** **Confirmado.** Monitor de layer ignora visibilidade, mute e ganho, mas respeita seleção L/R e MONO. A UI só assinala mute explícito na faixa.
>
> **Solução revista:** Rotular “PFL”, explicitar entrada L/R versus saída L/R e mostrar “fora do programa/oculta”. Não é necessário mudar o roteamento para resolver a ambiguidade.

### A8 — Baixa

**Achado da auditoria:** Pacotes sobrepostos são **somados** (1 ms de sobreposição → amplitude a dobrar). Não observado na medição (Δpts sempre 10 ms), mas não há proteção.

**Prova / localização:** `Scene.cpp:16`, `:33`

**Proposta da auditoria:** Derivar o início do pacote de um contador de amostras contínuo e usar o pts só para detetar descontinuidades.

> **Resposta da revisão — A8:** **Confirmado e reproduzido; ocorrência real não demonstrada.** Pacotes com início crescente mas sobrepostos somam-se. Pacotes com início anterior ao último limpam as filas, outro caso que merece teste.
>
> **Solução revista:** Contador contínuo de amostras por stream, ancorado ao PTS; distinguir quantização, repetição, lacuna e reinício. Descartar/aparar duplicados. Não usar o contador para ignorar descontinuidades reais.

### A9 — Baixa

**Achado da auditoria:** Na recuperação de underrun, `cursor_` passa para o tempo de stream *atual* → as amostras seguintes já nascem atrasadas; a recuperação é contada em "Audio errors" junto com erros reais.

**Prova / localização:** `DeckLink.cpp:69`

**Proposta da auditoria:** Ressincronizar para `agora + margem`; contadores separados para recuperações e erros.

> **Resposta da revisão — A9:** **Confirmada a recuperação sem folga; atraso efetivo por medir.** O cursor passa para `GetScheduledStreamTime`, que já é passado quando o agendamento termina. O contador mistura recuperação com erro de API.
>
> **Solução revista:** Recomeçar num instante futuro com folga medida e remapear a origem de forma coerente com vídeo; contadores distintos e pequena transição. Evitar crescimento de latência após recuperações sucessivas.

### A10 — Baixa

**Achado da auditoria:** `outputExpected` ignora ganho master 0 e L/R desligados → expiração classificada como "overrun" quando o áudio nem é pedido.

**Prova / localização:** `Main.mm:100`

**Proposta da auditoria:** Incluir esses casos.

> **Resposta da revisão — A10:** **Confirmado e incompleto.** `outputExpected` ignora ganho/canais master e também a seleção L/R da própria layer. Expiração não prova perda de áudio que devesse ter sido ouvido.
>
> **Solução revista:** Derivar a expectativa do roteamento efetivo, incluindo ganhos/canais, e separar expiração, descarte por pressão, underrun e ausência legítima de fonte.


## 5. Outras funcionalidades

### V1 — **Alta** (regiões 50 Hz)

**Achado da auditoria:** O CEF está fixo a **60 fps** (`Browser.cpp:9`, `windowless_frame_rate=60`) e a composição faz polling de 16 ms, sem ligação à cadência SDI. A 50p cai 1 em cada 6 frames, a 25p alterna 2/3 → **judder** visível em movimento (a entrada do lower third). Em Portugal o normal é 50/25.

**Proposta da auditoria:** `windowless_frame_rate` igual à cadência SDI (arredondada); idealmente `external_begin_frame_enabled` com `SendExternalBeginFrame` acionado pelos callbacks do DeckLink, para ter um frame renderizado por frame SDI.

> **Resposta da revisão — V1:** **Confirmado o desacoplamento; padrão exato simplificado na auditoria.** CEF pede 60 fps; composição verifica de ~16 em ~16 ms e SDI lê a imagem mais recente. Não há fase comum. A 50/25 a amostragem de movimento pode ser irregular; o padrão 1-em-6 ou 2/3 pressupõe cadências ideais.
>
> **Solução revista:** Renderizar à cadência de saída, composição acionada por novas imagens e agendamento coerente. Avaliar external begin frame; o disparo tem de antecipar o prazo de saída, porque renderização é assíncrona. Arredondar 59,94 para 60 não resolve deriva de cadência.

### V2 — Média (a verificar)

**Achado da auditoria:** Conversão de cor: sRGB → linear → OETF BT.709 (`Core.cpp:76`). Um cinzento sRGB 128 sai a ~115; num monitor BT.1886 os meios-tons ficam **mais escuros** do que na pré-visualização. A prática corrente em grafismo é manter os valores de código (sRGB ≈ BT.709/BT.1886 por ecrã).

**Proposta da auditoria:** Confirmar com waveform; tornar a transferência uma opção (passthrough por defeito vs conversão atual).

> **Resposta da revisão — V2:** **Confirmada a transformação; “errada” não demonstrado.** Com alpha=255, cinzento 128 converte para cerca de 115 pela LUT. Aparência física depende da cadeia de exibição.
>
> **Solução revista:** Validar patches/waveform e recetor; opção explícita de transferência se necessária. Preservar valores de código não permite eliminar a desmultiplicação de alpha exigida pelo percurso atual. Não alterar silenciosamente o comportamento por defeito.

### V3 — Média

**Achado da auditoria:** **Cada mensagem da UI grava o `session.json` e reconfigura a cena** (`Main.mm:194`). Arrastar um fader gera dezenas de escritas atómicas em disco por segundo e força a recomposição do vídeo (incrementa a revisão da cena) só por causa de um ganho de áudio.

**Proposta da auditoria:** Debounce da gravação (ex.: 500 ms / no blur); separar `configureAudio` de `configureScene`.

> **Resposta da revisão — V3:** **Confirmado para mensagens mutáveis, não literalmente cada mensagem.** `ready`, `layout`, `drag` e outras regressam antes. Ganho, mute e monitor chegam a `configureScene`/`save`; `Scene::configure` incrementa sempre a revisão.
>
> **Solução revista:** Separar configuração de áudio/imagem, persistir só estado durável, debounce e flush ao sair. Guardar apenas quando algo mudou. Reduz contenção e carga, mas não elimina as reservas temporais existentes.

### V4 — Baixa

**Achado da auditoria:** Fader de ganho linear em amplitude (0–2): metade inferior = −∞…0 dB, −20 dB fica a 5 % do curso → pouca resolução onde mais se trabalha.

**Proposta da auditoria:** Curva em dB (ex.: −60…+6 dB com zona morta em −∞).

> **Resposta da revisão — V4:** **Confirmado; melhoria ergonómica.** Ganho 0,1 (−20 dB) fica a 5% do curso 0–2. A UI já permite dB numéricos no editor de layer.
>
> **Solução revista:** Curva em dB com posição explícita −∞ e 0 dB; guardar amplitude continua a ser válido. Sem efeito direto no sincronismo.

### V5 — Baixa (a verificar)

**Achado da auditoria:** `OnBeforeBrowse` cancela qualquer navegação não HTTP(S)/u2k em **todos os frames**; iframes `data:`/`blob:`/`srcdoc` de terceiros podem ficar bloqueados.

**Proposta da auditoria:** Testar com páginas reais (widgets, embeds); aplicar a regra só ao frame principal se for o caso.

> **Resposta da revisão — V5:** **Confirmada a política ampla; incompatibilidade concreta por demonstrar.** `OnBeforeBrowse` não distingue frame principal; nem toda a forma de iframe provoca necessariamente a mesma navegação/callback.
>
> **Solução revista:** Página de teste com data/blob/srcdoc e widgets reais. Relaxar apenas as navegações necessárias em subframes, mantendo a política do frame principal e a separação da UI. Não autorizar esquemas indiscriminadamente.

### D1 — Baixa

**Achado da auditoria:** Documentação desalinhada: `ARCHITECTURE.md` ainda diz "Studio Rack 0.2" e põe o master mute em "Setup Advanced"; `VERSIONS.md` salta de 0.2.2 para 0.4.0; `VALIDATION.md` não tem entrada 0.4.0; commits "3.0.0"/"3.0.1" vs tags 0.3.0/0.3.1. O README chama "limitado" ao que é hard clip.

**Proposta da auditoria:** Atualizar os documentos; corrigir a terminologia.

> **Resposta da revisão — D1:** **Confirmado.** Arquitetura 0.2, localização antiga de mute, versões 0.3 ausentes e validação sem secção 0.4. A validação resumida existe em VERSIONS. “Limited” é ambíguo, embora a saturação proteja overflow.
>
> **Solução revista:** Atualizar documentação para a implementação efetiva; usar “hard clipping/saturation”. Explicar nomes históricos de commits sem reescrever o histórico publicado.

### T1 — Média

**Achado da auditoria:** Testes: nenhum cobre temporização/deriva, PAUSE→STOP, MONO+L/R, clipping visível ou o monitor local; o smoke test só exige > 4800 amostras capturadas.

**Proposta da auditoria:** Testes unitários para A1, A4, A6 e um harness de deriva como o usado nesta auditoria (§3.2).

> **Resposta da revisão — T1:** **Essencialmente confirmado, com correção.** Já há testes de MONO, clipping PCM/contador, mute, L/R e leitura de monitor sem consumo. Faltam as combinações e a temporização apontadas, visibilidade de OVER e o motor local real.
>
> **Evidência reproduzível — T1:** a correção da revisão está certa: o achado original ignorava o teste existente de leitura do monitor sem consumo. `tools/audio-audit/drift_sim.cpp` é o harness determinístico de relógios; pode ser convertido em teste CTest quando houver correção de deriva para validar.
>
> **Solução revista:** Acrescentar testes das sequências/regressões e harness determinístico de relógios; manter aceitação física separada. O smoke de hardware exige também frames SDI, mas o limiar de não-zero não prova sincronismo nem continuidade.


Sem problemas encontrados em: ordem e alfa pré-multiplicado da composição, fit
proporcional, bloqueio de configuração durante PLAY, confirmação de saída/apagar,
watchdog de 2 s de perda de dispositivo, isolamento do CEF (perfil temporário,
mock-keychain, permissões de média negadas, popups bloqueados), servidor `u2k://`
restrito ao catálogo e migração/validação do `session.json`.

> **Resposta da revisão — alcance da conclusão:** As afirmações de “sem problemas” devem ler-se como “não foram encontrados problemas nos cenários examinados”. Não demonstram segurança integral, comportamento físico do keyer ou imunidade a falhas.

## 6. Ordem de trabalho proposta

1. Métrica "source headroom" + teste longo no HD Mini (mede 3.1/3.2 no hardware real).
2. Relógio monótono único + margem J explícita (3.1, 3.3).
3. Controlo de deriva com reamostragem adaptativa (3.2).
4. Correções pequenas: A1, A2, A6, A9, A10, V3.
5. Cadência de vídeo ligada ao SDI (V1) e opção de transferência de cor (V2), com waveform.
6. Qualidade de mistura: rampas (A5), OVER/limitador (A4), fader em dB (V4), clareza da UI (A7).
7. Documentação e testes (D1, T1).

> **Resposta da revisão — prioridades e aceitação:**
>
> 1. Métricas e harness reproduzível; correções A1/A10; separar configuração/persistência (V3). Baseline antes de mexer nas filas.
> 2. Relógio monótono/mapeamento e tratamento de descontinuidades; medir A/V com flash+impulso na saída real.
> 3. Percursos simples, pré-alocação e composição acionada por frames; comparar desempenho e igualdade de resultado.
> 4. Margem explícita, correção de deriva e perfis de reserva SDI; ajustar A/V medido. Recuperação de Core Audio e eliminação do atraso local fixo.
> 5. Cadência render/composição/saída, rampas e indicadores; restantes melhorias de UI, cor e documentação.
>
> Ensaios: uma/múltiplas fontes, 25/50 e taxas fracionárias suportadas, ganho/mute/PAUSE→STOP, mudanças de stream, sobreposições e lacunas, jitter sintético com picos, deriva positiva/negativa, saltos de parede e suspensão. Comparar percurso simples e geral, medir custos máximos/percentis, continuidade e offset A/V ao longo do tempo.
>
> O ensaio físico de 2 h é útil, mas não substitui simulações aceleradas: a falha lenta estimada pela própria auditoria ocorre mais tarde (~7 h). Acrescentar ensaio prolongado que cubra essa condição. Definir previamente orçamento de A/V e latência; exigir ausência de underruns/crescimento de offset nas condições aprovadas, não apenas “amostras não nulas”. O mesmo recetor deve medir som e imagem; duas capturas em relógios distintos podem criar um desfasamento de medição.

Nota: a documentação do projeto está toda em inglês (desde a 0.2.1); este relatório
está em português por ser dirigido ao Luis. Se for para ficar versionado, convém
traduzi-lo.

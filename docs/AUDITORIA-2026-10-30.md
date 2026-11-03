# Auditoria URL2KeyFill 0.4.0 — 30/10/2026

Âmbito: código em `development/v0.2` (commit `c7f0558`, versão 0.4.0), com foco no
roteamento de áudio. Sem hardware DeckLink ligado durante a auditoria
(`decklink_probe`: *No compatible SDI output device detected*), por isso a saída SDI
física não foi testada; o comportamento do callback DeckLink foi analisado no código e
simulado com as classes reais do mixer.

> **Correção do auditor:** a frase acima está errada. A UltraStudio HD Mini estava ligada; o `decklink_probe` foi executado dentro do sandbox da shell, que esconde o driver. Fora do sandbox o dispositivo é detetado e foi medido — ver §8.

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

> **Segunda revisão — reprodução da resposta do auditor (30/10):** executei `run-models.sh` com as fontes atuais e o analisador sobre o trace comprimido de 120 s. Os checks do mixer passaram e os números publicados de jitter/deriva foram reproduzidos. O trace contém efetivamente 11 743 pacotes, mas cobre **117,4 s entre o primeiro e o último**, dentro de uma captura nominal de 120 s. As ressalvas anteriores sobre falta de acesso aos dados ficam ultrapassadas por esta evidência; a validação física SDI e a margem real de arranque continuam em aberto. As novas correções não tornam válidos os números antigos que foram conservados acima: em caso de conflito, prevalece a evidência reproduzível, com as precisões seguintes.

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

> **Segunda revisão — 3.1:** valores reproduzidos: p50 0,95 ms, p99 1,60 ms, máximo 30,70 ms; 4 pacotes acima de 2 ms, 2 acima de 20 ms e nenhum acima de 40 ms, **relativamente aos PTS ajustados pela aplicação**. O modelo reproduz 0,178%, 0,024% e 0% de amostras em falta para margens de 0,5/2/40 ms. “Pacotes tardios” no analisador é um indicador de chegada; não é uma contagem observada de underruns DeckLink. O simulador assume escrita integral, callbacks instantâneos e buffer consumido exatamente pelo relógio modelado: não simula custo/concorrência, FIFO físico, escritas parciais ou prazos do driver. É adequado para demonstrar o mecanismo e comparar cenários, não para prever a taxa física de cortes.

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

> **Segunda revisão — qualidade do reamostrador:** concordo com a retirada da recomendação de interpolação linear. Para 48 kHz e fase fracionária 0,5, a resposta em amplitude é `|cos(πf/48000)|`: −11,74 dB a 20 kHz e −2,01 dB a 10 kHz, conforme os valores do auditor. Uma razão muito próxima de 1 apenas torna lenta a variação da fase, não elimina esta atenuação. Recomenda-se reamostragem de qualidade com razão variável, estado contínuo e atraso de grupo conhecido; polifásico/sinc é uma opção adequada. Contabilizar esse atraso no ajuste A/V, incluindo no percurso de uma fonte.

> **Segunda revisão — 3.2:** a correção do erro de fase do primeiro simulador é pertinente e os novos resultados são reproduzíveis. Há uma precisão necessária: `missing` e `total` são repostos depois de cada impressão. Assim, **4,542% é a perda no intervalo 150–300 s; 40,652% no intervalo 450–600 s; 87,430% no intervalo 750–900 s**. Não são percentagens acumuladas desde PLAY nem valores instantâneos aos 5/10/15 min. No caso lento, 99,791% refere-se à oitava hora. Os ~80 s são o limiar analítico aproximado para margem 3 ms e atraso constante 1,4 ms, não uma medição do HD Mini. A inclinação da margem é útil para diagnóstico, mas deve ser estimada por regressão/janelas e com incerteza: a 20 ppm o efeito é apenas 1,2 ms/min, sobre jitter e quantização. O sinal também importa: dispositivo mais rápido faz a margem descer; mais lento faz subir. Duas horas são um ensaio diagnóstico razoável para este cenário, sem garantia universal de detetar qualquer deriva.

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

> **Segunda revisão — PTS e orçamento de latência:** a evidência sustenta que os PTS brutos acompanham o domínio uptime neste sistema e confirma que `steady_clock` não é substituição numérica direta. Porém, **“fica entre 21,0 e 22,0 ms” confunde percentis com limites**: o analisador publica p1/p99; calculei mínimo **20,982 ms**, mediana **21,496 ms** e máximo **51,338 ms** para chegada em `mach_absolute_time` menos PTS bruto. A origem comum é consistente com o trace, mas o diferencial inclui idade de captura/entrega e quantização; não deve ser tratado integralmente como erro de relógio a subtrair.
>
> **Consequência para a solução:** o offset atual ancora o primeiro PTS à chegada do primeiro pacote e remove da timeline aproximadamente 21 ms de idade inicial nesta captura. Passar a usar PTS bruto no domínio uptime preserva essa idade: as margens medidas sobre PTS ajustados deixam de ter o mesmo significado. Os **40 ms ajustados não equivalem a 40 ms brutos**; o máximo bruto observado é ~51,3 ms. Isto não obriga automaticamente a escolher uma margem maior: obriga a definir se J é a idade total de leitura ou uma reserva adicional sobre a idade de captura e a medir novamente disponibilidade e A/V. Para várias layers, evitar ancorar cada uma ao seu primeiro callback, pois o jitter inicial pode introduzir offsets relativos distintos. Tratar suspensão/retoma como descontinuidade e validar a conversão de relógios no sistema suportado.

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

## 7. Plano detalhado de implementação — proposta para validação do auditor

**Estado: proposta, ainda não implementada.** Esta secção consolida as respostas inline e substitui a ordem indicativa da secção 6 para execução futura. Não aprova os 40 ms como configuração final, não assume oscilação de ±20 ppm no HD Mini e não considera o monitor local prova do sincronismo SDI. A validação deve incidir primeiro nos contratos temporais, depois nos resultados de cada fase.

### 7.1 Objetivos, âmbito e regras de execução

1. Impedir crescimento ilimitado do desfasamento e corrigir cortes causados por indisponibilidade de amostras, deriva ou recuperação incorreta.
2. Reduzir latência nos casos comuns sem perder a correção de relógios, o roteamento, o controlo de níveis ou a qualidade de áudio.
3. Separar três resultados: atraso total, offset A/V e continuidade. Cada um terá métricas e critérios próprios.
4. Manter o código portátil no core; relógios macOS, CEF, DeckLink e AVAudioEngine ficam em adaptadores. Não introduzir dependências macOS nos testes determinísticos.
5. Executar em alterações pequenas e verificáveis, com resultado anterior/posterior no mesmo cenário. As alterações já em curso no gerador AV, documentação e host devem ser inventariadas e preservadas; os testes têm de identificar o commit e eventuais alterações locais.
6. Não incluir nesta intervenção Windows, interlaced, novos formatos SDI, notarização ou reconstrução geral da UI. V2 e V5 só mudam comportamento se a validação específica justificar.

### 7.2 Contrato temporal a aprovar antes de programar

**Unidades e relógios.** Introduzir tipos explícitos para tempo monótono do host, posição inteira de amostra, fase fracionária de leitura e tempo racional de vídeo. Conversões centralizadas, com precisão superior a milissegundos e sem multiplicar epochs de parede por 48. `system_clock` fica apenas para datas legíveis nos logs. No macOS, o adaptador usa uptime compatível com os PTS medidos; não assume igualdade numérica com `steady_clock`.

**PTS CEF.** Na versão fixada, converter o PTS bruto para a timeline comum preservando a idade de captura. Não ancorar cada stream à chegada do seu primeiro pacote. Registar domínio/versão, validar intervalos e tratar reinício, mudança de domínio e suspensão como eventos explícitos. Um contrato CEF futuro diferente exige adaptador/teste próprio, não a heurística silenciosa de 60 s. A quantização de 1 ms é tratada como incerteza de timestamp, não como jitter de produção de amostras.

**Relação origem/apresentação.** Definir `H(s)` como o instante host estimado em que a amostra de saída `s` será apresentada pelo dispositivo. O conteúdo a ler é `C(s) = H(s) − L`, onde `L` é o atraso de conteúdo pretendido nessa referência. Se o agendamento lê para um instante aproximadamente `D` no futuro, a idade da origem no momento da leitura é aproximadamente `J = L − D`. Consequentemente, reduzir D pode reduzir L mantendo J; aumentar D sem aumentar L reduz a margem de origem. Usar esta equação como referência, não manter implicitamente `epoch + cursor − target`.

**Orçamento de latência.** Distinguir `D` (antecedência/reserva de agendamento), `J` (idade da origem à leitura, incluindo a idade preservada pelos PTS brutos) e o offset relativo de vídeo/áudio. Registar ainda atraso de grupo do reamostrador, processamento/conversões e latência residual do hardware/recetor. A associação entre tempo de stream e apresentação física tem de ser calibrada; não assumir que a chamada SDK revela o instante elétrico exato. A idade ~21,5 ms do trace atual não se soma duas vezes nem desaparece por ajuste do primeiro callback.

**Offset A/V.** Definir sinal: `ΔAV = instante do som − instante do flash`; positivo significa som atrasado. A compensação deve selecionar imagem/áudio compatíveis com a apresentação pretendida. Como `OnPaint` não fornece PTS multimédia, timestamp de chegada apenas limita/mede a idade; a calibração flash/impulso é obrigatória e a incerteza do gerador/captura deve acompanhar o resultado. Não prometer sincronismo exato entre conteúdos independentes de páginas distintas.

**Recuperação e estados.** Distinguir STOP, preroll, PLAY, PAUSE, recuperação e falha de dispositivo. PAUSE mantém o stream SDI e faz fade para silêncio/transparência; STOP liberta o dispositivo sem deixar o monitor bloqueado. Reinício de stream invalida a sua geração anterior, não as restantes fontes. Sleep/wake e saltos de relógio do dispositivo invalidam mapeamentos e iniciam recuperação controlada.

**Decisões para o auditor:** validar estas definições, o tratamento de PTS brutos e a estratégia de medição antes de aceitar quaisquer valores por defeito de latência.

### 7.3 Fase 0 — baseline, evidência e testes de referência

**Ficheiros:** `tools/audio-audit/*`, `tests/SceneTests.cpp`, `tests/CoreTests.cpp`, `CMakeLists.txt`, `docs/VALIDATION.md`.

- Reexecutar checks existentes e os dois traces; guardar comandos, compilador, commit, configuração, hash dos traces e resumo dos resultados. Identificar claramente build instrumentada e build normal.
- Distinguir no output do simulador perdas por janela, acumuladas, maior sequência de amostras em falta e tempo da primeira falha. Acrescentar verificações automáticas de resultados, em vez de depender só da leitura de stdout.
- Extrair o cálculo de agendamento para um componente testável usado também pelo backend. O simulador passa a exercitar essa lógica, evitando manter uma cópia que possa divergir de `DeckLink.cpp`.
- Parametrizar relógio, fase/período de callback, packetização, atraso de chegada, capacidade de histórico e resposta do dispositivo. Incluir callbacks tardios, buffer real reportado, escritas parciais/zero, falha de API e pausas no consumidor.
- Medir baseline físico quando houver hardware: 25p/50p e modos fracionários suportados, uma fonte, múltiplas fontes, carga de UI e de composição. Usar a mesma cadeia de captura para A/V e registar a sua resolução temporal.

**Saída para revisão:** relatório reproduzível que distingue medições, simulações e critérios ainda não executados. Sem hardware, esta fase pode concluir a parte determinística, deixando a baseline física explicitamente pendente.

### 7.4 Fase 1 — telemetria e correções de estado/carga

**Casos:** A1, A9/A10 parcialmente, V3, T1. **Ficheiros:** `Scene.*`, `Output.hpp`, `DeckLink.cpp`, `Main.mm`, `interface.html`.

- Acrescentar por fonte geração do stream, primeiro/último sample disponível, fim efetivo recebido, cobertura da janela pedida e lacunas. Métricas por destino: margem de origem, profundidade agendada, tempo máximo/percentis de preparação e contadores separados de underrun, recuperação, expiração normal, descarte por capacidade e erro de API.
- Atualizar contadores no percurso crítico sem logs de disco, formatação ou alocação. Agregar/exportar fora dos callbacks com frequência limitada. Timestamp de chegada deve ser capturado antes de trabalho de instrumentação; comparar overhead com build normal.
- Corrigir A1: tirar a propriedade de PAUSE do mixer partilhado. A saída controla o silêncio SDI; monitor OUTPUT segue o programa durante PLAY/PAUSE e volta a poder ouvir a mistura após STOP, respeitando master mute. PFL continua independente. Tornar esta política explícita nos testes/UI.
- Corrigir `outputExpected` segundo o roteamento efetivo, incluindo ganhos e canais de entrada/saída. Não classificar ausência de fonte ou cancelamento entre fontes como perda de buffer.
- Separar `configureAudio` de configuração de imagem. Nome, ganho, canais, mute e monitor não incrementam revisão visual sem necessidade; ordem/visibilidade/tamanho alteram imagem quando aplicável.
- Separar estado transitório de estado persistente. Gravação com debounce inicial de 500 ms, uma escrita consolidada e flush no encerramento normal; reportar falha de gravação e documentar a janela de alterações potencialmente perdida em crash.

**Aceitação:** PAUSE→STOP→monitor e STOP→PLAY repetidos sem estado preso; UI de áudio não provoca novas composições por si só; rajada de fader não gera uma escrita por evento; métricas distinguem silêncio legítimo de falta de cobertura; nenhuma alteração de reserva temporal nesta fase.

### 7.5 Fase 2 — timeline comum, continuidade e recuperação

**Casos:** 3.3, A8, A9. **Componentes propostos:** `MediaClock`/adaptador macOS e `AudioTimeline` no core; integração em `Browser.hpp`, `Scene.*`, `DeckLink.cpp`, `Main.mm`.

- Introduzir relógio injetável e mapeamento host/stream com amostras de medição e incerteza. Substituir o uso de relógio de parede no áudio, medidores e monitor; preservar o relógio de parede apenas nos logs humanos.
- Converter PTS bruto e construir posição contínua por contagem de samples, ancorada ao timestamp de captura. Pequena variação compatível com quantização não cria sobreposição/lacuna artificial. Definir limiar de descontinuidade a partir da quantização e duração de pacote, coberto por testes.
- Descartar duplicados, aparar sobreposição reconhecida e representar lacunas reais como ausência de dados. Uma regressão grande/reinício cria nova geração; contadores separados para cada situação.
- Em underrun de dispositivo, selecionar um instante futuro com folga, recalcular o mapeamento de conteúdo e recuperar com transição curta. Não saltar simplesmente para o tempo já decorrido nem aumentar a latência a cada recuperação.
- Em suspensão/desligação, parar ou reconstruir o percurso conforme a disponibilidade do dispositivo; limpar blocos de gerações inválidas e reconstruir preroll antes de retomar.

**Aceitação:** relógio de parede avança/recua sem deslocar áudio; streams iniciados em momentos diferentes mantêm timestamps no mesmo domínio; duplicados não duplicam amplitude; lacunas não são escondidas pelo contador contínuo; recuperações repetidas mantêm orçamento limitado. Testar incluindo offset uptime/continuous muito grande.

### 7.6 Fase 3 — percurso de áudio previsível e simplificação automática

**Casos:** A3 e redução de processamento. **Ficheiros:** `Scene.*`, `Browser.*`, `DeckLink.cpp`, `Main.mm`; novos componentes de buffers conforme necessário.

- Fixar limites de fontes, tamanho máximo de bloco e capacidade de armazenamento; pré-alocar buffers. Configuração e lifetime das fontes são preparados fora dos callbacks, com publicação de snapshots e libertação fora da thread de áudio.
- Definir propriedade das filas antes da implementação: entrada CEF por stream → preparação/fan-out → buffers independentes de SDI e monitor. SPSC só quando existir exatamente um produtor e um consumidor; não partilhar uma fila consumível entre os dois destinos. Medidores usam estatísticas/snapshots, não drenam áudio de programa.
- O callback Core Audio só copia blocos preparados ou escreve silêncio; não aloca, não toma mutexes e não destrói objetos cujo lifetime possa bloquear. No SDI, cumprir o mesmo objetivo no trabalho de preparação; chamadas necessárias à API do driver ficam identificadas e medidas.
- Se a preparação usar worker, selecionar blocos e capacidade pelo prazo de consumo, medir a reserva adicional e incluí-la no orçamento. Evitar criar mais uma fila de 100 ms por conveniência.
- Implementar três percursos automáticos: silêncio, uma fonte estéreo neutra e mistura geral. A via neutra evita soma/copias/multiplicações desnecessárias, mas conserva continuidade, conversão PCM segura, métricas e correção de relógio.
- Transições de percurso ocorrem na fronteira adequada e preservam fase/estado do reamostrador. Mudanças de ganho não reiniciam o cursor. O mecanismo de deriva não é desativado apenas por existir uma fonte.

**Aceitação:** instrumentação sem alocações/mutexes da aplicação no render callback após arranque; buffers limitados sob pressão; falhas de preparação produzem silêncio contado, sem acesso inválido; ganho de CPU medido contra baseline; percurso neutro e geral equivalentes a ganho unitário, com mesma timeline. Usar sanitizers em testes próprios de concorrência quando compatíveis, separadamente dos ensaios de desempenho.

### 7.7 Fase 4 — margem de origem e compensação adaptativa de deriva

**Casos:** 3.1, 3.2 e recuperação A9. **Componentes:** mapeamento temporal, controlador de deriva, reamostrador, agendador e diagnósticos.

- Introduzir separadamente margem J, reserva D e atraso L segundo 7.2. A capacidade de histórico tem de cobrir a maior leitura permitida, atraso do filtro e tolerância de recuperação; ter capacidade não implica preenchê-la integralmente.
- Estimar taxa do dispositivo a partir de tempo de stream/hardware versus host. Consultas SDK fora da thread crítica sempre que possível; filtrar ruído e rejeitar medições de arranque/recuperação. Não inferir taxa exclusivamente da chegada de um pacote CEF.
- Controlar fase/margem lentamente em torno do alvo e corrigir a razão de amostras com reamostrador de razão variável. Limites de razão, slew rate, constantes do filtro e banda morta ficam explícitos e testáveis; ±100 ppm será um cenário de ensaio, não uma suposição universal de hardware.
- Aplicar uma relação temporal coerente às fontes alinhadas no mesmo domínio. Estado de interpolação e fase é por stream/consumidor conforme necessário; não fazer cada layer perseguir independentemente o jitter do seu último callback.
- Selecionar implementação de reamostragem após comparar qualidade, atraso de grupo, CPU, suporte de razão variável, licença e integração. Validar resposta em frequência/aliasing e continuidade na mudança de razão; não adotar interpolação linear como solução final.
- Fora da faixa corrigível ou após salto grande, sinalizar recuperação e reconstruir o alinhamento de forma limitada, com fade e contagem. Proibir inserção/remoção abrupta recorrente de samples como controlador normal.
- Iniciar ajuste de J com dados medidos sobre **PTS bruto** e carga representativa. Não converter o máximo de dois traces curtos em garantia; registar folga, falhas e latência observada para cada valor ensaiado.

**Aceitação determinística proposta:** 8 h simuladas por cenário, deriva 0/±20/±100 ppm e varrimento gradual; dentro do envelope de chegada/capacidade configurado, nenhuma amostra esperada em falta após aquisição e erro de margem limitado sem crescimento secular. Testar também fora do envelope e exigir recuperação contada/limitada, em vez de prometer continuidade impossível. Controlador deve estabilizar sem modular audivelmente o sinal nem perseguir os degraus de 10 ms dos pacotes.

### 7.8 Fase 5 — vídeo, alinhamento A/V e perfil de baixa latência

**Casos:** V1 e percursos simples de vídeo. **Ficheiros:** `Core.*`, `Scene.*`, `Browser.*`, `DeckLink.cpp`, `Main.mm`, testes de composição/agendamento.

- Acrescentar metadados de chegada, origem e geração aos frames; composição propaga idade de fontes e instante de produção. Não chamar PTS multimédia ao timestamp de chegada. Aliases no percurso simples não alteram a identidade da fonte indevidamente.
- Uma única imagem visível, sem transformação e no tamanho correto: publicar a referência imutável em vez de compor sobre transparente. Zero fontes: frame transparente reutilizável. Várias fontes: manter source-over atual.
- Acionar composição por novos frames/configuração, coalescendo notificações e sem perder a última atualização. Eliminar a espera fixa de polling sem criar espera ativa ou composição ilimitada sob rajadas.
- Adequar cadência CEF ao modo SDI e avaliar external begin frame com antecedência suficiente para renderização. Modos fracionários mantêm agendamento racional; arredondar o parâmetro CEF, se a API exigir inteiro, não será apresentado como sincronismo exato.
- Selecionar imagem para o instante de conteúdo pretendido. Se for necessária história, usar fila limitada e medir memória em HD/UHD; definir política para imagem tardia (repetir última adequada e contar), nunca bloquear o callback à espera do CEF.
- Calibrar offset A/V no recetor com flash/impulso e comparar início/fim de ensaio. Identificar previamente o piso de erro do gerador browser e da quantização de vídeo.
- Só depois ensaiar redução de três para duas frames de reserva, com áudio/vídeo coordenados; uma frame é experiência posterior condicionada aos prazos medidos. Manter perfil conservador explícito; não aumentar/reduzir filas silenciosamente durante PLAY sem política e telemetria de transição.

**Aceitação proposta para validação do auditor:** offset A/V residual dentro de ±1 frame de saída, com incerteza declarada e sem deriva secular; variação início/fim inferior a uma frame no ensaio prolongado. São limites iniciais de projeto, não uma norma ou resultado já demonstrado. O perfil reduzido só é aceite se diminuir latência física medida, mantendo continuidade e contadores dentro dos critérios acordados. Se a cadeia de teste não resolver estes limites, melhorar a medição antes de aprovar.

### 7.9 Fase 6 — monitor local, transições e clareza dos níveis

**Casos:** A2, A3, A4, A5, A6, A7 e V4. **Ficheiros:** `Main.mm`, `Scene.*`, `interface.html` e testes dirigidos.

- Reestruturar criação/reconfiguração do AVAudioEngine num controlador serializado; observar mudanças de configuração, reconstruir formatos/ligações quando necessário e tratar erro sem impedir nova tentativa. Remover observadores e parar o motor quando não há destino monitorizado ou no encerramento.
- Usar host/sample time válido do callback para relacionar consumo local com timeline; substituir os 100 ms fixos por reserva dimensionada. A deriva do dispositivo local é independente da DeckLink e tem controlo próprio. Testar mudança de formato/taxa e não assumir que todos os destinos têm latência semelhante.
- Distinguir escuta PFL e escuta de programa; mostrar origem e estado. Se o vídeo local não acompanhar o mesmo instante, não apresentar a escuta como monitor A/V sincronizado. AirPlay exige medição/política própria e não condiciona o perfil SDI.
- Aplicar rampas configuradas inicialmente entre 5–20 ms a ganho/mute/visibilidade/PAUSE, sem lookahead. Comandos de UI são snapshots coerentes por bloco; ganhos suavizados por sample. Definir efeito de ações sucessivas antes de terminar a rampa.
- MONO da layer: média com dois canais ativos, duplicação unitária de um canal ativo e silêncio com nenhum. Manter L/R de OUTPUT como canais de saída e documentar a diferença.
- Medir pico/RMS antes de saturar, conservar estatísticas de origem e acrescentar OVER pós-ganho/output com retenção inicial proposta de 2 s. Contar clipping em amostras intercaladas e explicitar a unidade. Saturação PCM permanece; limitador com lookahead não faz parte do percurso obrigatório.
- Exibir PFL, layer oculta/fora de programa e canais efetivamente ativos. Faders em dB com posição −∞ e reset 0 dB, preservando compatibilidade de ganhos gravados em amplitude.

**Aceitação:** troca entre destinos disponíveis e retoma após falha/suspensão sem reiniciar app; motor parado quando sem escuta; monitor não consome SDI; MONO/canais verificados por matriz de casos; OVER acusa excesso real; rampas contínuas sem saltos adicionais nos testes de sinal. Ensaios de auscultadores, saída interna e AirPlay são registados separadamente conforme equipamento disponível.

### 7.10 Fase 7 — cor, navegação e documentação

**Casos:** V2, V5, D1 e conclusão T1.

- V2: preservar conversão atual até comparação de cinzentos, primárias e alpha no waveform/recetor. Se houver necessidade comprovada, acrescentar opção de transferência claramente definida, mantendo desmultiplicação/ordem de canais e compatibilidade de sessões. Testar BGRA/ARGB e alpha parcial; não confundir transferência com legal/full range do dispositivo.
- V5: testes CEF com frame principal e subframes HTTP(S), data, blob e srcdoc. Só alterar a política necessária aos casos reais aprovados; manter isolamento da interface nativa e as restantes restrições. Resultado “não reproduzido” é válido e não força mudança.
- Atualizar README, ARCHITECTURE, VERSIONS, VALIDATION, HARDWARE e BACKLOG para comportamento efetivo, métricas, perfis, limites e testes executados. Não reescrever nomes de commits publicados.
- Integrar no CTest os testes temporais/core e regressões; separar smoke gráfico, teste SDK, teste de escuta e aceitação física. Atualizar instruções do gerador AV conforme a versão efetivamente usada.

**Aceitação:** documentação descreve implementação final; cada alegação tem teste/evidência ou pendência explícita. Mudanças de cor/navegação têm revisão própria e não bloqueiam correções de temporização já validadas.

### 7.11 Matriz de testes e critérios de saída

| Grupo | Cenários mínimos | Evidência exigida |
|---|---|---|
| Timeline | Offset inicial grande, reinício de stream, quantização de PTS, relógio de parede ±1 h, suspensão/retoma | Posições e gerações esperadas; nenhum uso residual de parede para temporização de média |
| Pacotes | Regular, duplicado, sobreposição parcial, fora de ordem, lacuna, stream silencioso, ausência de stream | Cobertura, amplitude, contadores e política de recuperação corretos |
| Deriva | 0, ±20, ±100 ppm; mudança gradual e salto fora da faixa; diferentes fases de callback | Margem/razão/erro ao longo do tempo; aquisição e falhas claramente delimitadas |
| Driver simulado | Callback atrasado, FIFO reportado diferente do previsto, escrita parcial/zero, erro, perda de dispositivo | Cursor só avança pelo efetivamente escrito; sem perder/duplicar conteúdo por avanço incorreto |
| Roteamento | 0/1/várias fontes; ganhos/canais/MONO/master; PFL; PAUSE→STOP→PLAY; mudança entre percursos | PCM/níveis esperados, timeline contínua e estado independente por destino |
| Concorrência | CEF+SDI+monitor+medidores, edição de ganho, reload/remoção, fila cheia | Sem corrida/use-after-free/deadlock; armazenamento limitado e falhas contadas |
| Desempenho | Uma fonte, várias fontes, UI ativa, HD e UHD se suportado | CPU, memória, preparação p50/p99/máximo, prazos e latência antes/depois; callbacks sem trabalho proibido |
| Cadência | 25/50 e fracionárias suportadas; animação e imagem estática | Idade/repetição/drop por frame, agendamento racional e resposta a imagem tardia |
| Físico curto | Cada modo/perfil proposto, flash+impulso, canais/fase, PAUSE/reload, monitor disponível | Captura externa com taxa/resolução, offset A/V e latência total medidos |
| Físico prolongado | 2 h para diagnóstico inicial; pelo menos 8 h no modo principal, e 2 h nos restantes modos/perfis candidatos | Séries temporais, nenhuma perda inesperada dentro do envelope acordado, sem crescimento de offset/memória; limitações de cobertura declaradas |

Oito horas simuladas ou físicas não provam todos os equipamentos e cargas: o relatório final tem de identificar o envelope suportado. A aceitação de estabilidade exige zero underruns inesperados e zero recuperações não provocadas nesse envelope; testes de falha intencional exigem recuperação conforme a política, não contadores a zero. Os limites de CPU/memória e prazo de preparação serão fixados após baseline na máquina de referência, antes de escolher o perfil de baixa latência.

### 7.12 Entregas e pontos de validação do auditor

| Entrega | Conteúdo | Condição para avançar |
|---|---|---|
| P0 | Baseline, contrato temporal 7.2, plano de captura e critérios numéricos propostos | Auditor valida significado de D/J/L, PTS bruto, sinal A/V e limites de medição |
| P1 | Telemetria, A1/A10, separação áudio/imagem/persistência | Testes de estado/carga aprovados; baseline preservada |
| P2 | Relógio comum, continuidade e recuperação; harness partilhado | Testes de domínios/descontinuidades e driver simulado aprovados |
| P3 | Buffers/callbacks previsíveis e percursos simples | Equivalência funcional, lifetime e ganho medido; latência adicional contabilizada |
| P4 | Margem explícita, controlador e reamostrador | Deriva simulada/qualidade aprovadas; parâmetros e atraso documentados |
| P5 | Cadência, alinhamento A/V e perfis SDI | Medição externa curta aprovada antes de ensaio prolongado |
| P6 | Monitor, rampas, MONO, OVER/PFL/faders | Matriz de áudio/estados e testes locais aprovados |
| P7 | Cor/navegação quando justificadas, documentação e aceitação prolongada | Evidência final por requisito, pendências e perfis efetivamente aprovados |

Cada entrega deve conter: alterações por ficheiro, relação com os IDs da auditoria, comandos e resultados, comparação com baseline, parâmetros por defeito, riscos/limitações e forma de regressar ao estado anterior. A ordem é de dependências: P6 pode ser preparada após P3, mas o monitor final depende da timeline/deriva; P5 depende de P4. Redução de reserva e mudança de cor não devem ser misturadas na mesma entrega.

**Checklist para a resposta do auditor:**

- [ ] Aprovar/corrigir o contrato PTS/host/dispositivo e a equação D/J/L.
- [ ] Aprovar política de PAUSE/STOP, PFL, ausência de fonte e recuperação.
- [ ] Validar escolha/qualidade/atraso do reamostrador e envelope de deriva antes de o integrar.
- [ ] Aprovar medição flash/impulso e tolerâncias A/V propostas, incluindo incerteza do browser e da captura.
- [ ] Validar capacidades/limites de filas e a arquitetura de produtores/consumidores.
- [ ] Aprovar seleção automática do percurso simples e critérios de equivalência.
- [ ] Definir máquina, modos, carga, cadeia SDI e limites de desempenho de referência após baseline.
- [ ] Aprovar os perfis e valores finais D/J/L apenas com evidência; 40 ms não fica aprovado por este plano.
- [ ] Rever evidência por entrega e declarar separadamente aprovação de código e aceitação física.

O plano fica pronto para comentário do auditor nesta secção. A implementação e os ensaios físicos ainda não foram executados no âmbito deste plano.

## 8. Medição no HD Mini e recomendações finais do auditor

**Estado: parecer final.** O auditor não volta a validar entregas. Esta secção responde à
checklist do §7.12 e define o que passa a ser autoverificação de quem implementar. A
aceitação física continua a ser decisão do Luis.

### 8.1 Relógio do HD Mini medido (30/10/2026)

Ferramenta `tools/audio-audit/decklink_clock.cpp`: preto transparente e silêncio digital
nas saídas SDI, regressão do tempo do dispositivo contra `mach_absolute_time`. Dados
brutos em `tools/audio-audit/data/hdmini-clock-*.csv.gz`; resumo em
[tools/audio-audit](../tools/audio-audit/README.md). Estado do REF IN não registado.

| Execução | Relógio de stream | Relógio de hardware | Consumo de áudio | Frames atrasados / perdidos |
|---|---|---|---|---|
| 1080p25 BGRA, 600 s | +0,42 ppm | +0,42 ppm | +0,42 ppm | 0 / 0 em 14 999 |
| 1080p50 BGRA Fill/Key, 300 s | +0,28 ppm | +0,28 ppm | +0,28 ppm | 0 / 0 em 14 998 |

Resíduos dos ajustes abaixo de 0,07 ms. Os ajustes por minuto desceram de +0,51 para
+0,28 ppm em 15 min (aquecimento do oscilador, compatível). O áudio é consumido ao
ritmo exato do vídeo. Sinal positivo: o dispositivo é mais rápido que o Mac e o
mapeamento atual perde margem para a frente a 1,0–1,5 ms por hora. Simulação com
+0,42 ppm e o trace de atrasos de 120 s: com 3 ms de margem inicial, 1,7 % de amostras
em falta às 3 h e 20 % às 6 h; com 5 ms, 1 % às 4 h e 9,5 % às 6 h.

**Leitura dos riscos com esta medição:**

1. **3.1 é o risco imediato:** sem margem de origem, um único pacote atrasado (houve um
   de 30,7 ms) abre um buraco desde o primeiro minuto.
2. **3.3 pesa mais que a deriva da placa:** o `system_clock` afastou-se 3–12 ppm do
   relógio monótono, até 30 vezes o erro do HD Mini.
3. **3.2 é real mas lento nesta unidade:** horas, não minutos. Continua obrigatório
   para sessões longas, outras unidades e qualquer instalação em genlock, onde o
   dispositivo segue a referência e não o seu cristal.

### 8.2 Respostas à checklist do §7.12

| Item | Parecer |
|---|---|
| Contrato PTS/host/dispositivo e equação D/J/L (§7.2) | **Aprovado.** Nota: ancorar no PTS bruto sobe o piso de latência ~21,5 ms face à ancoragem atual na chegada; é o preço de pôr todas as layers no mesmo domínio e deve constar do orçamento. Domínio do host: `mach_absolute_time`, nunca `steady_clock`. |
| PAUSE/STOP, PFL, ausência de fonte, recuperação | **Aprovado como em §7.4/§7.9.** PAUSE pertence à saída SDI; Monitor OUTPUT segue o programa e volta a ouvir a mistura após STOP; PFL independente e rotulado. |
| Reamostrador e envelope de deriva | **Aprovado com critérios:** razão variável contínua (polifásico/sinc próprio, ou speexdsp/libsamplerate, ambas BSD — preferir a que não obrigue a alterar o empacotamento). Aceitação: ondulação ≤ 0,1 dB até 18 kHz, rejeição de imagens/aliasing ≥ 80 dB, atraso de grupo contabilizado em L, sem modulação audível ao variar a razão. Envelope de ensaio ±100 ppm; o medido é < 1 ppm. Interpolação linear rejeitada. |
| Medição A/V flash+impulso e tolerâncias | **Aprovado:** ±1 frame de saída como limite inicial, com incerteza declarada. Verificar primeiro se o SDI IN do HD Mini permite loopback com captura e reprodução simultâneas; se não, usar um único dispositivo de captura externo para som e imagem. |
| Capacidades das filas e produtores/consumidores | **Aprovado.** Requisito adicional: nenhuma falha ou bloqueio do monitor local pode atrasar a preparação SDI. |
| Percursos simples automáticos | **Aprovado só para o vídeo** (uma layer visível publicada diretamente; composição acionada por eventos). A via neutra de áudio fica adiada: o ganho de CPU é desprezável e duplica a superfície de teste. |
| Máquina, modos e limites de referência | **Definido:** este MacBook Apple Silicon + UltraStudio HD Mini; modos principais 1080p25 e 1080p50, mais 1080p59,94 para taxas fracionárias. Limites de CPU/memória fixados na baseline da P0. |
| Valores D/J/L | **Valores de partida, não finais:** D = 3 frames (atual); J = 40 ms (cobre o pior atraso medido, 30,7 ms, mais a quantização). Ajustar com o headroom registado sob carga real. |
| Evidência por entrega | Sem retorno ao auditor, cada entrega exige: `tools/audio-audit/run-models.sh` e CTest a passar; um registo de headroom de ≥ 30 min no HD Mini; comparação com a baseline; e a declaração separada de aprovação de código e de aceitação física (esta última do Luis). |

### 8.3 Recomendações finais

1. **Antecipar a correção de menor custo e maior efeito.** Logo depois da P1, implementar
   juntos o relógio monótono (`mach_absolute_time`, PTS bruto) e a margem J explícita.
   Com J = 40 ms e a deriva medida (1,5 ms/h), a margem para pacotes típicos (p99 1,6 ms)
   dura ~25 h e a margem para atrasos excecionais como o de 30,7 ms dura ~6 h. Resolve
   3.1 e 3.3 já, para sessões dentro desse horizonte, enquanto o controlo de deriva da P4
   é construído e validado sem pressa.
2. **Medir o δ real dentro da aplicação** antes de mexer nas filas: instrumentar o
   `AudioCallback` para registar o fim da janela pedida face ao último pacote recebido.
   É a métrica "source headroom" da P1, e confirma no próprio código o que hoje é
   simulação.
3. **Repetir a medição de relógio** com a referência usada em produção (genlock ligado,
   se existir) e após pelo menos 30 min de aquecimento. Os +0,3–0,5 ppm valem só para
   esta unidade, neste estado.
4. **Corrigir o V1 (cadência CEF a 60 fps)** cedo: a produção é a 25/50p e o judder vê-se
   no ar, independentemente do áudio.
5. **Fazer os ensaios longos com os contadores certos:** zero underruns e zero
   recuperações não provocadas, com o headroom estável dentro de ±5 ms ao longo de 8 h
   no modo principal. "Amostras não nulas" deixa de ser critério de aceitação.
6. **Correr as ferramentas DeckLink, a app e as verificações de ambiente fora do sandbox
   da shell.** Dentro dele o driver e partes de `/tmp` ficam invisíveis: deu
   "nenhum dispositivo" e "CMake ausente" nesta auditoria, ambos falsos.
7. **Concluir com a atualização da documentação em inglês** (D1). Este relatório fica
   em português como registo da auditoria.
8. **Instalar o CMake de forma permanente antes da P0.** Verificado a 30/10: `cmake` e
   `ctest` não estão no PATH; a única cópia é
   `/private/tmp/cmake-3.31.6-macos-universal/CMake.app` (3.31.6, funcional), e o
   `build/CMakeCache.txt` e o `build/Makefile` apontam para ela. O macOS limpa `/tmp`
   no arranque e periodicamente: quando essa cópia desaparecer, qualquer alteração ao
   `CMakeLists.txt` (as fases criam ficheiros novos e testes CTest) parte a build.
   Instalar o CMake 3.31.6 indicado no `docs/BUILD.md`, confirmar o SHA-256 que lá está,
   colocá-lo num local persistente com `cmake`/`ctest` no PATH, e reconfigurar `build/`
   com o comando do `BUILD.md` para o cache passar a apontar para o novo caminho.
9. **Disciplina de commits durante a implementação:**
   - Fazer commit deste relatório (incluindo o §7) e de `tools/audio-audit` antes de
     começar, num commit só de documentação/evidência.
   - Implementar num branch próprio (ex.: `development/v0.5-audio-timing`) criado a
     partir desse commit, não em `development/v0.2`.
   - Antes de cada entrega, verificar com `git status` que não há trabalho alheio por
     fazer commit nos ficheiros a alterar; trabalho paralelo vai em commit separado, com
     a sua própria mensagem, nunca misturado com a entrega.
   - Um commit (ou série curta) por entrega P0–P7, com os IDs da auditoria na mensagem
     (ex.: "P1: A1, A10, V3 …"), testes e `run-models.sh` a passar antes do commit, e
     redução de reservas ou mudanças de cor nunca no mesmo commit que correções de
     temporização.
   - Mensagens com autoria do Luis, sem `Co-Authored-By` de IA, terminadas com a linha
     `AI-Assisted Code`.

> **Correções do auditor a afirmações feitas durante esta sessão:**
>
> - "O CMake não está instalado / foi limpo de `/tmp`": **errado em parte.** A cópia
>   em `/private/tmp` existe e funciona; não estava visível porque a verificação correu
>   dentro do sandbox da shell, o mesmo efeito que escondeu o HD Mini. O problema real é
>   o da recomendação 8: não está no PATH e vive num diretório temporário.
> - "Há trabalho alheio por fazer commit em `Main.mm` e `interface.html`": **resolvido.**
>   Esse trabalho entrou nos commits `d1cf52f` e `952c948`. Por fazer commit ficam apenas
>   este relatório (§7 e §8) e as alterações em `tools/audio-audit`.

Com estas emendas, o plano do §7 fica aprovado pelo auditor para execução.

## 9. Arranque da implementação — 30/10/2026

Estado anterior preservado no commit `55dbb38`; implementação iniciada no branch
`codex/audio-timing`. Primeira parcela de P1: correção mínima A1 em STOP, expectativa
de áudio por roteamento efetivo (A10), configuração de áudio separada da imagem e
gravação consolidada a 500 ms (V3). A configuração visual idêntica deixa de forçar
recomposição. Introduzidas métricas de cobertura de origem e headroom da última
leitura, sem confundir amostras silenciosas com lacunas, e contagem de recuperações
de relógio separada de erros de API.

CMake 3.31.6 colocado em `third_party/` após verificação do SHA-256 indicado em BUILD;
build reconfigurada para esse caminho persistente. Build completa passou, CTest 3/3,
modelos da auditoria, smoke gráfico e smoke real no HD Mini passaram. O teste de
hardware passou a incluir STOP diretamente após PAUSE e verificação de reposição do
mixer. Evidência e limitações em `docs/VALIDATION.md`.

Esta parcela não conclui P1: falta série temporal/exportação e ensaio de headroom
de ≥30 min, métricas agregadas/filtradas e separação completa do estado PAUSE.
Também não altera relógios, margem J ou reserva de três frames. Não constitui
aceitação física A/V. O passo seguinte é fechar a telemetria/baseline na aplicação
antes de alterar em conjunto o domínio temporal e a margem explícita, segundo §8.

### 9.1 Telemetria por intervalo e estado PAUSE independente

Segunda parcela: PAUSE passa a pertencer ao callback de saída, sem desativar o mixer
partilhado. O monitor OUTPUT segue o estado do programa através de uma flag própria;
STOP repõe escuta, enquanto PFL continua independente. Acrescentados agregados por
intervalo (reads, lacunas, ausência de fonte, headroom mínimo/médio/máximo) e CSV escrito
fora dos callbacks, bem como profundidade/alvo de áudio reportados pelo backend.

O modo `--audio-baseline-seconds` permite ensaios entre 1 s e 8 h com a configuração
do smoke de hardware; `analyze_headroom.py` resume o CSV e calcula tendência das
médias por intervalo após warmup. A estabilidade não é inferida do exit code do smoke.

Ensaio preliminar de 10 s no HD Mini: zero frames de origem em falta, zero erros de
API e zero recuperações de áudio; **um frame de vídeo perdido**, presente já no primeiro
intervalo. Não é aceitação de estabilidade. CSV em
`tools/audio-audit/data/application-headroom-p1-short.csv.gz`. O headroom inicial
inclui preroll; a inclinação de um ensaio tão curto não é estimativa útil da deriva.
É necessário o registo de 30 min antes de alterar o mapeamento temporal.

### 9.2 Preparação testável da próxima fase

A aritmética de cursor/bloco/origem passa a estar em `AudioSchedule`, usada pelo
callback DeckLink e pelo simulador de deriva, mantendo o mapeamento atual. Os testes
cobrem avanço parcial/zero e recuperação do cursor. Acrescentado também o adaptador
`MediaClock`: uptime macOS em unidades de amostra, conversão sem overflow intermédio
e deteção testável de alteração entre uptime e relógio que inclui suspensão.

O adaptador de relógio ainda **não está ligado ao percurso da aplicação**; a mudança
de domínio e margem será feita em conjunto após fechar a baseline. Os testes do
core, a build completa e os modelos passaram; os resultados de deriva/jitter do
modelo mantêm-se após partilhar o agendador. A baseline de 30 min está a executar a
cópia isolada do commit `bc8ae57`, sem incorporar estas alterações.

### 9.3 Vídeo: composição por eventos e percurso simples

Composição deixa de esperar pelo polling de 16 ms: as fontes visíveis notificam o
worker, as notificações são consolidadas e uma configuração entretanto ultrapassada
não publica um resultado antigo. A subscrição usa sinais de lifetime independente,
com referências fracas e remoção explícita na reconfiguração/destruição. Uma única
imagem já no tamanho certo é reutilizada diretamente, sem passagem de composição.

O limite de pinturas CEF passa a acompanhar o modo escolhido (25/50 para os modos
principais; arredondamento inteiro, máximo 60 nos restantes). Não é phase locking;
external begin frame e validação física da cadência fracionária continuam pendentes.

Build e CTest 4/4 passaram. O novo teste cobre publicação, ocultação, ausência de
recomposição redundante e publicação concorrente com reconfiguração/destruição.
ThreadSanitizer passou sem erros reportados nesse cenário. O smoke nativo com CEF
passou com a composição por eventos e o novo limite de pintura. Ainda não existe
medição comparativa física de atraso/CPU nem aceitação SDI destas otimizações.

Microbenchmark de composição de uma fonte semi-transparente (`tools/scene_bench.cpp`,
Clang C++20 `-O2`, 100 iterações por tamanho): `bc8ae57` demorou ~4,16 ms em 1080p e
~15,41 ms em UHD por chamada; o percurso de reutilização não percorre os pixels e
ficou abaixo de 0,001 ms. Ambos devolveram os valores de pixels esperados. Isto mede
apenas esta rotina, não o atraso total nem o consumo de CPU da aplicação inteira.

### 9.4 Monitor local e indicações de escuta

Separadas criação/paragem/reconstrução do AVAudioEngine. Mudanças de configuração
do motor e o evento de despertar do macOS pedem reconstrução na main queue; eventos
de motores anteriores são ignorados e os observadores são removidos no encerramento.
O motor para quando não há fontes monitorizadas. Mantêm-se, por enquanto, a leitura
local histórica de 100 ms e o processamento atual no render callback: A3 e o controlo
do relógio local ainda não estão resolvidos por esta alteração de lifecycle.

O teste `--monitor-smoke-test` passou no dispositivo local: arranque, execução
explícita do handler de reconstrução e paragem quando termina a escuta, seguido do
smoke CEF/UI completo. Troca física de auscultadores/AirPlay e sleep/wake não foram
ensaiados. A UI identifica PFL nas layers, explica a posição de L/R e assinala HIDDEN
nas faixas ocultas. O teste nativo verifica a identificação PFL.

O Luis confirmou que **ainda não há loopback nem captura conjunta A/V disponível**.
A medição física do offset som/imagem permanece pendente; nem a baseline SDI nem o
teste do monitor local serão apresentados como aceitação de sincronismo A/V.

### 9.5 Baseline real de 30 min e regressão curta da nova build

Baseline numa cópia isolada de `bc8ae57`, com o relógio/mapeamento antigos, 1080p25
e PLAY de 1800 s. A série cobre 1800,0005 s entre pontos ativos, em 1749 intervalos.
**Zero lacunas de origem, ausência de fonte, erros de API, recuperações e frames de
vídeo atrasados/perdidos** nos contadores registados. Correram builds, testes e um
smoke monitor/CEF noutra instância durante a medição. Uma consulta SDK durante o
ensaio indicou perfil full duplex e referência desbloqueada; o estado da referência
não foi registado continuamente, nem foi ensaiada captura simultânea.

Após 5 s de warmup, headroom mínimo/médio/máximo por leitura: **55,25 / 57,79 /
86,08 ms**, sendo a média aqui a média das médias intervalares. As médias intervalares
ficaram entre **56,54 e 61,14 ms**; a regressão descritiva foi −3,62 ms/h. Incluindo
preroll, os extremos foram 7,98–106,67 ms. Não aplicar a banda ±5 ms aos extremos
brutos nem interpretar a regressão como medição isolada do cristal.

**Consequência para 3.1:** neste arranque existiu margem efetiva apreciável; a hipótese
de margem quase nula não descreve esta execução. O algoritmo continua sem margem de
origem explícita/controlada; não garante o mesmo offset noutros arranques/modos.
Antes de escolher J, contabilizar a margem existente e a idade dos PTS brutos, em vez
de acrescentar cegamente 40 ms ao percurso. Estes contadores não medem offset A/V
nem latência física.

Nova build correspondente a `34e9637`, PLAY de 10 s por modo:

| Modo | Lacunas de áudio / erros API / recuperações | Vídeo atrasado / perdido | Resultado |
|---|---|---|---|
| 1080p25 Fill/Key BGRA | 0 / 0 / 0 | 0 / 0 | Smoke passou |
| 1080p50 Fill/Key BGRA | 0 / 0 / 0 | 0 / 1 | Perda já no primeiro intervalo (~1,07 s após setup), causa por identificar |
| 1080p59,94 Fill/Key BGRA | 0 / 0 / 0 | 0 / 0 | Smoke passou |

Os smokes passaram, mas os testes curtos não aprovam estabilidade prolongada,
movimento nem sincronismo físico. Os 30 min da baseline anterior não são 30 min da
nova build. Instrumentar a perda inicial a 50p antes de reduzir reservas.

Evidência em `tools/audio-audit/data`: `application-headroom-p1-1800s.*`,
`application-regression-*-10s.*` e `single-layer-scene-benchmark.json`: CSV, logs
comprimidos, resumos, commits/binários e contexto de carga. Build, CTest 4/4, modelos,
ThreadSanitizer e smoke nativo/monitor passaram. A verificação final
`codesign --verify --deep --strict` passou após reaplicar o script de assinatura para
remover metadados Finder/ResourceFork de empacotamento.

### 9.6 Estado após esta parcela

Concluídos nesta parcela: telemetria/baseline P1, PAUSE independente, base testável de
agendamento/relógio, composição por eventos/percurso simples de vídeo, pedido de
cadência CEF, lifecycle do monitor e indicações PFL/HIDDEN. O pedido CEF é limite de
pintura, não sincronização de fase.

Pendentes: ativação conjunta da timeline monótona/PTS bruto e margem explícita;
continuidade de pacotes/recuperação temporal completa; reamostragem adaptativa;
callback do monitor sem alocações/mutexes; rampas, OVER, MONO/faders e restantes itens
do plano. Faltam regressão prolongada da nova build e investigação da perda inicial
a 50p. A aceitação física A/V depende do equipamento que o Luis confirmou ainda não
estar disponível. Não foi substituída a aplicação instalada.

## 10. P2 — relógio comum, aquisição de fase e continuidade de pacotes

### 10.1 Implementação do passo

- CEF entrega agora os PTS brutos, sem offset por layer nem heurística de 60 s.
  SDI, monitor e medidores usam `mediaSampleTime()` no domínio uptime validado.
- A idade de origem é independente da fila SDI: valor provisório **60 ms sobre PTS
  brutos**, ajustável para ensaio por `U2K_AUDIO_SOURCE_AGE_MS` (10–200 ms). Os 40 ms
  sobre timestamps ajustados não cobriam o máximo bruto observado de ~51,3 ms.
- Não se fixa definitivamente o mapeamento no primeiro callback. A aquisição espera
  pelo consumo de duas reservas e quatro observações coerentes de host/fila, rejeita
  consultas com duração >1 ms e só então ancora a origem. A alteração de fase é
  suavizada por crossfade de 240 samples (5 ms), sem acrescentar lookahead.
- Preroll tem horizonte limitado. Fila vazia com cursor ultrapassado pede recuperação
  para um instante futuro e nova aquisição. A posição consumida da fila é a referência
  de preparação; a fase face ao stream SDK é registada separadamente e não é uma
  medição física A/V. Correção contínua de taxa ainda não está implementada.
- Suspensão detetada pela diferença uptime/continuous limpa históricos e pára PLAY;
  o monitor é reconstruído. A deteção é testada com relógios injetados; não se suspendeu
  fisicamente o Mac nesta validação.
- Duplicados são descartados, packets antigos não apagam o histórico, variações de
  timestamp até 1 ms seguem um contador contínuo e sobreposições maiores são aparadas.
  Lacunas maiores continuam explícitas. Cada clear invalida a geração anterior,
  incluindo cópias de packets que ainda estavam em curso.
- Correções para trás podem usar o histórico retido, sem duplicar amostras. Padding
  anterior ao primeiro sample de um stream é contado separadamente das lacunas reais.
- Falha ao ativar um dispositivo ocupado termina o smoke com limpeza normal. O backend
  só desativa os recursos que conseguiu ativar, evitando desativar a saída de outro dono.

### 10.2 Resultados intercalares e critério de fecho

Os primeiros ensaios expuseram idades de leitura de ~74–149 ms apesar do offset de
60 ms. Calibrar no primeiro callback não resolveu o problema. A medição adicional
mostrou fase stream/fila praticamente alinhada no regime estável, não uma diferença
permanente comprovada do SDK. Era necessário aguardar a estabilização do arranque.

Com aquisição, os três smokes de 10 s passaram, com zero lacunas de áudio, erros,
recuperações ou perdas de vídeo nesses ensaios. Após calibração, as leituras ficaram
próximas de 60 ms; antes dela permanece uma fase de aquisição explícita. Estes números
são idade da janela pedida no callback, não latência física de saída.

Build isolada em `build-audio-timing/URL2KeyFill.app`; CTest 4/4 e modelos com PTS
brutos passaram, incluindo fase inicial de 80 ms, FIFO modelado e uma paragem
transitória do relógio no arranque. Os testes de publicação/clear/leitura concorrentes
passaram sob ThreadSanitizer e AddressSanitizer/UBSan. A baseline de 30 min da nova
candidata é o próximo critério de fecho deste passo; não se reutiliza a baseline P1.

Falta depois: compensação adaptativa de deriva, callback local previsível, qualidade
de mistura/níveis e alinhamento A/V final. Não há captura conjunta disponível para
aceitação física e a aplicação instalada não foi substituída. Alterações novas na
pasta `components/` feitas em paralelo não fazem parte deste passo.

### 10.3 Baseline P2 de 30 minutos — resultado e limitação

Candidata `64b87b1`, HD Mini, 1080p25, idade configurada de 60 ms e reserva
de 5760 samples. Evidência versionada em `tools/audio-audit/data/application-headroom-p2-1800s.*`.
O trace cobre 1799,60 s após warmup. Sem erros de API, recuperações, resets,
frames de vídeo tardios ou perdidos; sem duplicados/reordenação/ajustes de timestamps.
Porém, aos 1531,99 s foram contados **12 frames de áudio em falta (0,25 ms)**,
com headroom mínimo de −0,25 ms. A idade de leitura calibrada variou entre
58,5625 e 60,2292 ms. O smoke terminou normalmente, mas o critério de zero
lacunas **não passou**. Falta investigar a chegada/disponibilidade nesse intervalo;
não atribuir a causa a deriva ou jitter sem trace mais fino.

### 10.4 Captura física por HDMI ou SDI

A saída HDMI do HD Mini inclui áudio estéreo embebido e permite uma primeira
medição A/V, capturando flash e impulso sonoro gerados no mesmo instante de conteúdo.
É necessário gravar imagem e áudio embebido juntos; o capturador pode introduzir
o seu próprio desfasamento, que deve ser caracterizado. Especificação do fabricante:
https://www.blackmagicdesign.com/uk/products/ultrastudio/techspecs/W-DLUS-09

Esta medição caracteriza o percurso HDMI mais captura. Não demonstra que a fase
HDMI/SDI é idêntica. Para aceitação da saída SDI de produção, medir o SDI Fill com
áudio embebido, diretamente ou através de conversor cujo efeito seja caracterizado.
Flash/impulso na gravação mede desfasamento relativo A/V; latência absoluta desde
a origem exige também referência temporal de entrada comum.

### 10.5 Investigação das 12 amostras em falta

**Factos do trace:** uma única janela de reporte, aos 1531,992285 s, conta 12
amostras e headroom mínimo −12 samples. As janelas imediatamente adjacentes
têm mínimo ~39,4 ms. Não há resets, recuperação, mudança de calibração, erros
SDK, lacunas/ajustes nos timestamps ou perda de vídeo. O instante é o do reporte,
não o instante exato da leitura que falhou. A idade de leitura reportada é a
última do intervalo e não necessariamente a da falha.

**Reprodução controlada:** um pacote cobre [48000,48480); pedir [48012,48492)
antes de publicar o pacote seguinte deixa exatamente 12 samples sem cobertura.
O teste verifica tanto a contagem como as 12 amostras finais a zero, seguidas
de publicação com timestamp contínuo. Isto demonstra um mecanismo compatível
com os dados, não prova a causa histórica. Uma falta de disponibilidade transitória
é a hipótese principal; não há evidência de esgotamento progressivo da margem.
Não aumentar J por defeito nem introduzir saltos periódicos com base neste evento.

**Diagnóstico acrescentado:** snapshot fixo da última leitura em falta, com sequência,
geração, janela pedida, instante host da leitura, fim do pacote mais recente,
chegada antes da cópia/lock e publicação após inserir/aparar os históricos.
O primeiro pacote posterior que cobre a cauda em falta acrescenta chegada/publicação
e limites, mesmo se só chegar no intervalo de reporte seguinte. A sequência permite
reconhecer substituições: isto não preserva todos os eventos entre reportes nem
resolve retroativamente o evento original. Não há escrita em disco nem alocação
adicional neste registo de falha; os timestamps de pacote acrescentam duas consultas
ao relógio, cujo custo ainda falta medir em hardware.

**Validação:** build completa e CTest 4/4 passaram, incluindo reprodução exata do
défice, recuperação posterior, persistência do diagnóstico e isolamento por geração.
O smoke nativo passou captura CEF, arranque/reconstrução/paragem do monitor local
e o diagnóstico A/V gerado no browser; não é captura física.
O analisador conserva o resultado do trace histórico e calcula corretamente o
cenário sintético de 60 ms de idade/−0,25 ms de cobertura. O novo ensaio SDI não
arrancou: modo indisponível; `system_profiler SPThunderboltDataType` confirmou todos
os portos sem dispositivo ligado. Não se conta este ensaio como validação SDI.

**Próximo passo:** religar HD Mini e repetir a baseline com este diagnóstico, mantendo
60 ms e a reserva atual. Se ocorrer falha, comparar chegada/publicação com leitura,
idade nessa leitura, continuidade de timestamps e fase/fila. Caso a falha não se
repita, o mecanismo histórico continua por confirmar; um ensaio sem falhas isolado
não prova resolução. A medição física HDMI/SDI continua independente deste diagnóstico.

### 10.6 Repetição instrumentada concluída em 31 de outubro

Candidata `35e9156`, mesmo executável do smoke local validado, SHA-256
`3bb50ba2d96988e1bd11683c7f4dd400cc239de8170c032dfbba7f47f6275a1d`.
Ensaio reiniciado a pedido do utilizador; registo anterior preservado em `/tmp`.
HD Mini, 1080p25, J=60 ms, reserva 5760 samples. Evidência em
`tools/audio-audit/data/application-gap-diagnostic-2026-10-31-1800s.*`.

O ensaio terminou normalmente e fechou a aplicação; não ficou a correr após os
30 minutos. Trace ativo de 1799,900799 s, 1747 intervalos: **zero samples em falta**,
zero leituras sem fonte, erros API, recuperações, resets ou vídeo tardio/perdido.
Sem anomalias nos contadores de continuidade. Headroom mínimo 37,2917 ms,
máximo 144,1042 ms; tendência descritiva −1,2541 ms/h, não medição isolada
do cristal. Idade de leitura calibrada após warmup: 58,6875–60,0417 ms.

Passou o critério de zero lacunas nesta repetição. Não houve evento para o novo
diagnóstico caracterizar: a causa histórica das 12 amostras continua por confirmar.
Não se declara resolvida a falha transitória nem aceite o sincronismo físico A/V.
Mantêm-se pendentes compensação adaptativa de deriva e os restantes itens do plano.

### 10.7 Loopback SDI físico disponível — medição e conclusão

O Luis ligou SDI OUT a SDI IN do mesmo UltraStudio HD Mini. **Captura e reprodução
simultâneas funcionaram neste equipamento/configuração**. Foram acrescentados:

- `decklink_capture.cpp`: entrada YUV 8-bit e áudio estéreo int32/48 kHz, com
  timestamps SDK de imagem e áudio na escala 48000. Recolhe luminância no centro
  do flash e onsets de áudio em memória limitada; exporta depois de parar. Não
  ativa/desativa saída nem muda o perfil duplex ou as ligações configuradas.
- `--sdi-av-test`: uma única fonte bundled de sincronismo, no percurso normal
  CEF → composição/mistura → DeckLink; sessão smoke temporária, sem carregar a
  sessão de produção. Mantêm-se J=60 ms e as reservas existentes.
- `decklink_clock --av-reference 1`: referência sem CEF/mixer, com flash e som
  agendados na mesma timeline SDK. Na versão final, inicia reprodução a partir do
  callback de preroll quando o SDK reporta reservas suficientes, seguindo o exemplo
  `Mac/Samples/SignalGenerator` do SDK. O modo histórico de relógio/silêncio mantém-se.
- `run-loopback.py` e `analyze_loopback.py`: arranque coordenado, hashes/exit codes,
  logs e pares de eventos. O sinal positivo significa áudio depois da imagem.
  O analisador exige pelo menos cinco pares, ignora transições de vídeo sem frame
  predecessor contíguo e distingue gaps globais de gaps entre os pares medidos.

**Protocolo e ressalva de arranque.** Ativar a entrada antes de existir sinal deu
resultados diferentes entre arranques, mesmo na referência SDK: +20,69 e +80,67 ms
nos ensaios exploratórios de 50p. Foram preservados mas não usados como calibração.
Repetir com saída já estabilizada antes da captura deu ~0,7 ms na referência, a
25p e 50p. A causa exata desta dependência do arranque — relógio/reaquisição/driver
ou interação entre entrada e saída — não ficou isolada; não se atribui apenas aos
timestamps da captura. O runner final espera a confirmação de saída ativa e mais
4 s antes de abrir a entrada. A referência final usa ainda preroll por prontidão.

**Resultados aceites neste protocolo (valores brutos; referência não subtraída):**

| Percurso | Cadência | Arranque | Pares por canal | Áudio − vídeo |
|---|---|---|---:|---:|
| Aplicação | 1080p25 | A | 19 | +86,69 ms |
| Aplicação | 1080p25 | B | 17 | +42,13 ms |
| Aplicação | 1080p50 | A | 19 | +54,33 ms |
| Aplicação | 1080p50 | B | 8 | +71,44 ms |
| Referência SDK | 1080p25 | Final | 12 | +0,67 ms |
| Referência SDK | 1080p50 | Final | 9 | +0,73 ms |

L e R coincidiram. Dentro de cada execução, os pares detetados deram o mesmo valor;
**entre arranques, o offset da aplicação mudou**. Não houve gaps de timestamps de
vídeo entre os pares medidos. Os reportes globais podem incluir reaquisição ao
arrancar/parar; esses contadores não são uma contagem de frames SDI perdidos na saída.
A telemetria dos ensaios aceites da aplicação não registou lacunas de áudio, erros
API, recuperações ou vídeo tardio. A 25p não houve vídeo perdido; **cada ensaio a
50p registou uma perda de vídeo já no arranque**, antes da captura estabilizada.
Mantém-se portanto o problema inicial a 50p anteriormente observado; os timestamps
contínuos entre pares capturados não anulam essa perda na saída. O desfasamento
A/V não é identificado apenas pelos contadores de continuidade.

Uma repetição de 15 s a 25p foi rejeitada por ter menos de cinco flashes após
warmup. O flash de 40 ms nem sempre apareceu na captura a 25p, sem que o contador
SDI marcasse perdas; não se conclui só daqui onde se perdeu a atualização visual.
A repetição prolongada de 30 s passou o critério, sem mudar o sinal nem baixar o
mínimo de pares. A evidência rejeitada também foi preservada.

**Limites da conclusão.** Isto mede desfasamento relativo no percurso combinado
saída + entrada, não latência absoluta host→cabo nem atraso isolado da entrada.
A referência direta demonstra um residual pequeno nesse percurso sob o protocolo,
mas não é uma calibração independente só do ADC/capturador. O flash browser é
quantizado em frames (40 ms a 25p; 20 ms a 50p); timestamps/limiar dão casas decimais,
não essa precisão para o instante original de conteúdo. A envolvente/limiar de
áudio também contribui para o residual pequeno. O padrão de 1 Hz tem ambiguidade
de ciclo; emparelhamento pressupõe |offset| <400 ms. Não se mede deriva entre dois
equipamentos independentes usando o mesmo dispositivo como emissor/recetor.

**Conclusão de implementação.** A ausência de cortes P2 está verificada nos ensaios
referidos, mas **o sincronismo físico A/V da aplicação não está aceite**. Os valores
não sustentam uma compensação global fixa de 80 ms. O código seleciona a última
imagem disponível (`Frame` tem sequência, sem timestamp de conteúdo), enquanto o
áudio tem timeline/reserva/idade explícitas; isso confirma uma assimetria no código,
mas não quantifica por si só toda a causa dos offsets medidos. Prosseguir com:

1. Rever prontidão/preroll e relação de fase no arranque da aplicação, comparando
   com o exemplo SDK e registando a associação usada na seleção de ambos os meios.
2. Implementar o alinhamento previsto em P5: carimbo temporal de imagem preservado
   na composição, histórico limitado e seleção para o mesmo instante pretendido
   que o áudio. Um carimbo de chegada do paint é uma aproximação, não PTS de conteúdo;
   explicitar essa incerteza e calibrar o residual por cadência/percurso.
3. Validar a correção por vários arranques a 25/50p e pela captura estabilizada,
   antes de reduzir reservas. Não reduzir J=60 ms arbitrariamente para esconder
   o desfasamento: o máximo bruto de chegada medido antes era ~51,3 ms. Os passos
   de deriva, previsibilidade e qualidade do plano continuam pendentes.

**Evidência:** `tools/audio-audit/data/loopback-2026-10-31/`, incluindo CSVs/logs
comprimidos, manifests com hashes, resultados por pulso, resumo e ensaio rejeitado.
Build completa passou, probes compilaram com `-Wall -Wextra`, oito testes do
analisador e CTest 4/4 passaram. Não foi aplicada compensação A/V neste passo nem
substituída a aplicação instalada. Os ensaios pararam e libertaram o dispositivo.

### 10.8 Correção do arranque autorizada — prontidão, frame zero e antecedência inicial

**Âmbito autorizado:** corrigir o arranque e repetir ensaios 25/50p. Não foi
implementado o histórico/seleção temporal de imagem da P5 nem compensação fixa A/V.

**Investigação e passos que não chegaram.** A sequência anterior chamava
`RenderAudioSamples(true)` manualmente, terminava preroll e iniciava playback sem
esperar confirmação conjunta das filas; o worker de vídeo nascia depois do Start.
A nova sequência segue o exemplo SignalGenerator do SDK: ativa ambos os fluxos
antes de agendar vídeo, prepara o worker e deixa o callback real de preroll iniciar
playback apenas após o SDK reportar reserva de áudio completa e três frames de
vídeo. A espera pela confirmação tem timeout de 5 s; falhas do callback acordam a
espera. O cleanup também deteta playback iniciado durante uma corrida com o timeout.

Essas alterações, **isoladamente, não eliminaram a perda**. Foi acrescentada
identificação atómica do índice agendado por slot. Os ensaios seguintes localizaram
consistentemente a perda no **frame de programa 0**, e não num frame em regime.
Houve seis arranques curtos sem perdas após mudar a ordem de ativação, mas um
loopback A/V voltou a perder o frame 0: por isso os seis não foram usados para
fechar o tema. A preparação física por um frame transparente síncrono, isoladamente,
também voltou a mostrar uma perda do frame 0. Estas falhas estão preservadas.

A referência SDK passou seis ensaios de 5 s a 50p (dois por combinação de reservas
vídeo/áudio: 3/2880, 4/2880, 3/9600). Não se encontrou fundamento para aumentar as
filas em regime. Foram adicionadas opções de reserva apenas à ferramenta de
referência; os seus defaults e as reservas normais da aplicação não mudaram.

**Correção final desta parcela:**

- Antes do programa, `DisplayVideoFrameSync` apresenta um frame transparente preto,
  reutilizando um dos três slots. É preparação explícita, com contagem e duração
  próprias; não é um frame de programa subtraído aos contadores.
- O primeiro frame de programa conserva **índice 0**, mas é agendado para uma imagem
  depois de stream zero. Os seguintes mantêm cadência racional; a recuperação do
  cursor de vídeo usa a mesma relação entre índice e instante.
- Até esse instante é agendado silêncio de áudio explícito: **960 samples / 20 ms
  a 50p**, **1920 / 40 ms a 25p**. As escritas, incluindo parciais, terminam exatamente
  no limite silêncio/PCM. O contador é separado do padding anterior ao stream.
- A origem provisória conserva o primeiro instante de origem de programa durante
  esta antecedência. A aquisição de fase espera pelo padding e duas reservas
  consumidas; depois mantém o orçamento J=60 ms existente. O início continua a ter
  aquisição/transição de fase, não sincronismo de conteúdo já garantido.
- O programa não disputa o deadline de stream zero. **Não se descarta o seu frame
  0, não se ignora um resultado SDK e não se reduz o contador dropped.** Se o SDK
  perder o primeiro frame de programa, `first_dropped_frame` continua a indicar 0.
- Permanecem três frames de vídeo, áudio 5760 samples a 25p / 2880 a 50p e J=60 ms.
  Acrescenta-se preparação/antecedência no início, sem uma fila adicional permanente
  ou DSP no percurso estável. Em cadências fracionárias, o silêncio inicial arredonda
  para o sample seguinte; a validação física desta parcela foi apenas 25/50p.

**Validação da candidata final:** quatro loopbacks (dois por cadência) mais seis
arranques consecutivos da fonte A/V a 50p. **10/10 terminaram normalmente**, com
zero perdas/atrasos de vídeo, lacunas de áudio, erros API ou recuperações. Confirmados
pelo SDK três frames e a reserva de áudio correspondente antes de Start. Nos dez
ensaios, a chamada de preparação síncrona demorou **11,29–11,60 ms**; a isto soma-se
preroll e a antecedência inicial acima, não se trata da latência completa de PLAY.

| Cadência | Arranque final | Pares/canal | Áudio − vídeo bruto |
|---|---|---:|---:|
| 1080p25 | A | 23 | +52,31 ms |
| 1080p25 | B | 23 | +65,46 ms |
| 1080p50 | A | 18 | +46,71 ms |
| 1080p50 | B | 18 | +76,00 ms |

Os canais coincidiram e não houve gaps de timestamps entre os pares capturados.
**A perda inicial não se reproduziu nesta amostra; o desfasamento de conteúdo A/V
continua presente e variável entre arranques.** Não se declara eliminada qualquer
possibilidade de perda sob outras condições, nem aceite o sincronismo final.
A antecedência é uma mitigação validada nestes ensaios, não prova isolada de uma
causa interna específica do firmware/SDK. Aplicam-se os limites de medição da §10.7.

Build completa/assinatura local, CTest 4/4 e oito testes do analisador passaram.
Novos testes exercitam silêncio de 960/1920 samples, escritas parciais de 137 samples
e transição para PCM sem atravessar/repetir o padding. O backend passou compilação
`-Wall -Wextra`. A análise preserva o caso histórico de 12 amostras em falta e deteta
corretamente frame zero nos casos intermédios. Não se induziu timeout/falha API no
hardware; esses caminhos não têm validação por injeção física nesta parcela.

**Evidência:** `tools/audio-audit/data/startup-2026-10-31/`, com hashes da candidata
e fontes, dez casos finais, falhas intermédias e ensaios das reservas de referência.
A aplicação instalada não foi substituída. Os processos de teste pararam e libertaram
os recursos que ativaram.

**O que se segue, pendente de confirmação do Luis:** alinhar a seleção temporal do
vídeo com o áudio (P5), começando por carimbo temporal/histórico limitado e relação
entre instante de conteúdo e apresentação. Repetir loopbacks por vários arranques
para validar o residual e a quantização. Não avançar automaticamente para essa
implementação nem considerar este passo como correção completa do sincronismo A/V.

### 10.9 Resposta de implementação — seleção temporal de vídeo e prazo SDI (31/10)

**Estado: infraestrutura P5 implementada e ensaiada; sincronismo final de conteúdo
A/V ainda não aceite.** Este passo foi autorizado pelo Luis. A chegada de uma imagem
CEF não identifica o instante do conteúdo nela representado; alinhar carimbos de
chegada não resolve, por si só, o desfasamento observado. Não foi aplicado um offset
fixo para esconder esse residual. A aplicação instalada não foi substituída.

**Alterações concretas.** `OnPaint` recebe um carimbo de chegada no domínio comum
48 kHz/uptime. Resize preserva-o; composição usa o mais recente das fontes. Uma
reconfiguração invalida o histórico e estabelece uma barreira temporal. O monitor
continua a usar a imagem mais recente; a saída escolhe a imagem histórica mais
próxima de `audioSourceOrigin + tempo SDI agendado`, com antecedência máxima de meio
frame, sem esperar por futuras imagens, e empate a favor do passado. O mapeamento
atual é 1:1: a futura correção adaptativa de taxa terá de atualizar áudio e vídeo.
Falta de imagem adequada ou tamanho incompatível produz transparente e contadores
separados de arranque/regime. Imagens estáticas continuam reutilizáveis.

Histórico limitado a 16 entradas, janela nominal de 500 ms preservando o predecessor
estático, 192 MiB de imagens raw ou 384 MiB incluindo SDR durante a saída. O limite de
memória pode reduzir a cobertura temporal em resoluções elevadas; não constitui
aceitação UHD/8K. A conversão SDR é preparada pelo produtor, fora do mutex do
histórico, e no START para imagens já existentes. O worker copia pixels preparados,
incluindo stride SDK, e conta qualquer fallback de conversão. STOP liberta a cache
SDR. O caso simples de uma única fonte com tamanho correto conserva a reutilização
da imagem imutável, evitando recomposição. Não se acrescentou DSP nem processamento
de áudio: retirou-se conversão repetida do caminho com prazo, em troca de RAM limitada.

Durante SDI ativo, incluindo PAUSE, o macOS mantém uma atividade user-initiated /
latency-critical e o worker pede QoS user-interactive. STOP, erro e saída libertam a
atividade. Isto protege a execução contra suspensão por inatividade/App Nap; não
impede suspensão forçada nem prova que App Nap tenha causado as falhas anteriores.
A deteção de suspensão existente mantém-se. Referência:
[Apple, atividades da aplicação](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/PrioritizeWorkAtTheAppLevel.html).

**Protocolo corrigido de medição.** A entrada espera um segundo consecutivo de sinal
válido (deadline seis segundos), faz Stop/Flush/Start e só então inicia a medição.
O runner termina a captura um segundo antes de parar a saída. Qualquer no-signal
registado dentro da medição invalida-a; o próprio StopStreams já não cria um evento
artificial no fim. Settle aceita 1–10 segundos. O analisador de pipeline rejeita
mudanças de origem de áudio e não subtrai medianas como atraso exato quando janelas
contêm fases discretas diferentes. A instrumentação de flashes/pulsos só está ativa
no teste CLI, sem análise adicional de pixels/amostras no uso normal.

**Quatro ensaios finais independentes**, mesma candidata
SHA-256 `4a3bde14913b1079924b3063ed32fd36834562f3ac032726708d2d731e179217`,
SDI OUT→IN do HD Mini, sem caffeinate externo. Valores negativos significam áudio
antes do vídeo. A coluna da origem compara chegada do flash com PTS do áudio, não
PTS real do conteúdo visual.

| Ensaio | Pares capturados/canal | Origem flash−áudio, mediana | A−V agendado, mediana | A−V capturado, mediana |
|---|---:|---:|---:|---:|
| 1080p25 A | 21 | +33,646 ms | −24,458 ms | −23,875 ms |
| 1080p25 B | 21 | +18,000 ms | −2,625 ms | −2,063 ms |
| 1080p50 A | 15 | +32,167 ms | −35,000 ms | −34,417 ms |
| 1080p50 B | 16 | +44,083 ms | −46,250 ms | −45,646 ms |

Nos quatro: zero amostras de áudio em falta, erros API, recuperações, resets de
relógio, vídeo late/dropped, falhas de histórico em arranque/regime ou fallback SDR.
Atividade e pedido de QoS confirmados ativos, atividade libertada em STOP. Memória
retida máxima observada: 232 243 200 bytes a 25p / 265 420 800 a 50p.
Nos casos de fase única, captura menos agendamento foi +0,563 a +0,604 ms, incluindo
a entrada e os limiares de deteção. Em 50p A, agendamento varia −55/−35 ms: a diferença
conservadora é [−19,417; +20,583] ms; não se afirma um atraso exato pela diferença
das medianas. Estes ensaios mostram cumprimento do agendamento nesta amostra,
**não conteúdo A/V finalmente alinhado**. Também não medem latência absoluta
host→fio, drift com relógio independente ou sincronismo com um recetor externo.
As fases distintas impedem comparar diretamente estas medianas com as da §10.8
como uma redução controlada de atraso.

**Ensaios rejeitados e limites, preservados na evidência:**

- Histórico raw com conversão no worker: 17 drops a 50p. Cache SDR sem a proteção
  final de atividade: outro caso com cinco drops, três late e duas recuperações de
  áudio. Não foram usados para aceitar o resultado nem omitidos dos dados.
- Um arranque demorou 432 s, registou suspensão/reset de relógio e não capturou
  conteúdo. Descartado; não valida tolerância a suspensão durante saída ativa.
- Entrada sem estabilização apresentou 11 no-signal e divergência de 42,646 ms entre
  agendamento e captura. Num controlo com saída de referência contínua, quatro
  reinícios só da entrada deram 0,688 / 10,917 / 0,667 / 0,667 ms; o segundo teve
  22 no-signal. Após estabilização/reset, quatro reinícios deram 0,667 / 0,688 /
  0,688 / 0,688 ms. Isto apoia o novo protocolo nesta amostra, sem atribuir
  exclusivamente a causa ao relógio da entrada ou excluir interação do hardware.
- Experiência IOSurface/OnAcceleratedPaint: timestamp relativo ao início da captura,
  sem origem absoluta exposta no caminho ensaiado; houve ainda uma recuperação de
  áudio. Experiência totalmente revertida, com patch guardado como evidência.
  Não se introduziu ancoragem heurística à primeira chegada nem offset por camada.

**Validação:** build nativa completa e assinatura local, CTest 4/4, nove testes do
loopback, quatro do pipeline, SceneEvents com TSan e ASan/UBSan e smoke da interface/
monitor passaram. Os testes cobrem limites/empates/antecedência, imagens estáticas,
revisões, orçamento, concorrência clear/publish/read, equivalência SDR BGRA/ARGB e
libertação da cache. O smoke normal cobre composição/alpha/visibilidade e ciclo do
monitor. Não há aceitação de carga prolongada, UHD ou drift independente nesta etapa.

Evidência: `tools/audio-audit/data/video-timeline-2026-10-31/`, incluindo resumo,
hashes, quatro casos finais, controlos de reinício da entrada, experiências
rejeitadas e logs dos sanitizers/smoke. A candidata final usa o caminho CPU original;
o executável reconstruído coincide byte a byte com o testado.

**O que proponho a seguir, aguardando confirmação do Luis:** preparar uma referência
A/V com PTS de conteúdo conhecidos, reproduzi-la pelo mesmo CEF e comparar conteúdo,
chegada, agendamento e loopback em vários arranques. Só depois decidir entre obter
um relógio de conteúdo utilizável, alterar a fonte de teste ou calibrar um residual
comprovadamente estável. P4 de taxa adaptativa e a aceitação final P5 continuam
pendentes; esta etapa não os declara concluídos.

### 10.10 Resposta — referência com PTS de conteúdo conhecidos (31/10)

**Etapa autorizada concluída; diagnóstico, sem nova compensação de produção.**
Foi acrescentada uma referência com vídeo VP8 e PCM16 estéreo no mesmo Matroska,
reproduzida num elemento `<video>` pelo CEF normal. Evita gerar o flash consultando
`AudioContext.currentTime`, como faz o teste anterior. A combinação de codecs e
contentor é suportada pelo Chromium e foi confirmada na build CEF instalada
([documentação Chromium](https://www.chromium.org/audio-video/)). Não requer ffmpeg,
instalações ou serviços externos: o próprio CEF codifica duas imagens keyframe
independentes, valida-as com VideoDecoder, constrói o ficheiro e reproduz o Blob.
O servidor temporário está ligado apenas a 127.0.0.1 e guarda a evidência da fonte.

**Referência verificável:** 60 segundos, 1920×1080, 50 fps, áudio PCM16 L=R a 48 kHz,
preto no primeiro segundo; depois flash branco e impulso de 40 ms a cada segundo.
Vídeo com PTS em passos de 20 ms; áudio em blocos contíguos de 960 samples. O início
matemático do impulso coincide com o flash. O primeiro sample acima do limiar
0,003 FS é o quinto, logo o detetor deve medir **+0,104167 ms na fonte**, antes de
qualquer reprodução/transporte. Um verificador Python separado percorre o EBML,
valida codecs/formato, todos os PTS, identidade das imagens VP8 e a forma de onda
PCM, canais, silêncio e os 59 onsets. A página valida o centro preto/branco das duas
imagens após descodificação; não se afirma uma descodificação independente dos 3000
frames. O ficheiro exato está preservado, comprimido, na evidência.

A fonte foi idêntica nos quatro arranques, SHA-256
`2db4764955c9c8fc71ed06ad1966d75a2ab7d08c55fcc9e2fbf959d2ad354a64`.
A candidata testada tem SHA-256
`2d1645c4965d015ccdc63a1bb9dc0729ab9d5280e9cdbe0fb62ba9ac93439121`.
O hash exato do executável está também nos manifests/resumo; estes são a referência
mecânica para identificação da build.

| Ensaio de 30 s | Pares/canal | Chegada flash−PTS áudio, mediana | A−V agendado | A−V capturado |
|---|---:|---:|---:|---:|
| 1080p50 A | 21 | −22,896 ms | +19,813 ms | +20,396 ms |
| 1080p50 B | 21 | −13,625 ms | +15,813 ms | +16,417 ms |
| 1080p25 A | 20 | +17,229 ms | −25,083 ms | −24,521 ms |
| 1080p25 B | 19 | +15,542 ms | +3,750 ms | +4,333 ms |

Positivo significa áudio depois do vídeo. Todos tiveram zero amostras em falta,
erros API, recuperações, late/dropped SDI, resets, falhas de histórico ou fallback
SDR. A diferença captura−agendamento foi +0,563 a +0,604 ms, incluindo a entrada
não calibrada e os limiares. Não se deve subtrair automaticamente esse valor como
latência do recetor. Os eventos `requestVideoFrameCallback` preservam mediaTime,
presentationTime e expectedDisplayTime no log; não se confundem com PTS absoluto
nativo nem são usados para corrigir imagens. Há saltos na apresentação inicial da
fonte; a aceitação dos contadores SDI não significa zero frames omitidos no browser.
As janelas analisadas excluem o aquecimento inicial, como na etapa anterior.

**Conclusão factual:** o desfasamento variável continua com conteúdo cujo alinhamento
no ficheiro é conhecido. A fonte anterior não pode, portanto, ser a única explicação.
O agendador/captura reproduz o desfasamento previsto pelo mapeamento atual, mas a
chegada de OnPaint ainda representa a entrega da imagem e não o seu PTS de conteúdo.
A fase de apresentação/seleção é relevante, incluindo a quantização de 40/20 ms.
Estes quatro arranques não isolam causalmente cada buffer CEF/OS nem demonstram
latência absoluta ou drift independente. A variação observada **não sustenta um
offset fixo universal, nem sequer uma calibração estável por modo nesta amostra**.
Reduzir reservas de áudio indiscriminadamente não resolve a ausência de PTS visual
nem tem validação nesta etapa. Não foram alterados mixer, reservas, atraso de fonte
ou política temporal de saída da §10.9.

**Implementação e validação:** override `--sdi-av-source-url` exclusivo de
`--sdi-av-test`; logging de referência exclusivo desse diagnóstico; runner local,
fonte HTML e verificador independente. Build nativa e assinatura verificada,
CTest 4/4, analisadores 9+4 testes e verificador 4 testes passaram. Este último usa
o ficheiro preservado e rejeita truncagem, timebase e sample rate incorretos. Smoke
normal da interface/monitor/CEF passou. O log contém um aviso de validação de
assinatura macOS; a verificação codesign e o processo completo terminaram com sucesso.

Evidência: `tools/audio-audit/data/content-reference-2026-10-31/`, com fonte comum,
manifests, hashes, CSV/logs, verificação e decomposição de cada arranque. A aplicação
instalada não foi substituída; os processos de teste terminaram.

**Próximo passo proposto, pendente de confirmação do Luis:** protótipo de associação
entre cada imagem efetivamente recebida por OnPaint e o seu PTS de conteúdo, primeiro
nesta referência controlada, com identificação inequívoca de frame. Verificar se a
seleção por conteúdo reduz o residual aos limites de quantização, e depois avaliar
como obter essa informação para fontes reais. Uma página arbitrária pode não expor
um relógio de conteúdo utilizável; não se promete uma correção universal baseada
apenas em requestVideoFrameCallback ou num offset medido num arranque.

### 10.11 Resposta — associação inequívoca de imagem a PTS, protótipo autorizado (31/10)

**Protótipo concluído; sincronismo final continua por aceitar.** Foi demonstrado
que uma imagem recebida por OnPaint pode ser associada ao seu PTS de conteúdo
quando a fonte fornece uma identificação verificável. Isto não acrescenta PTS a
páginas arbitrárias. A seleção normal continua por chegada; o caminho experimental
exige flags CLI explícitas e a referência controlada, sem alteração à aplicação
instalada, às reservas SDI ou ao atraso de fonte de 60 ms.

**Protocolo da prova.** Nova referência VP8/PCM de 60 s, 50 fps, com vídeo 320×180
escalado pelo browser para 1920×1080. Cada imagem contém header de 16 bits, ID de
12 bits e verificação de 12 bits num código visual; o ID determina PTS = ID×960
samples. Cada um dos 3000 frames é keyframe e foi descodificado no CEF antes da
reprodução, validando ID, PTS e centro preto/branco. OnPaint lê duas linhas do código
nos pixels efetivamente recebidos, rejeitando níveis ambíguos, geometria ou check
inválidos. Não associa o frame a uma mensagem JavaScript próxima no tempo.

Os blocos PCM de 20 ms transportam header de 32 bits, ID absoluto de 16 bits e check
de 16 bits no bit menos significativo dos samples 200–263, igualmente em L/R.
A alteração é no máximo um LSB PCM16 (−90,3 dBFS), abaixo do limiar do detetor, sem
mudar o início do impulso no sample cinco. É uma marca digital exclusiva do teste,
sem DSP novo no áudio normal. Um verificador separado confirma os 3000 IDs PCM,
PTS contíguos, forma de onda e 59 onsets; o vídeo tem payloads distintos e a validação
descodificada de todos os IDs fica no manifesto/log da fonte.

O relógio de diagnóstico reconhece a posição absoluta de cada marca no áudio aceite
pela AudioLane. Origem = PTS do sample reconhecido − posição no bloco − ID×960.
Exige três marcas consistentes; não usa a primeira chegada nem conta impulsos para
adivinhar o segundo. Marcas ausentes com origem posterior consistente são contadas
à parte; ordem/origem incoerente invalida o relógio. O ensaio por conteúdo não inicia
sem relógio e interrompe a medição se ele se perder ou mudar. Algumas marcas não
foram reconhecidas, incluindo durante o arranque: não se declara preservação bit a
bit de todo o PCM pelo CEF. Os IDs absolutos reconhecidos dispensam essa suposição.

`Frame.paintTime` conserva a chegada; `Frame.contentTime` é separado e só é definido
neste modo. Copy/resize e alias de fonte única preservam-no; composição de várias
fontes deixa-o indefinido. O histórico usa contentTime quando existe, mantendo as
barreiras de configuração, limites de RAM e preparação SDR. Os IDs efetivamente
selecionados são guardados num buffer de diagnóstico pré-reservado de 4096 entradas
e escritos após STOP, sem I/O de log no prazo do worker SDI. O log de OnPaint é
exclusivo do diagnóstico. O analisador verifica **todos** os agendamentos da janela
7–29 s, sem lacunas: 1100 a 50p ou 550 a 25p, além da concordância marcas/impulsos,
origem estável e saúde I/O. O horizonte de 4096 registos limita esta instrumentação.

**Comparação na mesma candidata e no mesmo ficheiro**, seis ensaios de 30 s.
SHA-256 da candidata: `e5bf0fee773d5be8fcfe89e19afa87c63f27eefa3b9ddb59235843fe0b0b7637`.
SHA-256 comum da fonte: `251697aae5992e03e1004be26d7ae0c38381b4ac30bb3640e45b8ab813ba0a1f`.
A fonte nova é distinta da §10.10; a comparação controlada desta etapa é entre as
linhas seguintes, não uma redução isolada face aos números da fonte anterior.

| Modo / seleção | Pares/canal | A−V agendado | A−V capturado |
|---|---:|---:|---:|
| 50p / chegada | 21 | +17,167 ms | +17,771 ms |
| 50p / conteúdo A | 21 | −5,479 ms | −4,875 ms |
| 50p / conteúdo B | 21 | −6,063 ms | −5,458 ms |
| 25p / chegada | 20 | −36,500 ms | −35,917 ms |
| 25p / conteúdo A | 21 | −16,396 ms | −15,813 ms |
| 25p / conteúdo B | 20 | −30,292 ms | −29,729 ms |

Positivo = áudio depois do vídeo. Todos: zero áudio em falta, erros API,
recuperações, resets, vídeo SDI late/dropped, falhas de histórico ou fallback SDR.
Nenhuma mudança da origem de conteúdo foi aceite. Houve uma marca intermédia de
áudio não reconhecida por ensaio, contada sem a confundir com mudança de relógio.
Captura−agendamento ficou +0,563 a +0,604 ms, incluindo entrada/limiares; não é uma
calibração independente da entrada nem uma correção aplicada ao resultado.

**O que os IDs esclarecem:** a 50p, o primeiro frame branco recebido correspondeu
ao início do flash; a seleção por conteúdo teve erro constante −5,583/−6,167 ms
face ao alvo, compatível com a quantização disponível nesta amostra. A 25p, os três
ensaios receberam primeiro o frame branco **20 ms depois do início de conteúdo do
flash**: o primeiro frame da fonte de 50 fps não entrou na pintura de 25 fps.
No conteúdo B, o frame selecionado estava 10,396 ms antes do alvo; somando o início
visual disponível 20 ms tardio, prevê-se A−V de −30,292 ms, tal como agendado e
capturado. Portanto, **PTS correto é necessário para esta prova, mas não basta
para recuperar frames que o CEF não entregou nem para fechar o sincronismo a 25p**.
Não se aplica um offset fixo, não se declara eliminada a quantização e não se aceita
uma solução universal para fontes reais ou múltiplas camadas.

Um controlo exploratório anterior mediu +37,146 ms a 50p, mas o primeiro verificador
de relógio confundia um ID de marca ausente com mudança de origem. A origem observada
continuava igual. A classificação foi corrigida e repetiu-se o controlo na candidata
final; o caso exploratório, com outro hash, foi preservado e excluído da comparação.

**Validação:** build nativa/assinatura local verificada, CTest 4/4, testes Python
9+4+6+7, AudioDiagnostics e SceneEvents com ASan/UBSan e smoke normal da interface/
monitor passaram. Testes cobrem marcas atravessando packets, origem inconsistente,
ID/check inválido, clear, geometria/limiares, preservação de PTS e ausência de PTS
composto, stamp incorreto, desacordo com impulsos, I/O degradado e lacunas na
cobertura de agendamento. Não houve ensaio prolongado, drift independente ou teste
de páginas reais com este contrato de conteúdo; o formato visual é específico de
1920×1080 e não se generaliza a outras viewports.

Evidência: `tools/audio-audit/data/identified-pts-2026-10-31/`, com fonte comprimida,
hashes, seis casos finais, identificadores de publicação/agendamento, verificação,
caso exploratório e logs de checks. Os processos de ensaio terminaram.

**Próximo passo proposto, aguardando confirmação do Luis:** repetir a prova com a
cadência da fonte igual à saída (25 fps→25p), para isolar a omissão do primeiro frame
do flash e separar esse efeito da quantização SDI. Só depois escolher o contrato
viável de timestamps para fontes reais. O protótipo não é promovido automaticamente
a comportamento de produção; P4 e aceitação final P5 continuam pendentes.

### 10.12 Entrega autónoma por prioridades — fiabilidade, qualidade e velocidade (31/10)

**Âmbito autorizado pelo Luis:** implementar e testar de seguida as alterações,
priorizando deriva, desfasamento e latência, sem iniciar o ensaio físico de oito
horas. Esse ensaio continua reservado para decisão posterior. A entrega implementa
correção de deriva SDI, calibração A/V em direto, percurso de silêncio e dois perfis
explícitos. Não declara encerradas a aceitação prolongada, a revisão do monitor
local/P6 ou a sincronização automática de qualquer URL.

#### 10.12.1 Deriva — implementação e limites verificáveis

`AudioClock.hpp` introduz um mapeamento afim de samples de origem por sample do
SDK. O controlador observa uptime e consumo efetivo da fila áudio já consultada
pelo callback; não segue a chegada do último pacote CEF. Retém até 240 observações
espaçadas pelo menos 0,5 s, estima taxa por regressão e corrige fase lentamente.
Parâmetros explícitos: aquisição de taxa de pelo menos 20 s, filtro de fase de 10 s,
banda morta de 1 ms, retorno de fase de 60 s, razão limitada a ±200 ppm e slew máximo
de 5 ppm/s. O envelope nominal ensaiado é ±100 ppm, não uma promessa para qualquer
placa/referência. A estimativa bruta e a incerteza são telemetria, não medição isolada
do cristal. Só há feedforward de taxa quando a diferença excede quatro erros-padrão
estimados e um piso de 0,5 ppm; de outro modo a estimativa ruidosa não é promovida a
deriva. A realimentação lenta de fase continua ativa.

Consultas com janela superior a 1 ms ou posições incoerentes são rejeitadas e
contadas. Dez segundos sem observação adequada, erro de fase superior a 100 ms ou
taxa persistentemente fora do limite solicitam recuperação contada. Após três
recuperações por deriva na mesma execução, a saída falha de forma controlada. A
recuperação de underrun existente continua separada. Não se promete continuidade
fora do envelope nem se usa inserção/remoção recorrente abrupta de samples como
controlo normal. A falha de SDK/recuperação limite não foi injetada fisicamente.

As atualizações preservam a posição no cursor de áudio ainda não escrito, incluindo
escritas parciais. Áudio e vídeo usam o mesmo mapeamento publicado por snapshots
atómicos. O leitor de vídeo tem tentativas limitadas e conserva o último snapshot
completo; não espera por um escritor preemptado. A telemetria também conserva o
último snapshot completo em vez de inventar uma origem zero durante publicação.

`AudioResampler.*` implementa sinc com janela Kaiser, 160 taps e 1024 fases com
interpolação dos coeficientes. A tabela é preparada antes do playback. O lookahead
simétrico de 80 samples (1,667 ms) é obtido do histórico dentro da margem de origem;
o centro temporal é conservado e não se acrescenta uma FIFO escondida. A cobertura
lida, incluindo halo, continua a produzir contadores de falta reais. A escolha é uma
implementação própria do método de interpolação limitada em banda, não interpolação
linear de samples. Referência do método:
[Julius O. Smith, Windowed Sinc Interpolation](https://www.dsprelated.com/freebooks/pasp/Windowed_Sinc_Interpolation.html).

**Testes finais:** oito horas simuladas por cenário, para 0/±0,42/±1/±20/±100 ppm,
com ruído de observação e rejeições; variação sinusoidal de deriva; cenário fora da
faixa a 999 ppm; ausência de observações; publicação concorrente; continuidade de
mapeamento; equivalência após escrita parcial; integração com o mixer e deteção de
lacunas. Depois dos primeiros 120 s, o maior erro de fase nos cenários constantes
foi **1,452 ms**, sem crescimento secular nesse envelope. Não é um resultado de oito
horas físicas. Para J=50 ms, o menor headroom modelado ficou acima de 7 ms, com o
atraso histórico de 30,7 ms, granularidade dos pacotes e lookahead considerados.

A varrida definida até 18 kHz apresentou erro máximo de ganho de cerca de
0,00003 dB; os tons de 1/18 kHz a razões 0,9999/1/1,0001 tiveram resíduo relativo ao
ideal abaixo de −118 dB. A banda 23,8–24 kHz ficou abaixo de −108 dB no teste.
Passaram os critérios objetivos propostos de 0,1 dB/80 dB nestes ensaios. A execução
Release do filtro isolado ficou perto de 0,08 ms por 1024 frames estéreo; isto não é
o custo completo do callback nem uma garantia de deadline sob qualquer carga.
Não substitui escuta subjetiva. Os callbacks existentes ainda incluem alocações e
mutexes: não se declara concluído todo o redesenho real-time P3/P6.

#### 10.12.2 Desfasamento — cadência correta e calibração utilizável

A referência identificada passou a gerar **25 fps para 25p** e 50 fps para 50p por
defeito. A fonte 25 fps tem 1500 frames descodificados/verificados antes de playback;
os IDs visuais avançam dois slots de 20 ms e os 3000 blocos PCM conservam os seus
IDs absolutos. O verificador separado valida ambos os formatos. Nos flashes
emparelhados desta série já não apareceu a antecedência de conteúdo de 20 ms do
primeiro frame branco observada em 50→25 na §10.11. Isto isola esse fator na fonte
controlada, sem prometer ausência de omissões de pintura em páginas arbitrárias.

A aplicação normal mantém o caminho por chegada para páginas sem PTS verificáveis.
O caminho por conteúdo identificado continua exclusivo do diagnóstico. Não se
ancorou qualquer página à primeira chegada nem se aplicou um offset empírico global
por defeito. Um URL pode já conter A/V errado ou buffers próprios; não existe prova
de que o CEF exponha os PTS de toda a composição arbitrária.

Foi acrescentado em Setup/Advanced um **ajuste A/V de −200 a +200 ms**, persistido e
válido durante PLAY. Positivo atrasa áudio; negativo atrasa vídeo através do
histórico. O ajuste de vídeo está sujeito à cadência/frames disponíveis e à
cobertura histórica limitada; grandes atrasos em UHD/múltiplas fontes não foram
aceites por estes testes HD. As mudanças de áudio fazem crossfade de 240 samples
(5 ms) entre posições, mantendo o relógio e o estado temporal. Buffers já no SDK
precisam de drenar; não há reinício de placa, de browser ou de aquisição de relógio.
Formato/perfil continuam a exigir STOP. A calibração acrescenta atraso em vez de
pedir áudio futuro não disponível.

**Prova de ajuste em direto**, em candidatas intermédias identificadas pelos seus
manifests, sem mudança de sessão SDI e excluindo as janelas de transição:

| Ajuste solicitado | A−V antes | A−V depois | Mudança física |
|---|---:|---:|---:|
| +20 ms no áudio | +31,021 ms | +50,729 ms | +19,708 ms |
| −20 ms no vídeo | +24,188 ms | +4,229 ms | −19,958 ms |

Ambos tiveram zero falhas de continuidade. O ensaio demonstra direção/efeito do
controlo; não afirma que os valores deliberadamente aumentados sejam lip sync
aceitável. O analisador associa flashes capturados a IDs efetivamente agendados e
rejeita ausência do marcador, mudança errada e contadores degradados. Não houve
escuta subjetiva da transição.

#### 10.12.3 Latência e percursos simples — escolher margem antes de velocidade

O perfil **Reliable**, por defeito, conserva três frames SDI e J=60 ms. O perfil
**Low latency**, opt-in e experimental até aceitação prolongada, usa duas frames e
J=50 ms. Ambos mantêm correção de deriva, verificação de preroll real e a antecedência
inicial de uma frame já documentada. A pool efetiva e os limiares de áudio/vídeo são
coordenados; não há mudança automática de reserva durante PLAY.

J=40 ms passou ensaios físicos curtos exploratórios, mas foi **rejeitado** como
perfil: o modelo com o atraso histórico, granularidade e lookahead produziu margem
negativa. Não se escolheu o valor só porque trinta segundos não falharam. Os logs e
as séries de 40 ms foram preservados. J=50 ms conserva margem positiva nos cenários
definidos; não se extrapola para atrasos de produtor ilimitados ou hardware genlock
não ensaiado.

| Modo | Orçamento interno Reliable | Orçamento interno Low latency | Redução configurada |
|---|---:|---:|---:|
| 1080p25 | 180 ms | 130 ms | 50 ms / 27,8% |
| 1080p50 | 120 ms | 90 ms | 30 ms / 25,0% |

Estes valores somam reserva e margem após aquisição; não são latência medida de um
URL até ao ecrã, nem incluem todos os buffers da fonte/recetor. O lookahead cabe na
margem, sem outra reserva adicional. Os ganhos não significam essa percentagem do
atraso total de uma página com rede/player próprios.

O caso de silêncio foi simplificado: depois de verificar/contar a cobertura, se todo
o bloco e halo forem zero, evita interpolação e conversão PCM desnecessárias. O
mapeamento, a correção de deriva e os contadores continuam ativos; uma lacuna que
produza zeros não é escondida. Os testes distinguem silêncio válido de cobertura
em falta. Conservam-se o alias de fonte única, composição por eventos e cache SDR
antes do agendamento; não se criou um atalho de áudio neutro que contorne o relógio.

#### 10.12.4 Evidência física final e o que se pode assegurar

Candidata final SHA-256
`d9f73d044d33ba1968308dc172356ff06a26ce33389f23e52e42fa579fd2cd9c`, mesmo executável nos
quatro casos, HD Mini/1080p com SDI OUT→IN e fonte identificada à cadência do modo.
Valores negativos significam som antes da imagem. São amostras, não limites universais.

| Modo / perfil | A−V capturado, mediana | Continuidade |
|---|---:|---|
| 25p / Reliable | −12,042 ms | zero falhas |
| 50p / Reliable | −5,021 ms | zero falhas |
| 25p / Low latency | +15,500 ms | zero falhas |
| 50p / Low latency | −7,646 ms | zero falhas |

Nos quatro: zero áudio em falta, erros API, recuperações, resets, vídeo SDI
late/dropped, falhas de histórico e fallback SDR. Validaram-se as 550/1100 seleções
da janela estável e os IDs físicos recebidos contra fonte/agendamento. Os canais
coincidiram. A incerteza e quantização não são anuladas pelos três decimais da tabela.
As várias séries intermédias também foram preservadas; não se escolheu apenas o
arranque com residual mais baixo.

O probe passou a registar uptime de chegada do callback e a descodificar IDs no
YUV físico. A comparação de perfis no percurso até à captura, compensando a fase A/V
medida, indicou **cerca de 51 ms a 25p e 30 ms a 50p** de redução, coerente com a
configuração. A diferença usa chegada ao callback de entrada e inclui o seu atraso;
não é medição absoluta de fio nem de ecrã. A fase A/V foi explicitamente considerada
para não chamar redução de latência a uma mudança do frame escolhido. A fórmula e
os números sem compensação estão no resumo JSON.

O modo fracionário 1080p59,94 foi repetido no executável final com captura física;
os resultados e contadores estão na evidência. Não foi promovido a sincronismo
exato do CEF: a cadência de pintura continua arredondada, o agendamento SDK racional.

#### 10.12.5 Ensaio noturno preparado, sem iniciar oito horas físicas

`run-endurance.py` exige corrente AC, cria um processo `caffeinate` próprio ligado
à duração da app, verifica a propriedade das assertions de sistema/idle/ecrã a cada
15 s, acompanha crescimento da telemetria com tempo monótono, avanço dos flashes/
impulsos e contadores de continuidade. Falta de proteção, de corrente, de fluxo,
de prontidão do controlador ou falha I/O interrompe o seu próprio teste. Não altera
configurações permanentes de energia. Tampa fechada, suspensão forçada, shutdown ou
falha do sistema não podem ser impedidos por uma assertion; invalidam o ensaio.
Os registos de energia guardam as assertions do processo de teste, não as de outras
aplicações. O Mac deve ficar ligado à corrente, com tampa aberta e loopback ligado.

**Separou-se continuidade de qualidade.** Dois ensaios iniciais de 120 s tinham
contadores limpos, mas offsets absolutos excessivos: um ficou perto de −45 ms e outro
mudou aproximadamente uma frame (−17→−54 ms). O primeiro runner verificava apenas a
mudança início/fim, pelo que esses dados foram reclassificados: comprovam exercício
do harness/proteção e continuidade curta, **não aprovação A/V**. A decomposição do
segundo mostrou fase da fonte perto de +35 ms e troca da frame agendada, em vez de
provar deriva secular de 36 ms em dois minutos. A estimativa curta de taxa também
motivou a regra de confiança descrita acima.

Por defeito, o runner final faz uma captura de calibração na **mesma sessão de
saída**, calcula o negativo do centro min/max medido e comunica só esse inteiro
limitado à instância de diagnóstico. Uma opção CLI explícita e privada desse teste
permite ler o ficheiro de calibração; a app normal não monitora ficheiros nem expõe
um serviço de controlo. A correção aplicada é registada e uma nova captura verifica
o resultado depois de drenar as filas. Não é um offset adivinhado para a produção.
A janela longa começa depois desta preparação, que tem limite de 43 s; a app corre
até 45 s adicionais para permitir **oito horas de medição após preparação**.
`--no-calibrate` existe só para diagnóstico bruto, sem dispensar os limites A/V.

Aceitação do runner: duração suficiente após calibração, zero erros de continuidade,
controlador pronto, capturas válidas sem gaps na região medida, coincidência estéreo,
limite absoluto A/V de uma frame e mudança início/fim até uma frame. Declara-se
1 ms de allowance de medição no limite absoluto. A qualidade é verificada por
min/max dos canais, não só pela mediana. Capturas apenas no início/fim não constituem
captura física independente contínua de oito horas.

O ensaio com calibração e 120 s de janela medida passou no executável final: ajuste
medido **+36 ms**, medianas pós-calibração de **−18,083 e −14,646 ms**, variação
**+3,437 ms**, headroom mínimo de **36,396 ms**, fase do controlador no intervalo
**−1,577 a +1,014 ms**, zero contadores de falha. O tempo ativo incluiu a preparação;
a janela medida depois dela excedeu 120 s. Um ensaio anterior calibrado ficou entre
−1,292 e −2,292 ms; ambos estão preservados, sem escolher esse melhor caso como
garantia. Isto valida o funcionamento curto do harness/calibração, **não oito horas**.

#### 10.12.6 Validação, limites e próxima decisão

Build completa, assinatura local verificada, CTest 5/5, testes Python de captura,
fontes 25/50, decomposição, ajuste em direto e harness passaram. ASan/UBSan e TSan
foram executados nos testes do relógio/mixer/reamostrador, incluindo publicação
concorrente. O smoke nativo verificou interface, controlos/persistência, composição,
CEF e ciclo do monitor. A inspeção visual externa ficou bloqueada pela sessão macOS
bloqueada; não se tentou contornar a autenticação. Não se fez escuta/lip sync humano
com as fontes reais do Luis. O monitor local mantém a implementação anterior; as
suas pendências de real-time/deriva própria não são dadas como resolvidas por P4 SDI.

Evidência: `tools/audio-audit/data/priorities-2026-10-31/`, com hashes por candidata,
séries exploratórias e finais, fontes deduplicadas, deriva/qualidade determinísticas,
reclassificação dos ensaios brutos, calibrações, energia e logs de checks. Os registos
não substituem as limitações declaradas de fonte, carga, resolução e hardware.

A aplicação instalada não foi substituída. A candidata assinada está em
`/tmp/u2k-delivery-final/URL2KeyFill.app`; fontes e ferramentas ficam no repositório.
**O que falta decidir com o Luis:** janela para as oito horas físicas e aceitação
visual/auditiva no recetor com fontes reais. Não há execução noturna agendada nem
iniciada automaticamente. A entrega disponibiliza correção/calibração/perfis;
a garantia prolongada de produção continua dependente dessa validação.


### 10.13 Resposta de implementação — ensaio físico de oito horas (01-11-2026)

Esta secção atualiza o estado histórico de 10.12.5–10.12.6: o ensaio noturno foi
explicitamente autorizado pelo Luis, executado e concluído. Não altera retroativamente
os resultados dos ensaios curtos nem fecha toda a auditoria.

#### 10.13.1 Execução e evidência preservada

Executável da release **0.5.0, build 8**, commit/tag de publicação `fa69079` / `0.5.0`,
SHA-256 do executável `18859925ee4541947f61c08e9c593c4bb2d3aea10b8154ff2235771124388e2a`.
Perfil **Reliable**, **1080p25**, loopback **SDI OUT → SDI IN no mesmo UltraStudio**.
Arranque em 31-10 às 23:57, conclusão em 01-11 perto das 07:58, hora de Lisboa.
Tempo ativo registado: **28 844,651 s**; janela após calibração: **28 815,168 s**,
superior às oito horas solicitadas. A aplicação terminou com exit 0 e libertou as
assertions próprias. Não houve substituição da aplicação instalada.

Evidência em `tools/audio-audit/data/endurance-2026-11-01/`: CSV originais comprimidos
sem alteração dos bytes descomprimidos, manifest/resultados, capturas, logs,
`analysis.json` com janelas e método de decomposição, `power-checks.tar.gz` e hashes
`SHA256SUMS.json`. Não se guardaram caches/perfis de navegação temporários.

**Continuidade: aprovada.** Zero falhas de amostras, erros SDK, recuperações,
frames atrasados/perdidos, reinícios de relógio, falhas de histórico ou fallback de
conversão. 27 978 linhas de telemetria; maior intervalo entre linhas ativas de
**1,052473 s**. Controlador sempre pronto após os primeiros 45 s. Foram revalidadas
**1 899 verificações de corrente AC e assertions de suspensão**, todas válidas.
Isto suporta continuidade durante a janela; não é uma certificação de todos os
estados de energia do macOS.

#### 10.13.2 Sincronismo físico e critérios de aceitação

A calibração inicial mediu áudio adiantado **46,979 ms** e aplicou **+47 ms** apenas
à instância de diagnóstico. Não é uma nova predefinição para URLs normais.

| Captura física | Mediana áudio − vídeo | Min/max | Eventos por canal |
|---|---:|---:|---:|
| Início, após calibração | +0,208 ms | +0,104 a +0,396 ms | 27 |
| Final | +25,396 ms | +25,396 a +25,396 ms | 27 |

Os dois canais coincidem; cada captura contém 749 frames válidas, sem gaps de
PTS nem flashes sem correspondência. Sinal positivo significa áudio atrasado.
Mudança entre medianas: **+25,188 ms**. O runner aprovou continuidade e qualidade
pelo critério previamente definido: até uma frame de 40 ms, com 1 ms de allowance
no limite absoluto, e mudança início/fim até uma frame. **Aprovação nesse critério
não significa deriva nula, sincronismo subframe garantido ou aceitação humana de
lip sync.** As casas decimais não removem quantização visual nem incerteza da entrada.

#### 10.13.3 O que explica a diferença de 25 ms

Recalculou-se a decomposição com `analyze_pipeline_sync.analyse`, sem modificar os
registos, nas janelas explicitadas em `analysis.json`. O A/V estimado do agendamento
é −0,522 ms no início (33–65 s) e +24,694 ms no fim (28 786–28 818 s): mudança
**+25,217 ms**, coerente com os **+25,188 ms** físicos. O desvio captura–estimativa
fica próximo de +0,7 ms nas duas extremidades, mas não é uma calibração independente
da entrada nem uma identidade por amostra: são snapshots e janelas aproximadas.

| Hora | Mediana A/V estimada no agendamento, últimos 120 s |
|---|---:|
| 1 | +3,880 ms |
| 2 | +6,807 ms |
| 3 | +9,699 ms |
| 4 | +12,680 ms |
| 5 | +15,646 ms |
| 6 | +18,597 ms |
| 7 | +21,611 ms |
| 8 | +24,648 ms |

A regressão destas oito medianas dá **+2,965 ms/h**, equivalente a **0,824 ppm** de
movimento relativo de fase nesta janela. Assim, a telemetria acrescenta evidência
de evolução gradual, que as duas capturas físicas isoladas não permitiam afirmar.
Não há fundamento para atribuir os 25 ms apenas a um salto ocorrido entre capturas.
Não há, porém, captura física contínua que comprove a forma da curva a cada instante.

A fase flash–impulso observada na fonte varia muito menos: medianas de **36,438 →
38,729 ms** nas janelas das extremidades. O controlador mantém erro de fase de
**−0,698 a +0,981 ms**, headroom mínimo **39,229 ms** e zero recuperações. Nas janelas
horárias, a correção de taxa fica aproximadamente entre **−0,81 e −0,93 ppm** e a
idade de seleção de vídeo passa de **+9,344 para −11,875 ms**. Idade negativa é
permitida pela tolerância de seleção de meia frame; não demonstra corrupção temporal.

**Hipótese principal, ainda por confirmar:** deslocamento de fase entre a grelha
de vídeo de 25 fps e os eventos da fonte, enquanto a correção de taxa mantém o
áudio alinhado ao relógio host/dispositivo. O vídeo continua quantizado a frames,
mesmo usando o mesmo mapeamento afim. A ordem de grandeza da correção (~0,84 ppm)
coincide com o movimento estimado (~0,82 ppm). Isto é compatível com fase a percorrer
a grelha e uma futura mudança da frame selecionada; **não prova um erro no servo
nem crescimento ilimitado do atraso de áudio**. Os PTS de vídeo normais são de
chegada da pintura, não PTS de conteúdo. A telemetria disponível não identifica
por evento a pintura selecionada nem separa todos os relógios do CEF.

Não se deve multiplicar 2,965 ms/h indefinidamente nem anunciar uma hora de falha.
Uma extrapolação ignora a seleção quantizada, possíveis mudanças de frame e a
origem ainda não isolada. Também não se deve reduzir a margem Reliable ou alterar
a correção de taxa com base apenas neste resultado.

#### 10.13.4 Próxima intervenção proposta para validação

1. Instrumentar, apenas em diagnóstico e com armazenamento limitado, cada evento
   flash/impulso com ID, PTS da pintura selecionada, alvo de seleção, posição SDI,
   mapeamento e instante de atualização. Associar esses IDs aos eventos capturados.
2. Comparar a fonte HTML atual com a referência de áudio/vídeo de PTS de conteúdo
   conhecidos já existente. Isto separa fase da página/CEF, escolha quantizada do
   vídeo e mapeamento áudio–dispositivo; evita compensar um relógio no sítio errado.
3. Verificar deterministamente a seleção com desfasamentos de taxa próximos de
   ±1 ppm e fases iniciais em toda a frame. Medir a envolvente A/V e a mudança de
   frame, em vez de testar só ausência de underruns ou erro interno do servo.
4. Decidir uma alteração apenas depois desta atribuição: associação por conteúdo
   quando disponível, ou política explícita de seleção/calibração limitada pela
   frame para páginas genéricas. Voltar a medir antes/depois e validar lip sync
   com fontes reais e o recetor do Luis.

**Estado:** ensaio de oito horas aprovado no Mac/dispositivo/modo testados;
fiabilidade demonstrada nesta execução; movimento residual de fase reconhecido e
quantificado; causa exata e comportamento para além da janela ainda não fechados.
Esta análise não iniciou outro teste físico nem alterou o código de produção.
A próxima intervenção acima fica pendente de confirmação do Luis.


### 10.14 Resposta de implementação — diagnóstico por evento e ensaios de 01-11-2026

Trabalho autorizado pelo Luis após 10.13, com continuação autónoma dos testes até
às 15:00 de Lisboa. Evidência em `tools/audio-audit/data/phase-diagnosis-2026-11-01/`.
Esta secção distingue a confirmação do mecanismo de quantização de uma garantia
universal de sincronismo de páginas arbitrárias.

#### 10.14.1 Instrumentação limitada ao diagnóstico

O modo explícito `--sdi-av-test` passou a reservar buffers limitados a 4096 eventos
por tipo, antes de começar os callbacks. `SYNC_AUDIO` deteta os impulsos no PCM
**efetivamente aceite** por `ScheduleAudioSamples`, contando apenas o prefixo aceite
em escritas parciais; guarda posição de saída, mapeamento, atraso e indicação de
transição/crossfade. `SYNC_VIDEO` guarda o onset do flash escolhido, sequência,
PTS de pintura/conteúdo, alvo, mapeamento e ID da referência quando disponível.
A variante final acrescenta o timestamp efetivo do histórico e o limite de avanço
de meia frame, mais uma observação de relógios por segundo na própria instância.

Não há alocação no append nem escrita de logs no agendamento: a serialização é
feita depois de os escritores pararem. Overflow é contado e o analisador recusa
traços truncados. A reprodução normal não cria estes buffers, não analisa PCM
para este fim nem faz as novas consultas de hardware. Não se alterou o algoritmo
de seleção, o servo, a margem Reliable ou os valores de calibração de produção.

`analyze_event_sync.py` verifica mapeamentos afins, ordem/completude dos eventos,
exclui impulsos de transição e compara áudio aceite com a frame escolhida. Para a
referência identificada exige IDs físicos: um probe antigo sem IDs não passa esta
validação. Para a página genérica há um emparelhamento opcional por periodicidade
mais observação host, explicitamente limitado a atraso de observação inferior a
400 ms e época vídeo entrada/saída constante por captura. Não equivale a IDs de
conteúdo. Os timestamps host são observações de software, não instantes de emissão.

Foram preservadas duas variantes: a primeira usada nas quatro comparações e nos
ensaios de 20/10 minutos; a final acrescenta a observação SDK independente e o
instante de seleção. Os hashes identificam cada executável. Os fontes exatos da
primeira variante, verificados contra os hashes registados, ficam comprimidos com
a evidência. A release 0.5.0 publicada e a aplicação instalada não foram substituídas.

#### 10.14.2 Referência conhecida: previsão e captura concordam por ID

O probe foi recompilado dos fontes atuais para descodificar os IDs físicos. Cada
fonte de 60 s foi verificada independentemente, incluindo os 1500/3000 frames
visuais a 25/50 fps e as marcas áudio. Os dois pares chegada/conteúdo usam os mesmos
bytes de media em cada cadência, preservados uma vez por SHA-256. São sessões de
saída diferentes: não se compara a fase de arranque como se fosse uma alteração
isolada numa única sessão.

| Fonte/saída | Seleção | Mediana física áudio − vídeo | Min/max físicos | IDs emparelhados |
|---|---|---:|---:|---:|
| 25/25 fps | Chegada da pintura | +0,802 ms | +0,771 a +1,625 ms | 30 |
| 25/25 fps | PTS de conteúdo | +11,333 ms | +10,792 a +11,375 ms | 31 |
| 50/50 fps | Chegada da pintura | +17,729 ms | +17,208 a +17,771 ms | 31 |
| 50/50 fps | PTS de conteúdo | +10,563 ms | −9,167 a +10,792 ms | 31 |

Os canais L/R coincidem e todos os contadores I/O verificados estão a zero. A
diferença captura–PCM agendado é praticamente constante por evento: **+0,708 ms**
em três casos e **+0,729 ms** no controlo por chegada a 50 fps. É um residual do
percurso saída+entrada; não foi subtraído nem atribuído integralmente à entrada
como se houvesse calibração física independente.

**Mudança de frame confirmada fisicamente.** No caso de conteúdo a 50 fps, os IDs
1550 e 1600 dão, respetivamente, **+10,792 e −9,167 ms** físicos; o PCM agendado dá
**+10,083 e −9,875 ms**. A variação de **−19,958 ms** é aproximadamente uma frame de
20 ms, mantendo residual de +0,708 ms nas duas extremidades. A grelha de vídeo
mudou de fase sem underrun, recuperação ou frame SDI perdida/atrasada. Contadores
SDK a zero não significam que nunca muda a identidade do frame de conteúdo escolhido.

Isto demonstra o mecanismo que faltava à hipótese de 10.13: uma fase pode avançar
gradualmente e depois mudar um frame; a tendência não tem de crescer linearmente
sem limite. Demonstra-o nesta referência controlada. A atribuição de todos os
25 ms do ensaio noturno ao mesmo mecanismo continua condicionada à falta de
registo por evento nessa execução; é uma explicação suportada, não uma reconstrução
física contínua das oito horas.

Duas primeiras recolhas usaram o probe antigo sem IDs e foram preservadas como
exploração, sem contar como prova por identidade. Num arranque a 25 fps, o primeiro
impulso observado pelo CEF diferiu 1148 amostras da origem conhecida, antes da janela
de validação. O analisador inicialmente rejeitou o caso. A reavaliação usa para os
impulsos o mesmo warmup de sete segundos já usado no agendamento e guarda as
observações excluídas; os impulsos estáveis, marcas de origem e saúde I/O continuam
verificados. Não se apagou a rejeição nem se dispensou o teste dentro da janela.

#### 10.14.3 Envolvente determinística da seleção

`sync_diagnostics_tests` exercita o `FrameHistory` real em **192 cenários**, cada um
com **48 horas simuladas**: quatro combinações fonte/saída 25/50 fps, 16 fases de
arranque e diferenças de taxa 0/±1 ppm. Verifica a envolvente e a mudança de frame,
além de escritas PCM parciais, escrita nula, sinal negativo extremo e overflow
sem crescimento de memória. A previsão mantém-se numa envolvente de uma frame de
saída, com tolerância de arredondamento, e faz mudanças discretas quando a fase
atravessa a fronteira. Sem diferença de taxa, a fase fica constante neste modelo.

Com cadências iguais e sem jitter, a quantização tem aproximadamente ±20 ms a
25 fps e ±10 ms a 50 fps, em torno do centro ideal escolhido. São limites do modelo
com frames disponíveis, não limites absolutos da app para qualquer fonte. Cadências
mistas podem ter uma envolvente assimétrica. A pintura/descodificação real, composição,
frames indisponíveis e fase do recetor continuam a acrescentar incerteza.

Validação desta implementação: build nativa e assinatura verificadas, CTest **6/6**,
Python **64/64**, ASan/UBSan no teste de diagnóstico/seleção. Não se publicou uma
nova versão nem se alterou a 0.5.0 para incorporar dados posteriores à sua publicação.


#### 10.14.4 Capturas intermédias e critério de qualidade reforçado

O runner ganhou capturas físicas opcionais de 30 s a cada 120 s medidos, sem
sobrepor a posse da entrada com calibração/início/fim. A variante atual testa tanto
o A/V absoluto como a **envolvente min/max de todas as capturas**, até uma frame
mais 1 ms de allowance. Os bons endpoints não podem ocultar uma excursão intermédia.
Isto reforça o critério original, que verificava valores absolutos e a mudança de
medianas início/fim. Não se reescreveram os manifests antigos para simular esse
critério novo; a mesma revalidação é guardada separadamente para as novas séries.

O ensaio a **25 fps, Reliable, 1200 s após calibração**, teve 1244,651 s ativos e
1213,982 s após preparação. Ajuste medido **+49 ms**. Foram recolhidas **11 janelas**
(início, nove intermédias e fim), com 297 emparelhamentos periódicos/host; todas as
épocas de timestamps foram consistentes por captura e os canais coincidiram.
Mediana física: **+0,396 → +13,542 ms**; min/max conjunto **0 a +13,833 ms**.
Passou continuidade e a revalidação da envolvente. Zero contadores de falha,
headroom mínimo **42,688 ms**, fase interna **−2,417 a +2,379 ms**, taxa aplicada
**−43,590 a +32,644 ppm**. Não se estende a esta sessão a observação de erro interno
inferior a 1 ms obtida no ensaio noturno. Foram revalidadas **82 verificações** de
energia/assertions e o maior intervalo de telemetria foi **1,052 s**.

As medianas físicas nas nove capturas intermédias foram **1,063; 4,063; 5,625;
7,250; 4,875; 4,583; 8,021; 9,521; 12,521 ms**. O movimento não é estritamente
monótono. O residual captura–PCM agendado manteve-se em torno de **0,688–0,708 ms**:
o deslocamento aparece no agendamento, sem evidência de uma mudança comparável
no atraso relativo da captura. O emparelhamento da página genérica é condicionado
à hipótese host/periódica descrita acima; não foi promovido a identidade física.


O ensaio a **50 fps, Reliable, 600 s após calibração**, teve **644,651 s ativos**,
com ajuste medido de **−5 ms**, seis capturas (início, quatro intermédias e fim) e
162 emparelhamentos periódicos/host. Zero contadores de falha; headroom mínimo
**25,688 ms**; fase interna **−2,533 a +2,539 ms**; taxa aplicada **−60,160 a
+33,784 ppm**. Foram revalidadas **43 verificações** de energia/assertions e o
maior intervalo de telemetria foi **1,052 s**.

**Continuidade aprovada; qualidade reprovada pelo critério reforçado.** Medianas
início/fim **−4,667 → −1,625 ms**, mudança **+3,042 ms**. Porém, a envolvente física
conjunta foi **−5,042 a +18,854 ms**, largura **23,896 ms**, superior aos **21 ms**
permitidos (uma frame + 1 ms). O A/V absoluto ficou dentro do limite. O runner
terminou com exit 1 e `accepted=false`; este resultado não foi reclassificado para
aprovação. As medianas intermédias foram **−1,167; −3,583; +16,604; −4,438 ms**.
O residual captura–PCM manteve-se próximo de **0,729–0,750 ms**.

A fase de chegada flash–impulso observada na própria fonte, nos snapshots válidos,
abrangeu **−17,854 a +6,458 ms**, mediana **+1,979 ms**. A referência de pintura já
varia aproximadamente uma frame antes de se avaliar a seleção na saída. Não são
PTS de conteúdo, e a amostragem pode omitir eventos; ainda assim, estes dados
impedem atribuir a envolvente toda à correção de relógio. A calibração negativa
quantizada e a pintura do CEF são fatores distintos a isolar. Nem esta reprovação
significa crescimento ilimitado nem se deve considerar resolvida a qualidade de
qualquer URL por o ensaio de oito horas ter passado noutro modo/sessão.

#### 10.14.5 Granularidade do ajuste negativo: limitação confirmada e alternativa

A política de produção atual mantém áudio sem atraso adicional quando o ajuste é
negativo e procura uma pintura anterior para atrasar vídeo. Uma seleção de frames
não implementa uma translação contínua de 1 ms. Testes adicionais do `FrameHistory`
real, com 16 fases e fonte/saída à mesma cadência, confirmaram, sem jitter:

| Ajuste pedido | Efeito possível a 25 fps | Efeito possível a 50 fps |
|---|---:|---:|
| −5 ms | 0 ou −40 ms | 0 ou −20 ms |
| −20 ms | 0 ou −40 ms | −20 ms |
| −25 ms | 0 ou −40 ms | −20 ou −40 ms |

São diferenças em relação ao caso sem ajuste na mesma fase do modelo. O teste
físico anterior de −20 ms a 50 fps correspondia a uma frame inteira; não demonstrava
precisão de 1 ms para valores negativos fracionários. Esta limitação não deve ser
confundida com crescimento ilimitado de deriva nem com uma falha I/O.

Foi testada **apenas como protótipo matemático**, sem mudar a produção, uma
decomposição que desloca o vídeo por um número inteiro de frames de saída e
compensa a fração no áudio. Por exemplo, para obter −5 ms a 50 fps: vídeo +20 ms,
áudio +15 ms; a 25 fps: vídeo +40 ms, áudio +35 ms. O modelo de taxa unitária
recupera o ajuste pedido em todas as fases testadas. O custo é latência adicional,
a considerar pela ordem de prioridades fiabilidade → qualidade → velocidade.

A alternativa ainda exige implementação/validação com mapeamento variável,
transições, orçamento do histórico, captura física e fontes reais. **Não elimina
por si só a incerteza de pintura do CEF**, nem se atribui automaticamente a esta
limitação toda a envolvente A/V medida em páginas genéricas. A referência com PTS
conhecidos permite separar estes fatores; uma página arbitrária continua sem essa
origem comum garantida.


#### 10.14.6 Observação SDK na própria instância

Uma tentativa de observador separado, estritamente de leitura, não obteve os
relógios ativos através da sua interface SDK; ficou preservada como tentativa
falhada, sem a tratar como medição. A variante final passou a observar o relógio
de hardware na própria instância de saída, uma vez por segundo, com tempos host
antes/depois da consulta e sem usar essas novas leituras para comandar o servo.

O ensaio de **240 s após calibração a 25 fps**, com ajuste medido **+35 ms**, passou:
**284,651 s ativos**, **253,543 s após preparação**, zero contadores de falha,
headroom mínimo **36,583 ms**, medianas físicas **−0,896 → −0,083 ms** e envolvente
amostrada **−3,458 a +0,396 ms**, largura **3,854 ms**. O erro de fase interno na
telemetria foi **−3,267 a +0,788 ms**; a taxa aplicada foi **−53,215 a +36,564 ppm**.
São valores desta sessão curta, não uma garantia de erro máximo em produção.

Foram ajustados 262 pontos válidos, cobrindo **263,920 s** depois do warmup:

| Relógio reportado pelo SDK contra host | Taxa média dispositivo/host |
|---|---:|
| Consumo da fila áudio | +15,628 ppm |
| Referência de hardware | +15,640 ppm |
| Stream agendado | +15,650 ppm |

Os três ajustes diferem menos de **0,03 ppm**. O residual RMS do ajuste linear
foi cerca de **0,84 ms**, com máximo perto de **2,24 ms**. Nas três janelas de
hardware de aproximadamente 90/90/85 s, as taxas ajustadas foram **−19,625;
+36,945; +25,072 ppm**. A taxa não se comportou como uma constante nesta janela;
não se deve comparar a mediana da correção aplicada com uma única regressão global
para deduzir um erro fixo no servo.

Não houve observações rejeitadas pelo limite de tempo das queries. A concordância
entre os canais SDK enfraquece a hipótese de uma variação exclusiva do contador da
fila. **Não mede um oscilador físico independente**: hardware/stream são valores
reportados pelo driver na mesma máquina/dispositivo, e o erro de ajuste tem ruído
correlacionado/não estacionário. Não há base nestes dados para substituir o servo
por uma taxa fixa, declarar avaria da placa ou prometer sincronismo subframe.


#### 10.14.7 Cadências mistas e decisão após a janela autorizada

As comparações finais da referência identificada, com PTS de conteúdo e a variante
final de diagnóstico, concluíram sem contadores de falha, com os canais coincidentes:

| Fonte → saída | Mediana física A/V | Min/max físicos | IDs emparelhados |
|---|---:|---:|---:|
| 25 → 50 fps | −1,563 ms | −1,583 a −0,729 ms | 31 |
| 50 → 25 fps | −6,813 ms | −6,833 a −6,188 ms | 31 |

O residual captura–PCM foi **+0,750 ms** nos dois casos. Estas sessões curtas
validam a associação por conteúdo nas cadências testadas; não tornam a cadência de
pintura de uma página arbitrária equivalente ao PTS de um vídeo identificado.

**Os testes físicos terminaram às 14:55 de 01-11-2026**, antes do limite autorizado
das 15:00. Não ficou outro ensaio agendado. Foram preservados os dados originais,
fontes de referência deduplicados, variantes de instrumentação, scripts efetivamente
usados, verificações e a rejeição de qualidade a 50 fps. `summary.json` resume os
casos; `SHA256SUMS.json` permite verificar os artefactos. Os ensaios decorreram com
atividade de desenvolvimento no Mac, não em ambiente de carga/temperatura controladas.

**Conclusão para o auditor e para o Luis:**

- A fiabilidade I/O passou nas novas sessões e já tinha passado oito horas a 25p;
  não há indicação nestes casos de fila a crescer sem limite ou de falhas periódicas.
- A diferença de fase entre relógios e uma grelha de frames pode acumular-se e depois
  mudar uma frame. Houve confirmação física por IDs; não se extrapola uma taxa
  linear para anunciar uma hora de falha de lip sync.
- A qualidade para URLs genéricas continua condicionada pela pintura e pela
  calibração. A envolvente de 23,896 ms a 50p reprovou o novo limite de 21 ms, mesmo
  com A/V absoluto dentro do limite e contadores de continuidade limpos.
- Ajustes negativos fracionários não têm hoje efeito contínuo de 1 ms. A proposta
  de frame comum mais compensação áudio merece a próxima implementação/validação,
  porque a qualidade tem prioridade sobre até uma frame adicional de latência.
  Deve ser comparada na mesma sessão e não promete remover o jitter da fonte.
- Mantém-se a correção adaptativa de relógio e o perfil Reliable; não há fundamento
  para aplicar uma taxa fixa, reduzir margens ou ativar automaticamente o protótipo
  de PTS para qualquer URL. Referências de conteúdo conhecidas permitem melhor
  controlo; fontes reais e receção/lip sync humano continuam por validar.

Próximo passo proposto: implementar e medir a decomposição de ajustes negativos,
com tratamento de transições e limites de histórico, depois repetir a comparação
com a página HTML e a referência identificada. A observação SDK pode acompanhar
um ensaio mais prolongado, mas os buffers devem ser dimensionados/amostrados para
essa janela antes de o certificar por evento. Nenhuma nova sessão longa foi
iniciada. O monitor local/P6 mantém as pendências já identificadas.

### 10.15 Resposta de implementação — ajuste negativo com precisão subframe (2026-11-02)

**Estado: implementação candidata sobre 0.5.2; não constitui uma nova publicação.**
As conclusões de 10.14 mantêm-se: melhorar a precisão do ajuste não elimina a
variação de fase da fonte, nem certifica todas as URLs ou receção humana.

**Alteração implementada.** Para um ajuste negativo, `AVTiming` decompõe o pedido
num atraso comum de vídeo de um número inteiro de frames e numa compensação de
áudio fracionária. Exemplo: −5 ms a 25p corresponde a vídeo +40 ms e áudio +35 ms;
a 50p, vídeo +20 ms e áudio +15 ms. O cálculo usa a taxa racional e o mapeamento
adaptativo do relógio. Zero e ajustes positivos conservam o percurso anterior,
sem atraso comum adicional. Mantêm-se o servo, as reservas SDK, a idade da fonte
e a transição áudio de 5 ms. Não foi adicionada uma fila de saída.

**Custo explícito.** A precisão negativa pode acrescentar até quase uma frame de
latência comum relativamente ao atraso ideal pedido. Não é uma otimização de
velocidade: é uma troca deliberada de latência por precisão. Para −5 ms, o custo
nominal adicional de áudio é 35 ms a 25p ou 15 ms a 50p. As estimativas por callback
medidas na primeira comparação foram vídeo +39,56/áudio +34,56 ms a 25p e vídeo
+20,54/áudio +15,58 ms a 50p. Incluem captura e escalonamento de software; não são
medições absolutas da latência do fio, do ecrã ou do observador.

**Histórico e falha encontrada durante a validação.** O primeiro candidato passou
−199 ms a 25p e 50p, mas falhou a 59,94p: 1583 falhas de histórico e 7 no arranque.
Este resultado foi preservado, sem o reclassificar como aprovado. As 16 frames
fixas não cobriam o atraso, a idade da fonte e a reserva nessa taxa. O segundo
candidato dimensiona o limite em frames: 16 a 25p, 18 a 50p e 21 a 59,94p na
configuração ensaiada, mantendo os limites de bytes de 192 MiB sem preparação e
384 MiB com preparação. Comparado com 16 frames, o máximo adicional preparado
em 1080p é aproximadamente 33 MB a 50p ou 83 MB a 59,94p. A seleção mantém a mesma
conversão por publicação; reter mais frames não acrescenta esse atraso à saída
quando o ajuste é zero.

O segundo candidato repetiu −199 ms durante 30 s nas três taxas: zero falhas de
histórico, incluindo arranque, e zero amostras em falta, erros API, recuperações,
frames atrasadas/descartadas ou resets. Estes ensaios validam continuidade e
capacidade do histórico; os offsets absolutos entre sessões não servem para
atribuir erro de calibração ao ajuste. Pedidos que ultrapassam o orçamento de
histórico da resolução são rejeitados antes de ativar a saída; alterações em
PLAY também verificam a disponibilidade do histórico antes de serem aceites.

**Precisão física já medida.** Na comparação inicial de 0 para −5 ms, com referência
de conteúdo identificada e loopback real, a alteração foi −5,000 ms a 25p e
−5,021 ms a 50p. Houve 31 correspondências físicas de eventos por ensaio e zero
falhas de continuidade. O analisador separa o ajuste inicial do final e exclui
as janelas de transição. Isto valida o efeito em regime estável; não certifica a
perceção subjetiva da transição.

**Verificação automática.** Passaram 7/7 testes C++ e 67 testes Python. O teste de
ajuste cobre 16040 planos em oito taxas e cinco razões de relógio, incluindo
±200 ppm, e 2240 comparações com seleção real de histórico a 25p/50p. A execução
com AddressSanitizer/UndefinedBehaviorSanitizer passou; o teste concorrente de
retenção/publicação/leitura passou com ThreadSanitizer. Estes testes não
substituem observação de cor, lip sync humano ou novo ensaio de oito horas.

Evidência bruta e resultados: `tools/audio-audit/data/negative-adjustment-2026-11-02/`.
As falhas do primeiro candidato estão em `history-v1`, a repetição corrigida em
`history-v2`, e a matriz final em `live-v2`. A app de teste foi preparada em
`/tmp/u2k-negative-adjustment-v2/URL2KeyFill.app`; não foi substituída a instalação
normal nem publicada uma nova versão.

**Matriz final de alterações em PLAY — resultados provisórios.** A 25p, as mudanças
0→−1, 0→−5, 0→−21 e −5→+5 ms mediram, respetivamente, −1,000, −4,990, −21,000 e
+10,000 ms. A 50p, 0→−1, 0→−5, 0→−21 e +5→−5 ms mediram −1,000, −5,021, −20,979 e
−10,000 ms. Os oito analisadores registaram aprovação e zero contadores de falha.
Contudo, o utilizador comunicou ter desligado e voltado a ligar o UltraStudio
para recuperar o funcionamento. Como não ficou registado o instante dessa
intervenção, esta matriz fica **provisória**, sem certificação de funcionamento
ininterrupto do hardware. Os registos originais e esta ressalva são preservados;
não se usa a ausência de erros dos contadores como prova contra o bloqueio relatado.

**Resposta à fotografia de imagem verde / recuperação do UltraStudio.** A imagem
uniforme não era o padrão esperado do teste. Foi observada uma diferença entre
PLAY e repouso: durante PLAY, a captura dos oito pontos das barras devolveu
valores distintos coerentes com branco, amarelo, ciano, verde, magenta, vermelho,
azul e preto (por exemplo, extremos YUV 235/128/128 e 16/128/128). Depois de STOP,
o SDK indicava `Busy=0`; o sinal capturado em repouso apresentou valores anómalos.
Isto não demonstra, por si só, uma conversão de cor errada durante PLAY nem permite
atribuir o bloqueio ao driver, ao firmware ou à aplicação.

A consulta, sem escrita persistente, encontrou suporte de saída em repouso,
operação em preto e passagem de captura em `Clean Switch`. No ensaio reversível,
com passagem desativada, os oito pontos horizontais amostrados devolveram preto
16/128/128; com a configuração original restaurada, voltaram valores anómalos.
O valor médio da região central em repouso incluía linhas com zeros e **não deve
ser interpretado como uma imagem completa nem como medição colorimétrica válida**.
A realimentação SDI OUT→SDI IN em repouso é uma hipótese apoiada por esta comparação,
não uma causa definitivamente demonstrada. O ensaio de cor também fica condicionado
pela ausência do instante exato do reinício físico comunicado pelo utilizador.

A configuração original foi restaurada (`SetInt` devolveu sucesso); não foi
chamado `WriteConfigurationToPreferences`. Nenhum processo de teste ficou ativo.
O utilizador confirmou que o equipamento voltou a funcionar após desligar/ligar.
Não foi introduzida uma alteração especulativa no percurso de saída para tentar
ocultar este sintoma. A evidência está em `green-investigation`.

**Próximo passo após validação pelo utilizador/auditor:** um ensaio curto, com hora
de início/fim e de qualquer reinício registadas, comparando PLAY/STOP e passagem
SDI desativada temporariamente. Confirmar imagem no LCD e na captura, repetir o
arranque sem desligar a placa e só depois decidir se é necessário corrigir a
sequência de STOP ou configurar o ambiente de loopback. Não arrancar novo teste
longo com este incidente por esclarecer. A precisão negativa candidata está
implementada; a fiabilidade do hardware perante este sintoma permanece pendente.

### 10.16 Continuação autorizada até às 09:00 de 2026-11-02 (Lisboa)

Às 02:44 foi iniciada uma nova sequência, após o utilizador comunicar recuperação
por desligar/ligar e autorizar testes até às 09:00. Estado inicial: UltraStudio
livre (`Busy=0`), Mac em alimentação AC. Esta sequência não altera a classificação
provisória dos ensaios anteriores ao reinício sem hora registada.

O executor `tools/audio-audit/run-until.py` repete sequencialmente oito mudanças
A/V e arranques/paragens a 25p/50p, seguidos de blocos de 20 minutos a 25p, 50p e
59,94p com calibração física e capturas periódicas a cada 120 segundos. Cada
comando e início/fim tem registo horário. Falhas de continuidade, falta de
telemetria, perda de alimentação AC ou dispositivo ocupado interrompem a sequência.
Uma reprovação de qualidade A/V com continuidade válida fica registada e não é
convertida em aprovação; permite continuar a caracterização nas outras taxas.

O limite horário é verificado durante os subprocessos. Só são iniciados ensaios
que caibam no tempo restante com margem, pelo que a sequência pode terminar antes
das 09:00 se não couber outro ensaio completo. As asserções de energia são locais
ao processo; não são alteradas preferências permanentes de energia. São verificadas
periodicamente a alimentação AC e as asserções que impedem repouso do sistema/ecrã.
Não se reinicia automaticamente o dispositivo, não se interrompem outras
aplicações, não se faz publicação e não se inicia um novo ensaio de oito horas.

Registos em curso: `/tmp/u2k-until-0900-20261102/state.json`, `power.jsonl`, logs,
telemetria e capturas em subdiretórios por ensaio. O acompanhamento automático
neste chat está configurado para verificar progresso/falhas e consolidar os
resultados nesta secção depois da conclusão. Este parágrafo descreve o plano e
o arranque, **não uma aprovação antecipada dos resultados**. O teste de 20 minutos
usa capturas amostradas; não equivale a medição física contínua nem certifica
lip sync humano, todas as URLs ou um relógio independente.

**Ponto de controlo 02:59:** a nova matriz completou os oito ensaios entre 02:44 e
02:49, com todos os analisadores aprovados. As mudanças medidas foram −1,000,
−5,021, −21,000 e +10,000 ms a 25p; −0,979, −4,979, −21,021 e −10,021 ms a 50p.
O erro máximo da mudança pedida foi 0,021 ms. O primeiro bloco de 20 minutos a
25p está em curso; aos 589,6 s, os contadores de continuidade e de histórico
permaneciam a zero, a fonte avançava e o controlador estava pronto. As verificações
de alimentação/asserções de repouso continuam válidas. Ainda não é resultado final
do bloco nem prova de ausência do incidente de imagem no LCD.

**Ponto de controlo 03:14:** o primeiro bloco a 25p terminou às 03:10:26 e passou
o critério configurado. A janela de medição foi de 20 minutos após calibração;
a atividade total incluiu 45 s de preparação (1244,6 s de telemetria ativa).
Todos os contadores de continuidade/histórico/conversão permaneceram a zero.
Nas capturas amostradas, A/V variou entre +0,583 e +11,604 ms (amplitude 11,021 ms);
a mediana inicial foi +1,479 ms e a final +11,188 ms, diferença +9,708 ms. Esta
diferença entre extremos não estabelece uma deriva linear nem permite prever
quando o sincronismo se tornaria inaceitável. O bloco a 50p arrancou às 03:10:27;
aos 240,8 s continuava sem falhas, com fonte e servo ativos. Alimentação e
asserções de repouso continuam válidas. A observação do LCD permanece separada
da aprovação dos contadores e das capturas A/V.

**Ponto de controlo 03:29 — reprovação de qualidade já observável a 50p:** embora
o bloco ainda esteja em curso (1140,9 s), as capturas concluídas já contêm valores
entre −18,063 e +15,938 ms, amplitude de pelo menos 34,000 ms. Isto ultrapassa o
critério da envolvente completa amostrada de uma frame mais 1 ms (21 ms a 50p),
independentemente do resultado da última captura. Os valores absolutos observados
continuam dentro de ±21 ms, mas isso não basta para aprovar a estabilidade da
envolvente. Os contadores de continuidade/histórico permanecem todos a zero,
a fonte avança e o servo está pronto. Este achado não demonstra bloqueio do
UltraStudio nem autoriza extrapolar uma deriva linear; confirma que o ajuste
preciso e os contadores sem erros não asseguram, por si só, estabilidade de fase
da fonte. Mantém-se o bloco até concluir a amostragem e a sequência prossegue
nas outras taxas conforme a distinção já prevista entre qualidade e continuidade.

**03:44 — conclusão 50p e correção do executor:** o bloco 50p terminou às
03:31:13, com continuidade aprovada mas qualidade reprovada: envolvente completa
−20,104 a +19,958 ms, amplitude 40,063 ms; medianas inicial/final −10,042/−17,354 ms.
O lançamento seguinte a 59,94p foi rejeitado pelo parser do executor de duração,
que só aceitava 25p/50p. Não chegou a abrir a placa; não é um bloqueio do hardware.
Corrigiu-se a opção e o limite racional de frame para 1001/60 ms a 59,94p, com
12 testes do executor aprovados e dry-run desse modo validado. Os dados originais
foram preservados, e a sequência retomada em
`/tmp/u2k-until-0900-20261102-resume1`, mantendo o prazo das 09:00. A placa estava
livre antes da retoma. A falha de qualidade 50p permanece reprovada.

**03:59 — retoma acompanhada:** a segunda matriz de oito alterações terminou às
03:50:28, novamente com todos os analisadores aprovados. O bloco seguinte a 25p
mantém-se em curso, sem falhas de continuidade/histórico aos 537,3 s, com fonte e
servo ativos. Alimentação AC e asserções de energia válidas. Esta repetição de
ajustes estáveis não altera a reprovação da envolvente de fase já observada a 50p.

**04:14 — segunda medição de 20 minutos a 25p:** terminou às 04:11:15 com
aprovação, zero contadores de falha e envolvente amostrada −6,083 a +0,042 ms
(amplitude 6,125 ms). Medianas inicial/final −0,583/−0,042 ms; diferença +0,542 ms.
A diferença para o primeiro bloco reforça que não há fundamento para extrapolar
uma taxa linear única a partir de um par início/fim. A repetição a 50p está em
curso desde 04:11:15, com continuidade e alimentação válidas nesta verificação.

**04:44 — falha real de continuidade a 59,94p; sequência física interrompida.**
A repetição a 50p terminou às 04:32:02 e passou: envolvente −1,167 a +12,792 ms,
amplitude 13,958 ms; medianas inicial/final +0,813/+12,563 ms, sem erros. Assim,
a falha de qualidade anterior a 50p não se reproduziu nesta sessão, mas continua
válida e impede uma garantia geral de estabilidade baseada apenas nesta aprovação.

O ensaio 59,94p arrancou às 04:32:02 e foi interrompido pelo watchdog às 04:39:40.
A primeira linha com erro surge aos 442,498 s de telemetria (cerca de 04:39:25):
1 recuperação áudio, 1 frame atrasada e 1 descartada; o servo deixou de estar
pronto. As amostras em falta, erros API, resets de relógio, falhas de histórico e
conversões de recurso continuaram a zero. Antes do evento, a margem mínima de
fonte era cerca de 61,5 ms e o buffer indicado 2403 amostras; esses valores
amostrados não excluem uma interrupção breve entre linhas. A causa não está
estabelecida. Não é legítimo atribuí-la ao histórico ou ao bloqueio visto no LCD
sem evidência adicional.

O watchdog verificava a cada 15 s, pelo que a reação ocorreu cerca de 14,5 s após
a primeira linha de erro. Os subprocessos próprios terminaram; verificação
posterior confirmou dispositivo livre (`Busy=0`). Não houve reinício automático
nem repetição cega a 59,94p. Até ao limite autorizado, prossegue a análise dos
registos e da capacidade de diagnóstico, sem iniciar outro teste físico contra
esta falha por esclarecer. A alimentação AC e as asserções de repouso estavam
válidas na última verificação anterior à interrupção. Este bloco é **reprovado e
incompleto**, não um ensaio de 20 minutos aprovado. Os eventos detalhados SDK,
normalmente despejados no STOP, não constam do log terminado pelo watchdog; a
telemetria existente não permite reconstruir a ordem subsegundo dos três erros.

**04:59 — análise offline do evento 59,94p:** a primeira frame descartada tem
índice 26437. Entre as linhas aos 441,498 e 442,498 s, `startup_shift_ms` muda de
38,583 para 10,083 ms e o mapeamento publicado volta a `mapping_stream=0`;
`clock_calibrated` permanece 1, enquanto `audio_drift_ready` passa a 0. O código
usa o mesmo contador `recoveries` para recuperação de fila e rejeição/reset do
servo; portanto, o valor 1 isolado não prova uma fila vazia. As amostras de buffer
posteriores ao callback indicam 2403, exatamente o alvo, e podem já refletir
reabastecimento. Sem o evento detalhado anterior ao reabastecimento não é possível
separar com segurança essas causas nem estabelecer se o atraso de vídeo precedeu
a recuperação áudio. Não foi alterada a aplicação com base numa atribuição causal
não demonstrada.

**05:14 — preservação e limites do diagnóstico:** os dois segmentos completos,
incluindo a falha incompleta a 59,94p, foram copiados para
`tools/audio-audit/data/until-0900-2026-11-02/`, com logs/CSV comprimidos, estados,
resultados e manifesto SHA-256; caches e perfis temporários CEF foram excluídos.
Para um futuro ensaio de diagnóstico, é necessário distinguir explicitamente
os motivos de recuperação (fila vazia versus rejeição do servo), preservar o
buffer antes do reabastecimento, a duração/intervalo dos callbacks e o motivo de
rejeição, e permitir que o watchdog obtenha um despejo limitado antes de encerrar.
Esta é uma proposta de instrumentação, não uma correção da causa nem uma alteração
já aplicada ao percurso de áudio. Os testes físicos permanecem parados.

**Consolidação às 05:29 — encerramento antecipado por falha, sem novo arranque.**
O limite das 09:00 era o prazo máximo autorizado. Os testes físicos terminaram
às 04:39:40 por reprovação de continuidade; a análise e preservação subsequentes
estão concluídas. Não se ocupa o tempo restante com repetições sem capacidade de
distinguir a causa. A verificação dos 745 ficheiros do manifesto não encontrou
qualquer divergência SHA-256.

| Verificação após recuperação comunicada | Resultado |
| --- | --- |
| Duas matrizes de oito alterações A/V a 25p/50p | 16 ensaios aprovados; erro máximo da mudança cerca de 0,021 ms |
| Dois blocos de 20 minutos a 25p | Ambos aprovados; amplitudes amostradas 11,021 e 6,125 ms |
| Dois blocos de 20 minutos a 50p | Primeiro reprovado, amplitude 40,063 ms; segundo aprovado, 13,958 ms |
| Bloco a 59,94p | Incompleto e reprovado: recuperação áudio, frame atrasada e descartada aos 442,5 s |
| Energia durante os ensaios | Verificações AC e asserções de repouso válidas até às interrupções |
| Fiabilidade geral / bloqueio do LCD | Não certificada; causa do incidente permanece por esclarecer |

A precisão dos ajustes negativos ficou demonstrada nas condições controladas;
a fiabilidade a 59,94p e a estabilidade de fase a 50p não ficam aprovadas. Não há
base para prometer um tempo até o lip sync se tornar inaceitável ou uma melhoria
percentual global. A proposta seguinte é instrumentar o motivo de recuperação e
a cronologia dos callbacks, garantir o despejo no encerramento por watchdog e
só então repetir um ensaio curto dirigido ao evento 59,94p e ao comportamento
PLAY/STOP. Esse passo requer nova decisão do utilizador/auditor. Não foi publicada
versão nem repetido o ensaio de oito horas. O acompanhamento periódico termina
com esta consolidação; os registos não dependem de o chat continuar ativo.

### 10.17 Instrumentação da recuperação e ensaio dirigido (2026-11-02)

**Alteração autorizada e implementada:** o servo expõe o motivo da sua última
decisão sem alterar os limiares, a estimativa, a correção ou as regras de retorno.
O diagnóstico distingue fila áudio vazia de recuperação pedida pelo servo por
intervalo excessivo, salto de fase ou taxa estimada fora do limite. Consultas
lentas, tempo não monótono e consumo recuado também ficam identificados quando
observados, mesmo que não determinem recuperação. Preserva-se a estimativa bruta
que levou à decisão, antes de um reset a apagar.

No modo explícito de diagnóstico, acrescentou-se uma janela circular de 512
callbacks áudio. Guarda intervalo entre entradas, duração, buffer **antes** do
reabastecimento, cursor, consumo, tempo de stream e motivo. No primeiro incidente
(recuperação ou intervalo não-preroll superior a 50 ms), conserva o contexto
anterior e mais 128 callbacks, congelando depois a janela. As recuperações e
notificações de vídeo atrasado/descartado têm buffers separados, limitados a
4096 eventos, com contagem de omissões. A memória é reservada antes do PLAY;
não se escreve no terminal nem se aloca memória ao acrescentar estes eventos.
Não foram aumentadas reservas nem alterados o perfil Reliable ou o caminho de
seleção de frames para tentar ocultar a falha.

`analyze_recovery.py` valida os resumos/contagens e rejeita diagnóstico ausente ou
truncado. Compara o instante da notificação de vídeo com o intervalo do callback
áudio (antes, durante ou depois). Isto estabelece **ordem de notificações SDK**,
não ordem física no cabo nem causalidade entre o atraso e a recuperação. A janela
é limitada: não contém todos os callbacks de uma sessão longa e pode congelar
num incidente anterior ao evento que se pretenda investigar.

**Encerramento por watchdog:** `run-endurance.py` passa um ficheiro de controlo
privado à app de teste. Em caso de interrupção, pede STOP/quit pelo temporizador
da aplicação, aguarda até 8 s para guardar os traces e só depois usa a terminação
forçada já existente se necessário. O mecanismo é restrito ao teste SDI explícito,
não ao uso normal da interface. O manifesto distingue confirmação do pedido e
preservação do diagnóstico; não considera o teste aprovado por ter terminado bem.

**Verificação do próprio mecanismo, 08:44:54–08:45:42:** foi provocada uma paragem
intencional do watchdog aos 45 s, em 59,94p. O executor terminou com erro esperado,
mas a app confirmou `SYNC_DIAGNOSTIC_STOP`, guardou os três novos resumos, os traces
A/V/relógio e `CEF shutdown complete`. Foram preservados 512 callbacks, sem
recuperações ou notificações de atraso/descarte nessa janela. Máximos observados:
intervalo 21,042 ms, consulta 0,104 ms e duração 0,417 ms. A paragem intencional
**não é uma falha espontânea de continuidade nem um ensaio de fiabilidade aprovado**.

**Controlo de variáveis:** a primeira compilação incluiu recursos de interface de
outro trabalho ainda não commitado. O ensaio dirigido inicial foi parado
controladamente às 08:47:45 e classificado como inconclusivo, preservando os seus
registos e motivo. O erro genérico de captura no manifesto resulta dessa paragem
antecipada; não foi atribuído à placa. Antes da repetição, foram repostos na
candidata os recursos da candidata anterior; os hashes dos 35 ficheiros de base
coincidem. As alterações de interface no workspace foram preservadas e não fazem
parte deste trabalho. A repetição controlada tem oito minutos de medição após
calibração, com capturas periódicas; não é um novo ensaio de oito horas.

**Validação automática:** passaram os oito CTests atualmente configurados (incluindo
a verificação de recursos UI existente no workspace) e 72 testes Python. A nova
cobertura verifica congelamento/ordem da janela circular, motivos de rejeição do
servo, rejeição de diagnóstico incompleto e classificação temporal sobreposta.
O teste C++ de diagnóstico passou também com AddressSanitizer e
UndefinedBehaviorSanitizer, incluindo os novos casos de motivos. Não foi feita
publicação nem substituída a instalação normal da aplicação.

**08:50 — falha reproduzida com interface controlada:** o ensaio de oito minutos
foi interrompido pelo watchdog antes de completar a duração. A primeira linha
com falha surge aos **87,202 s**, com 1 frame descartada (índice 5157); a última
telemetria, aos 92,402 s, mantém 0 recuperações áudio, 0 frames atrasadas e 0
amostras em falta. O servo permanece pronto. O trace confirmou a notificação SDK
`result=2` dessa frame e nenhuma recuperação áudio. A app confirmou o pedido de
paragem e guardou todos os resumos, encerrando CEF normalmente. O contexto áudio
retido teve máximos de intervalo 24,521 ms, consulta 0,208 ms e duração 0,500 ms;
estes máximos da janela não provam que toda a sessão foi livre de perturbações.

Este resultado separa uma falha de vídeo de uma recuperação áudio: não se deve
atribuir automaticamente qualquer frame descartada ao servo ou ao esvaziamento
da fila áudio. A causa do descarte continua em aberto. O ensaio é **reprovado e
incompleto**, preservado sem alterar os critérios.

**Ajuste do diagnóstico:** uma notificação de atraso/descarte de vídeo passa a
acionar também o congelamento das janelas de contexto áudio e agendamento de
vídeo. Isto evita perder contexto quando a falha não provoca recuperação áudio.
O contexto de agendamento reutiliza a consulta de tempo SDK já existente no
worker; não introduz consultas adicionais ao dispositivo. Regista início/fim,
última conclusão do slot e tempo previsto de apresentação. O analisador calcula
espera do worker e margem estimada usando a idade dessa observação, assumindo
reprodução a 1×; essa margem não é medição independente de prazo de hardware.
A repetição seguinte limita-se a três minutos após calibração, sem alterar
reservas, reiniciar a placa ou modificar os recursos de interface de referência.

**Conclusão da repetição curta:** a execução configurada para 180 s de medição
completou 194,141 s após calibração (a preparação terminou antes da margem fixa
reservada pelo executor). Todos os contadores de continuidade/histórico/conversão
ficaram a zero, mas a qualidade foi **reprovada**: envolvente amostrada −14,688 a
+24,958 ms, amplitude 39,646 ms, acima da frame de 16,683 ms mais 1 ms de tolerância.
Não houve recuperações áudio nem notificações de frames atrasadas/descartadas.
Não se usa esta execução sem falhas de continuidade para invalidar o descarte
observado no ensaio controlado anterior.

Na janela final de 512 agendamentos, sem incidente, a duração máxima foi 1,813 ms,
a espera máxima do worker 0,188 ms e a margem estimada mínima 31,354 ms. Estes
números caracterizam essa janela saudável, **não** a frame 5157 descartada antes
de existir a instrumentação de agendamento. As tentativas opcionais de
correspondência física por host periódico nos três ficheiros de captura foram
rejeitadas pelo analisador por discordância da época de vídeo entrada/saída.
Essa correspondência não foi aceite nem usada para atribuir a variação A/V à
fonte, ao SDK ou ao hardware. O resultado de qualidade acima vem do analisador
físico de capturas, sem depender dessa associação adicional.

**Entrega e pendência:** a instrumentação e o encerramento controlado ficaram
implementados e validados; não foi aplicada uma correção causal ao descarte nem
à envolvente A/V. A causa exige capturar uma recorrência com ambas as novas
janelas acionadas. O passo seguinte proposto é esse ensaio dirigido, mantendo a
mesma interface e reservas, antes de discutir alterações de buffers, worker ou
servo. Aguardará decisão do utilizador/auditor. Todos os ensaios deste passo
terminaram antes das 09:00; a verificação final confirmou `Busy=0` e ausência de
processos de teste próprios. Não se repetiu o teste de oito horas.

Evidência deste passo: `tools/audio-audit/data/recovery-diagnostics-2026-11-02/`.
A candidata local é `/tmp/u2k-recovery-diagnostic/URL2KeyFill.app`; o seu executável
é identificado nos manifestos por ensaio. Não houve publicação de versão.

### 10.18 Dois ensaios finais com limite explícito (2026-11-02, noite)

O utilizador autorizou dois ensaios, com critério de saída: (1) um ensaio dirigido
até 20 minutos para captar a falha com a instrumentação completa; (2) uma validação
até 30 minutos, após uma correção sustentada pelos dados. Se o primeiro não
permitir diagnosticar a causa, não se abre outra ronda automaticamente. O ensaio
de oito horas fica fora deste passo.

Às 21:28 a placa estava livre (`Busy=0`) e o Mac em alimentação AC. O primeiro
ensaio usa a candidata instrumentada já preparada, com a interface de referência,
a 59,94p e perfil Reliable. Configuraram-se 1155 s de medição nominal mais 45 s de
preparação no executor, limitando o PLAY programado a 1200 s (20 minutos), com
capturas periódicas a cada 120 s e watchdog de continuidade. Não foram alteradas
reservas, firmware, driver ou configuração persistente do dispositivo.
Registos: `/tmp/u2k-two-tests-20261102/test1-directed` e
`/tmp/u2k-two-tests-test1-20261102.runner.log`. Este é o registo de arranque, não
uma aprovação antecipada. A segunda validação depende do resultado e não foi
iniciada neste momento.

**Resultado e encerramento da ronda limitada, 21:50:** o primeiro ensaio completou
1199,701 s de atividade (20 minutos programados, incluindo preparação) e
1168,820 s após calibração. A continuidade passou: zero recuperações áudio,
frames atrasadas/descartadas, amostras em falta, erros API, resets e falhas de
histórico/conversão. Foram verificadas 79 capturas de estado de alimentação e
asserções de repouso, todas válidas. A falha de descarte anterior **não se
reproduziu**, pelo que não surgiu evidência que fundamente uma correção causal.

A qualidade A/V voltou a reprovar no critério existente. Medianas inicial/final:
+2,521/−6,417 ms (diferença −8,938 ms). O analisador periódico reportou envolvente
−30,229 a +138,063 ms, amplitude 168,292 ms. Porém, os eventos de +138,063 e
+75,500 ms aparecem em capturas com flashes sem correspondência, sem IDs de
conteúdo que confirmem a associação. São resultados brutos do emparelhamento,
**não excursões de lip sync independentemente certificadas**. Não foram removidos
para obter aprovação. Mesmo a janela com mediana −21,938 ms já ultrapassa o
limite absoluto de uma frame mais 1 ms a 59,94p. A análise do PCM aceite contra
vídeo selecionado também variou: −33,917 a +13,833 ms em 1193 pares periódicos,
sem constituir medição da emissão física ou prova de causa.

O trace não contém recuperações nem notificações de atraso/descarte. As janelas
finais, sem incidente, guardaram 512 callbacks/agendamentos: máximos de intervalo
áudio 22,000 ms, consulta 0,104 ms, duração áudio 0,333 ms, duração de agendamento
1,188 ms e espera do worker 0,208 ms; margem estimada mínima 32,021 ms. Estas
janelas saudáveis não esclarecem o descarte ocorrido noutra sessão.

**Decisão conforme o critério acordado:** foi executado **1 dos até 2 ensaios**.
O segundo, de 30 minutos, estava condicionado a uma correção sustentada pelos
dados do primeiro. Essa condição não foi satisfeita: não se aplicou uma alteração
especulativa, não se iniciou o segundo e não se abriu outra ronda. A candidata
permanece sem aprovação geral de estabilidade A/V/fiabilidade a 59,94p. Este
encerramento não transforma os episódios anteriores em aprovados nem promete
que um ensaio mais longo resolveria a investigação.

Os resultados estão preservados em
`tools/audio-audit/data/two-final-tests-2026-11-02/`, incluindo orçamento fechado,
manifestos, hashes, telemetria, capturas, qualificação das associações periódicas
e diagnóstico. A verificação final confirmou placa livre (`Busy=0`) e ausência
de processos de teste próprios. O acompanhamento automático é eliminado com o
encerramento. Não foi alterado o percurso de saída, publicada uma versão nem
iniciado o ensaio de oito horas.

### 10.19 Revisão da arquitetura A/V — sem novos ensaios físicos (2026-11-02)

**Resposta à questão de fundo:** o percurso normal consegue ordenar áudio e
imagens numa escala de tempo da máquina, mas não dispõe de informação suficiente
para garantir que ambos representam o mesmo instante do conteúdo de uma página
arbitrária. O timestamp de chegada da imagem inclui atrasos de renderização,
captura e entrega. O servo da saída corrige relógios/consumo; não recupera a
identidade temporal que a captura não forneceu. Esta é uma limitação verificável
da arquitetura atual, não uma atribuição da causa dos descartes de frames.

A revisão consultou o código da aplicação e os headers da versão CEF incluída
no repositório (`cef_binary_156.0.14+g4fa4ab5+chromium-156.0.8078.48`). Não houve
execução da aplicação, acesso de teste ao UltraStudio, nova campanha de medição
ou alteração do percurso de saída.

#### Caso 1 — o áudio e a imagem usam timestamps equivalentes?

**Parcialmente: partilham uma escala, mas a semântica é diferente.**
`Browser::OnAudioStreamPacket` (`src/browser/Browser.hpp:41`) passa o `pts` ao
`AudioLane::push`, que o converte de milissegundos para amostras (`Scene.cpp:13`).
A aplicação preserva esse timestamp, com tratamento de contiguidade, sobreposição
e gerações; não substitui normalmente o PTS pela chegada do callback áudio.

Já `Browser::OnPaint` (`src/browser/Browser.cpp:29–39`) recebe BGRA, dimensões e
retângulos alterados, **sem parâmetro de timestamp**. A aplicação atribui
`paintTime=mediaSampleTime()` à entrada do callback. Portanto, esse valor identifica
a receção da pintura, sem certificar o instante de apresentação ou descodificação
do conteúdo que está nos píxeis. Mudar o nome dessa hora para PTS, ou subtrair-lhe
uma constante, não cria a informação em falta.

**Cuidado de contrato:** o comentário de `cef_audio_handler.h:77–90` descreve o
PTS áudio como milissegundos desde Unix Epoch. A aplicação usa uptime com base
nas observações anteriores desta build, explicitadas em `Browser.hpp:39` e na
auditoria. Não se deve trocar agora para Epoch apenas por esse comentário, nem
assumir uptime para qualquer atualização CEF. A implementação seguinte deve
validar o domínio de relógio por origem/geração e rejeitar uma incompatibilidade
explicita, sem estimar silenciosamente uma nova origem por chegada dos pacotes.

#### Caso 2 — um ajuste fixo ou uma reserva maior resolve essa diferença?

**Não oferece uma garantia de sincronismo variável.** Exemplo de impossibilidade:
duas imagens dos mesmos tipos de evento podem chegar com 10 e 45 ms de atraso
sobre o conteúdo; corrigir ambas por uma constante mantém 35 ms de diferença.
É um exemplo lógico, não outra medição do equipamento. Uma reserva maior pode
ajudar a suportar uma interrupção de execução, mas acrescenta latência e não
reconstrói o instante de conteúdo. A correção negativa já implementada continua
válida para aplicar um ajuste escolhido; não deteta automaticamente esse ajuste.

A seleção SDI (`src/output/DeckLink.cpp`, `DeckLink::schedule`) usa um alvo derivado
do mapeamento áudio e procura uma frame no histórico da cena. Esse histórico é
normalmente indexado por chegada de pintura. Assim, a seleção é coerente com o
índice disponível, mas essa coerência não prova uma associação de conteúdo A/V.
Os descartes SDK continuam um problema separado de continuidade/agendamento.

#### Caso 3 — a composição conserva a associação por camada?

**No caso simples preserva metadados; no caso misto não existe um PTS comum
certificado.** O atalho de uma única camada à resolução final devolve a própria
frame (`Scene.cpp:115–118`), e o redimensionamento preserva `paintTime/contentTime`
(`Core.cpp:91`). Estas otimizações já existem; não há aqui outra composição/cópia
a remover sob a mesma promessa.

Com várias camadas, `Scene::run` obtém a imagem mais recente de cada uma por
`LatestFrame::get()` (`Scene.cpp:131`), em vez de as selecionar por um instante de
conteúdo comum. `composite` atribui ao resultado o maior `paintTime` e deixa
`contentTime` indefinido (`Scene.cpp:119`). Os testes existentes
`SceneEventsTests.cpp:33–36` verificam esse comportamento. Deixar o PTS indefinido
é preferível a inventar um PTS de conteúdo comum; copiar o maior `contentTime`
para o resultado **não seria uma correção**. Falta um contrato por origem para
saber qual é a camada A/V de referência e em que instante cada camada contribuiu.

#### Caso 4 — porque funcionaram os ajustes da referência identificada?

O percurso de diagnóstico tem informação adicional: descodifica o ID da imagem,
obtém a origem temporal a partir dos marcadores PCM e constrói
`contentTime=origin+id*960` (`Browser.cpp:33–35`). A opção que o ativa é restrita ao
ensaio de referência (`Main.mm`, validação de `--sdi-av-content-pts`). Não é um
serviço disponível para o conteúdo de qualquer URL. Os bons resultados desses
ajustes demonstram o mecanismo sob esse contrato conhecido, não a sincronização
geral de todas as páginas. Mensagens JavaScript com `mediaTime` ou um callback
de vídeo, sem associação inequívoca à pintura recebida, não substituem esse
contrato. A troca de mensagens assíncronas pode voltar a misturar frames.

As páginas de diagnóstico também têm limitações próprias: o flash é desenhado
por `requestAnimationFrame` a partir da observação de `AudioContext.currentTime`,
enquanto o impulso PCM é gerado/agendado separadamente (por exemplo,
`assets/Diagnostics/05_Audio_Esquerdo.html:35,50,81–83,104`). Usar o relógio áudio
na página não faz o flash chegar ao callback nativo exatamente com esse impulso.
Isto limita a interpretação dos ensaios, sem justificar remover as reprovações.

#### Caso 5 — GPU ou cadência externa resolvem a arquitetura?

**São possibilidades de desempenho/pacing, não uma correção já demonstrada.**
`OnAcceleratedPaint` disponibiliza IOSurface e metadados; no header incluído,
`cef_types_osr.h:48–59`, `extra.timestamp` é descrito em microssegundos desde o
início da captura. É informação temporal melhor que uma mera chegada, mas o
contrato local não estabelece, por si só, a origem absoluta comum com o PCM nem
que esse valor seja o PTS de descodificação de um elemento da página. Não se
conclui que seja inútil ou incapaz de resolver a captura; seria preciso estabelecer
esse mapeamento de apresentação/captura antes de o usar como garantia A/V.

O percurso CPU atual não implementa esse callback. Migrar para IOSurface requer
copiar/reter um recurso próprio durante o callback, como exige o header CEF;
não é seguro guardar o handle emprestado depois do retorno. Essa migração pode
reduzir transferências, mas não deve ser vendida como sincronismo resolvido.
`SendExternalBeginFrame` pode controlar pedidos de renderização; não acrescenta
um timestamp ao `OnPaint` atual nem associa automaticamente o PCM a uma pintura.

Há ainda conversão de cadência: `Browser::frameRate` arredonda 59,94 para 60
(`Browser.cpp:15–17`). Isso exige seleção/repetição ao sair a 60000/1001, mesmo
com uma fonte regular. Não prova a causa dos descartes SDK nem toda a envolvente
medida. Alterar para 59 fps também não reproduziria a cadência racional correta.
25p não corrige o contrato de conteúdo: foi apenas a configuração provisória
mais favorável nos ensaios curtos já efetuados, sem certificação geral de lip sync.

#### Alteração concreta recomendada — separar três contratos de origem

1. **Gráficos declarados sem áudio:** acrescentar um modo explícito por cena em
   que nenhuma origem contribui áudio para a saída. O SDI usa a imagem mais
   recente, mantendo o atalho de fonte única e evitando a seleção atrasada pelo
   histórico/margem de alinhamento áudio. A temporização/preroll do dispositivo
   continua válida; pode emitir PCM zero se o arranque SDK o exigir. Não se
   remove a gestão do dispositivo apenas por deixar de haver mistura/SRC.
   O atalho deve obter a última frame **já preparada** para o formato SDI
   (por exemplo, um snapshot imutável `{frame, preparedSDR}` ou acesso à última
   entrada preparada), em vez de converter a imagem dentro do callback/worker
   a cada apresentação. O cache/conversão SDR por publicação mantém-se; elimina-se
   a procura temporal e pode reduzir-se a retenção da cena, sem trocar atraso
   por cópias/conversões repetidas no caminho de saída.
   Isto ataca diretamente a latência do caso comum de grafismo silencioso.
   Não deve ser ativado por detetar alguns pacotes silenciosos nem por um mute
   transitório: esses casos podem voltar a ter conteúdo A/V. A mudança de contrato
   deve ser feita com a saída parada, para evitar um salto temporal em PLAY.
   A margem de fonte de 60 ms atualmente configurada deixa de ser uma razão para
   reter vídeo neste modo; a redução efetiva de latência não foi medida nesta
   revisão e não é prometida uma percentagem de melhoria.

2. **Origem A/V com timeline verificável:** definir uma interface de origem que
   entregue áudio e vídeo com timestamps de apresentação de semântica conhecida,
   origem, escala, velocidade e geração comuns. São vias possíveis um leitor
   nativo para ficheiros/streams compatíveis que preserve os PTS das duas pistas,
   ou uma integração de captura CEF que exponha e mapeie a mesma timeline de
   apresentação. PTS de descodificação não são a única solução possível; uma
   captura com timestamps de apresentação comuns e comprovados também serve.
   Um timestamp relativo sem origem conhecida ou uma mensagem JS sem identidade
   da frame não satisfazem o contrato. A via nativa deve ser tratada como um novo
   tipo de origem, não como solução para todas as páginas HTML da aplicação.

3. **Página HTML genérica:** manter compatibilidade com o percurso atual e
   explicitar que a relação A/V é estimada por captura/chegada. Não atribuir a
   esse percurso uma garantia de conteúdo que a interface não fornece. Deve
   continuar disponível para grafismo e páginas que o utilizador conhece, sem
   converter resultados de uma referência identificada numa certificação geral.

**Contrato interno proposto para implementação:** metadados por frame/pacote com
`sourceId`, `generation`, `timebase`, timestamp original, instante de captura/
apresentação quando disponível, instante de chegada e proveniência
(`arrival_only`, `capture_presentation`, `content_pts_verified`). Normalizar para
a timeline da cena apenas depois de validar a origem. Não promover uma hora de
chegada a PTS certificado; não misturar dados de gerações diferentes após seek,
reload ou troca de origem. O compositor deve selecionar a camada A/V de referência
para o instante pedido e conservar a proveniência das contribuições. Overlays
estáticos/silenciosos podem usar a sua imagem atual; várias origens A/V exigem
mapeamentos explícitos por origem, sem inventar um PTS global pelo máximo.

**Critérios de conclusão do trabalho de implementação:** perfil silencioso usa
latest-frame e não altera cadência/preroll SDK; perfil A/V rejeita domínios
incompatíveis e conserva a associação em resize/composição; seek/reload cria uma
geração nova sem reutilizar áudio/vídeo antigos; HTML sem contrato continua
identificado como estimado; nada altera automaticamente o ajuste para esconder
uma falha. Estes contratos podem ser verificados com dados sintéticos em testes
locais antes de qualquer nova decisão sobre hardware. Esta revisão não iniciou
esses testes nem aprova antecipadamente a implementação.

**Decisão recomendada:** implementar primeiro o perfil explícito de gráficos sem
áudio e os metadados de proveniência. Para prometer lip sync em vídeo com som,
escolher depois uma origem A/V com timeline comprovada; um leitor nativo é uma
opção concreta para media suportada, e uma extensão CEF é a opção que preserva o
objetivo de páginas genéricas, caso consiga fornecer esse contrato. Não voltar
a calibrar buffers indefinidamente sobre timestamps de chegada à procura de uma
garantia que a arquitetura atual não define. Nenhuma destas propostas está
implementada por este texto e nenhuma garante resolver o descarte SDK já visto.

### 10.20 Publicação expressamente autorizada da versão 0.6.0 (2026-11-02)

A pedido do utilizador, foi compilada e publicada a versão **0.6.0, build 10**,
para macOS Apple Silicon/arm64. A tag `0.6.0` identifica o commit
`be969839b294b27aebfcb13a5885ceea78b79bc0`. A build Release com DeckLink e sandbox
CEF ativos foi feita numa cópia isolada desse commit, incluindo os controlos
partilhados e a monitorização PFL/AFL existentes no workspace. As propostas de
arquitetura da secção 10.19 não foram apresentadas como funcionalidades prontas:
o modo de gráficos sem áudio e um novo contrato de origem A/V não estão
implementados nesta versão.

Validação da publicação: 8/8 CTests, 72/72 testes Python, consistência dos recursos
gerados, testes de interface por bridge simulada e smoke test nativo com CEF real
(transparência, camadas, visibilidade, preferências temporárias, captura áudio e
páginas incluídas). Estes controlos não ativaram saída SDI nem reiniciaram a
campanha de ensaios físicos. O ZIP foi extraído e verificado: versão/build,
executável arm64 idêntico ao compilado e assinaturas `codesign --deep --strict`
válidas. É assinatura ad-hoc, sem notarização Apple.

Publicação: https://github.com/luismiggarcia/URL2KeyFill/releases/tag/0.6.0

Artefactos: `URL2KeyFill-0.6.0-macOS-arm64.zip` e `SHA256SUMS.txt`. O hash do ZIP
verificado localmente e confirmado pela API GitHub é
`a871094ac1b3374dc8cb711bc8248429d92dc2e9906cb4b8d2f80a2a1f7f770e`.
A release está pública, sem estado de rascunho/pré-release, e marcada como mais
recente. O código foi enviado para `codex/audio-timing`; a tag fixa a origem
exata dos binários. Não foi substituída a aplicação instalada no Mac.

As notas publicadas mantêm explícitos os limites conhecidos: estabilidade A/V
não certificada para HTML genérico, episódios de descartes/envolventes reprovadas
a 59,94p e custo de latência dos ajustes negativos. A publicação autorizada não
constitui aprovação de produção nem resolve as pendências das secções anteriores.
Logs e proveniência: `tools/audio-audit/data/release-0.6.0-2026-11-02/`.

## 11. Revisão do auditor ao trabalho de 30/10 a 02/11 (2026-11-02)

Âmbito: os 42 commits de `55dbb38` a `02e3829` no branch `codex/audio-timing`
(≈1 440 linhas novas em `src/`), com leitura do código e verificação local. Não
houve ensaio SDI físico nesta revisão.

### 11.1 Verificado e aprovado

- Build e **CTest 8/8** a passar (fora do sandbox, com o CMake 3.31.6 de `third_party/`).
- **Relógio:** `mediaSampleTime()` usa `mach_absolute_time`, o PTS CEF é preservado sem
  heurística de parede e existe uma margem de origem explícita (60 ms). Isto resolve 3.1 e 3.3.
- **Deriva (3.2):** `AudioDriftServo` (regressão de 120 s, banda morta de 1 ms, slew de
  5 ppm/s, limite de ±200 ppm) com resampler polifásico. A resposta medida do resampler
  cumpre os critérios do §8.2: 0,000 dB até 20 kHz, −0,19 dB a 21 kHz e ≤ −105 dB a
  23–24 kHz, nas fases 0, ¼ e ½.
- O vídeo é escolhido pelo mesmo mapeamento do áudio, o CEF segue a cadência do modo
  (V1, exceto 59,94) e a composição é feita por evento, com atalho para camada única.
- 8 h a 1080p25 com continuidade perfeita (§10.13). A precisão do ajuste negativo ficou demonstrada.
- Disciplina de registo: as reprovações não foram escondidas e há evidência com hashes.

### 11.2 Problemas encontrados

1. **[Alta] Mudar a visibilidade de uma camada no ar apaga a imagem SDI durante 3 frames.**
   `Scene::configure` chama `frames.history.clear()` em cada alteração de camadas
   (visibilidade, adicionar, apagar, reordenar). A nova composição recebe a hora atual,
   mas o SDI procura uma frame ≈ `audioTarget + sourceAge` no passado (≈180 ms a 25p).
   `FrameHistory::at` não encontra nenhuma, e `DeckLink::schedule` envia preto
   transparente, ou seja, o key fica a zero. Reproduzido com a `Scene` real: depois de
   esconder a camada B, a camada A, que continua visível, desaparece nas frames 15–17 e
   volta na 18. A 50p serão ≈6 frames. **Correção:** não limpar o histórico em
   `configure`; a mudança aparece com o mesmo atraso que o áudio, como diz o
   ARCHITECTURE.md. Em alternativa, quando não há candidata, usar a frame mais antiga.
   Acrescentar um teste em `SceneEventsTests`.
2. **[Média] `driftResets_` nunca volta a zero.** Três resets do servo (salto de fase
   > 100 ms, intervalo > 10 s ou taxa fora do limite em 10 observações seguidas) em
   **qualquer** momento da sessão chamam `fail()` e param a saída de vez. Numa emissão de
   dias, isto converte incidentes isolados e recuperáveis numa paragem. **Correção:**
   contar só numa janela deslizante (por exemplo, 3 em 10 min) e acrescentar um teste.
3. **[Média, arquitetura] O desvio de 25 ms em 8 h é cadência e não PTS.** O movimento
   de +2,97 ms/h (§10.13.3) coincide com a correção de taxa integrada
   (≈ −0,85 ppm × 8 h ≈ 24,5 ms). O CEF pinta ao ritmo do relógio do Mac, o SDI consome
   ao ritmo do relógio da placa, e a seleção desliza na grelha de frames. Resultado: um
   dente de serra de uma frame, com período ≈13 h a 25p. A 59,94p com o CEF a 60 fps
   salta uma frame a cada ≈16,7 s, o que é compatível com as envolventes piores a 59,94.
   **Recomendação:** ativar `external_begin_frame_enabled` e chamar
   `SendExternalBeginFrame` ao ritmo da saída (conclusões DeckLink ou mapeamento). Isto
   fixa a cadência de render ao relógio da placa, resolve o caso 59,94 e dá a cada
   pintura um instante de pedido conhecido. Não substitui o contrato de PTS de conteúdo
   do §10.19; antes de o adotar, confirmar no CEF 156 que cada BeginFrame produz no
   máximo um `OnPaint` associável. Discordo do §10.19 apenas por tratar esta via como
   irrelevante: não dá PTS de conteúdo, mas resolve a cadência.
4. **[Média] O domínio do PTS CEF não é validado.** O header do CEF descreve Epoch; o
   código assume uptime. Se uma atualização do CEF trocar o domínio, `push` aceita os
   pacotes e o servo entra em salto de fase, reset e, ao terceiro, `fail()` (ver 2).
   **Correção:** no primeiro pacote de cada geração, rejeitar a lane se
   |pts·48 − `mediaSampleTime()`| > 1 s e mostrar a mensagem na UI. O §10.19 já o pede.
5. **[Baixa] Higiene de tempo real.** `RenderAudioSamples` aloca vetores (`mixFloat`,
   `mixResampled`, `toPCM`, cópia dos inputs) e toma os mutexes da lane e do mixer
   dentro do callback DeckLink, com o seu próprio mutex fechado. A duração máxima
   medida, 0,333 ms, mostra que isto não é urgente. Pré-alocar quando houver oportunidade.
6. **[Baixa] Ferramentas de evidência.** `tools/audio-audit/run-models.sh` já não
   compila: faltam `MediaClock.cpp` e `AudioResampler.cpp`. Com essa correção numa
   cópia, os resultados mantêm-se. Os cenários de ±20 ppm do `drift_sim` modelam o
   percurso **sem** servo; devem ser rotulados como históricos ou passar a incluir o servo.
7. **[Processo]**
   - A versão 0.6.0 foi publicada a partir de `codex/audio-timing`; o `main` continua
     na 0.2.0, 42 commits atrás. É preciso decidir o merge.
   - Os commits `cb00fc5`, `72b4f20` e `dece228` não terminam com `AI-Assisted Code`.
     Já foram enviados para o remoto, por isso fica só o registo.
   - A recomendação 8 está cumprida de outra forma: CMake em `third_party/`, ignorado
     pelo Git e documentado no BUILD.md.

### 11.3 Ordem recomendada

Primeiro 1, porque é visível no ar em operação normal. Depois 2 e 4, que são pequenos
e testáveis sem hardware, e 6. A seguir, um protótipo de 3 a 59,94p antes de qualquer
nova campanha longa. O ponto 5 fica para quando se mexer no callback.

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

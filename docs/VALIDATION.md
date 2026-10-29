# Validation update — 29/10/2026, Studio Rack 0.2

- Installed version 0.2.0 in `~/Applications/URL2KeyFill.app`, preserving a hidden backup of the prior installed app. The installed copy passed the full non-SDI integration test and signature verification.
- No-device test passed with an explicitly test-only empty device list: SETUP driver help appeared and PLAY was disabled; real CEF rendering/audio capture continued. This test does not simulate successful hardware playback.
- Full Release arm64 application compiled with real CEF and DeckLink backend; local signature verification passed.
- CTest: 2/2 passed. Composition tests cover ordering, premultiplied alpha, transparent fitting, visibility, stereo gain/mixing, limiting, mute and bounded queues.
- Real AppKit/WKWebView/CEF integration passed: bridge, monitor geometry with layer column hidden/visible, mutually exclusive drawers, edit drawer toggle, custom dimensions and PAUSE guard. These checks evaluate the local UI DOM; no screen recording permission was used.
- Two independent Full HD transparent browser instances composed correctly; hiding one returned the expected alpha. Layer configuration persisted and CEF closed cleanly.
- CEF generated/captured real planar stereo audio at 48 kHz. A monotonic-versus-epoch timestamp mismatch was found and corrected before audio acceptance.
- HD Mini hardware integration passed at 1080p25: scheduled video completions, scheduled stereo audio, more than 4800 non-zero mixed PCM samples, PAUSE/resume and stop. The test finished with exit code 0.
- Timed integrated run: 8.47 seconds wall time, 2.95 seconds user CPU and 1.60 seconds system CPU. `time -l` reported maximum RSS 330,366,976 bytes (about 315 MiB); this is not a sum of the live memory of all CEF/WebKit helpers. It is a short integration run, not a long-duration benchmark.
- Chromium emitted an ad-hoc process validation-category warning on startup. `codesign --verify --deep --strict` passed and the test completed; Developer ID/notarized distribution remains pending.

No receiver loopback or new screen capture was performed. SDK acceptance and local non-zero PCM do not establish audible reception, measured A/V sync, physical Key correctness or production readiness. The remaining physical protocol is in HARDWARE.md and BACKLOG.md. No physical UHD claim is made.

---

# Validação — 28/10/2026

## Resultados efetivos

- Compilação completa Release arm64, CEF integrado e backend DeckLink ativado: passou.
- CTest: 1/1 passou. Abrange definições, temporização racional, proporções, alfa, stride, propriedade de buffers, pipeline e política de URL.
- Teste gráfico real `--smoke-test`: passou com código de saída 0, fora do sandbox da ferramenta de execução. O sandbox interno do Chromium permanece ativo.
- CEF 1920 × 1080: píxeis transparentes, opacos e parcialmente transparentes, ordem BGRA e orientação passaram.
- Viewport 640 × 480: ajuste proporcional para Full HD e margens transparentes passaram.
- Viewport 3840 × 2160 reduzido para Full HD: passou. Não valida saída SDI UHD.
- Persistência das definições, erro de carregamento com remoção da imagem e encerramento CEF completo: passaram.
- Verificação da assinatura ad-hoc com `codesign --verify --deep --strict`: passou após remover metadados Finder adicionados pelo OneDrive. Estes metadados podem reaparecer na pasta sincronizada.
- Consulta real ao driver com `decklink_probe`, fora do sandbox da ferramenta: «Sem equipamento de reprodução SDI compatível ligado.» Não foi iniciada saída física.

## Porta-chaves e dados do browser

O Chromium anterior tentou utilizar «Chromium Safe Storage». A configuração foi corrigida antes de repetir o teste: usa `use-mock-keychain` no macOS, com uma raiz temporária exclusiva e perfil em memória. Não utiliza o perfil pessoal do utilizador, não requer a palavra-passe do Porta-chaves e não guarda cookies ou autenticações entre execuções. O teste terminou sem o bloqueio anterior. Não foi feita uma auditoria de chamadas ao sistema; a conclusão assenta na configuração e no teste funcional.

Referência do parâmetro: [GoogleChrome chrome-launcher](https://github.com/GoogleChrome/chrome-launcher/blob/main/docs/chrome-flags-for-tools.md). O comportamento do perfil em memória está documentado nos headers CEF fixados, em `cef_types.h`, campos `cache_path` e `root_cache_path`.

## Pendente de equipamento

Seguir `HARDWARE.md` para validar Fill/Key simultâneos no UltraStudio HD Mini, cor e níveis, sombras e contornos, frequências inteiras e fracionárias, sinal contínuo com página estática, retirar/voltar ao ar, reinício e reconexão. A pré-visualização não comprova estes pontos.

Saída física UHD e desempenho prolongado dependem de equipamento compatível. O host Windows ainda não está implementado. A aplicação não está validada para produção.

## Atualização: equipamento reconhecido e modos 1080p60

Após autorizar as extensões BlackmagicIO e Network, o SDK detetou o UltraStudio HD Mini sem reiniciar. O utilizador apresentou uma fotografia da página de teste num monitor externo e confirmou a saída de imagem. A saída Key física e a composição na mesa continuam pendentes por falta de ligação disponível.

Corrigida a enumeração: consultar reprodução normal e Keying independentemente. No equipamento ligado, o driver 16.4 rejeita BGRA/ARGB em reprodução normal a 1080p50/59,94/60, mas aceita esses formatos com bmdSupportedVideoModeKeying. O filtro anterior descartava-os antes desta segunda consulta. A aplicação recompilada e a consulta real confirmam agora os três modos na lista, em BGRA e ARGB. Isto valida a enumeração; a reprodução e estabilidade nesses modos ainda precisam de teste.

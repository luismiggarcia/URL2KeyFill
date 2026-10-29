# Compilar e executar

## Versões fixadas

- C++20, CMake **3.31.6** (mínimo 3.21).
- CEF **156.0.14+g4fa4ab5+chromium-156.0.8078.48**, distribuição `macosarm64_minimal` de 24/10/2026. Chromium integrado; não usa o Google Chrome instalado.
- Blackmagic **Desktop Video SDK 16.0**, obtido pelo utilizador no portal oficial. O CMake verifica a versão dos headers.
- macOS Apple Silicon; deployment target CEF macOS 13. O driver pode exigir uma versão mais recente do sistema. Ambiente efetivamente testado em `VALIDATION.md`.
- Runtime Desktop Video encontrado nesta máquina: **16.4**. O SDK e o runtime têm versões distintas.

## Dependências

1. Instalar as Command Line Tools (`xcode-select --install`) ou Xcode. O CEF recomenda Xcode 16+; nesta máquina foi possível compilar com as Command Line Tools AppleClang 21.
2. Obter [CMake 3.31.6](https://github.com/Kitware/CMake/releases/tag/v3.31.6) e disponibilizar `cmake`/`ctest` no PATH.
3. Descarregar o [CEF Apple Silicon fixado](https://cef-builds.spotifycdn.com/cef_binary_156.0.14%2Bg4fa4ab5%2Bchromium-156.0.8078.48_macosarm64_minimal.tar.bz2) e extrair em `third_party/`. A pasta deve chamar-se `cef_binary_156.0.14+g4fa4ab5+chromium-156.0.8078.48_macosarm64_minimal`.
4. Obter o [Desktop Video 16.0 SDK](https://www.blackmagicdesign.com/developer/products/capture-and-playback/sdk-and-software), cumprir o registo/licença do fornecedor e extrair para `third_party/DeckLinkSDK`. Deve existir `third_party/DeckLinkSDK/Mac/include/DeckLinkAPI.h`. Não basta instalar Desktop Video.
5. Instalar Desktop Video e autorizar os componentes Blackmagic conforme o instalador. Reiniciar quando solicitado. Ligar o UltraStudio com um cabo **Thunderbolt**, não apenas USB-C.

SHA-256 dos arquivos usados nesta compilação:

```text
CEF minimal arm64:
ed994b907ad596ca609a0542a1e2b22aa97e93e98b8141fed4c24aef39b9ccf6
CMake 3.31.6 macos-universal:
330b9514f5112e5ed4fb08b8b05803b776fd9b539a6ae12927d14dcc0ee2ba8d
```

Os binários de terceiros e o SDK ficam ignorados pelo Git. Conservar as suas licenças ao distribuir o bundle; o SDK tem uma EULA própria. Não se transfere automaticamente o SDK para outro computador.

## Compilação completa

A partir da raiz do projeto:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DU2K_BUILD_APP=ON -DU2K_DECKLINK=ON
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
open build/URL2KeyFill.app
```

O resultado inclui framework CEF, recursos e processos Helper sandboxed. A assinatura local é ad-hoc; não é uma distribuição notarizada. Não se desativa o sandbox Chromium, TLS ou a política de origem. Não se deve usar `--no-sandbox` para resolver problemas.

Para indicar localizações alternativas, usar `-DCEF_ROOT=/caminho/cef` e `-DDECKLINK_SDK_ROOT=/caminho/SDK`.

## Núcleo sem dependências proprietárias

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

`-DU2K_BUILD_APP=ON -DU2K_DECKLINK=OFF` compila CEF e a interface, mas mostra expressamente que o backend de hardware não foi compilado. Não cria dispositivos simulados. **A aplicação entregue nesta máquina foi compilada com `U2K_DECKLINK=ON`.**

## Diagnóstico e teste CEF

```sh
./build/decklink_probe
./build/URL2KeyFill.app/Contents/MacOS/URL2KeyFill --smoke-test
```

O primeiro comando enumera dispositivos/modos reais sem iniciar saída. O segundo abre uma janela, renderiza a página integrada, verifica píxeis e fecha a aplicação; não ativa SDI. Usa definições temporárias separadas em `$TMPDIR/url2keyfill-smoke`.

As definições normais ficam em `~/Library/Application Support/URL2KeyFill/settings.txt`. O Chromium usa um perfil temporário exclusivo por execução, com cache e cookies em memória e sem persistência de sessões web. A pasta temporária é removida após o encerramento normal. O acesso ao Porta-chaves macOS está substituído pelo backend de teste `use-mock-keychain`; nunca se reutilizam perfis de Chrome/Chromium existentes. As definições da aplicação continuam persistentes. A antiga subpasta `CEF` não é utilizada nem alterada. São guardadas ao carregar, iniciar e sair; uma configuração inválida não substitui o ficheiro existente. A escrita usa ficheiro temporário e renomeação atómica no macOS.

## Distribuição e Windows

Para distribuir no macOS: integrar avisos/licenças CEF, Chromium e SDK, assinar framework/helpers/aplicação com Developer ID e entitlements adequados ao CEF, notarizar, testar instalação limpa e atualizações de segurança. A assinatura ad-hoc só serve para desenvolvimento local.

O núcleo, as definições, a política de URL, o processamento de imagem e o cliente CEF são C++ portáveis. Falta implementar e validar o host Windows: janela e eventos (Win32 ou toolkit escolhido), preview, bootstrap CEF/sandbox Windows, inicialização COM, descoberta DeckLink por COM, conversão de nomes BSTR e GUID, paths de configuração e substituição atómica do ficheiro existente. A gestão de buffers e agendamento é reutilizável, mas `DeckLink.cpp` contém adaptação macOS que terá de ser isolada. Fixar então uma distribuição CEF Windows da mesma versão, incluir runtimes, assinatura e instalador. Não há binário Windows nem promessa de compilação do host macOS em Windows.


## Studio Rack 0.2 installation and tests

The application now bundles `assets/interface.html` and links the macOS WebKit framework. No npm dependencies, CDN scripts or preview server are used by the installed interface. CEF is still used for all URL video rendering.

```sh
ctest --test-dir build --output-on-failure
./build/URL2KeyFill.app/Contents/MacOS/URL2KeyFill --smoke-test
./build/URL2KeyFill.app/Contents/MacOS/URL2KeyFill --hardware-smoke-test
```

The first integration command uses temporary session data, two transparent local CEF test pages and a generated 440 Hz tone captured internally; it does not start SDI. The hardware command deliberately starts SDI video and stereo audio, then tests pause/resume/stop on a detected device. Both close automatically. They require normal desktop execution, not a shell sandbox that prevents GUI/driver access. They do not require screen recording or microphone permission.

`session.json` is the current settings file; the earlier `settings.txt` remains a migration source. The installed copy belongs in `~/Applications/URL2KeyFill.app`, outside OneDrive to avoid repeated Finder metadata/signature problems. Sign and verify this copy after installation. Keep the Desktop shortcut pointing to that stable path.


The test-only `--smoke-no-device-test` flag exercises empty-device setup guidance while retaining real CEF rendering/audio capture. It does not create fake output devices.

For subsequent local updates, close the application and run `bash scripts/install-local.sh`. It stages and verifies a new signed copy before replacing `~/Applications/URL2KeyFill.app` and preserves the previous installation as a hidden timestamped `.URL2KeyFill.previous.*.app` in the same Applications directory. Existing Desktop aliases/symlinks continue to work.

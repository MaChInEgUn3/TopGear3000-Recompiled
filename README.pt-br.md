<p align="center"><img src="docs/img/banner.svg" alt="Top Gear 3000: Recompiled" width="100%"></p>

<p align="center">
  <a href="https://github.com/MaChInEgUn3/TopGear3000-Recompiled/actions/workflows/build.yml"><img src="https://github.com/MaChInEgUn3/TopGear3000-Recompiled/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/licen%C3%A7a-GPL--3.0--or--later-blue.svg" alt="Licença: GPL-3.0-or-later"></a>
  <img src="https://img.shields.io/badge/plataformas-Linux%20%7C%20Windows-8b5cf6.svg" alt="Plataformas: Linux | Windows">
  <img src="https://img.shields.io/badge/status-pre--alpha-orange.svg" alt="Status: pre-alpha">
</p>

[English](README.md) · **Português (BR)**

# Top Gear 3000: Recompiled

Top Gear 3000: Recompiled é um port nativo para PC (Linux e Windows x86-64), ainda em estágio inicial, do jogo de SNES **Top Gear 3000**. Ele usa a lógica em C legível e verificada do [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp) e SDL3 para janela, entrada e áudio.

### **Este repositório e suas releases não contêm dados do jogo. O jogo original é necessário para rodar este projeto.**

Você precisa fornecer sua própria cópia legal da ROM **Top Gear 3000 (USA)**:
* Tamanho exato: `1.048.576` bytes (sem header de copier)
* SHA-1: `058aadf1ee719cb9e0c7333e665797f990a6381d`
* Coloque o arquivo em `input/TG3000.sfc` (ignorado pelo git) ou passe o caminho pela linha de comando.
* O firmware do DSP-4 (`dsp4.rom`) **não** é necessário.

## Índice
* [Estado atual](#estado-atual)
* [Nem 1% emulador](#nem-1-emulador)
* [Requisitos do sistema](#requisitos-do-sistema)
* [Planejado](#planejado)
* [Perguntas frequentes (FAQ)](#perguntas-frequentes-faq)
* [Como compilar](#como-compilar)
* [Estrutura do projeto](#estrutura-do-projeto)
* [Contribuindo](#contribuindo)
* [Bibliotecas usadas e agradecimentos](#bibliotecas-usadas-e-agradecimentos)
* [Aviso legal](#aviso-legal)

## Estado atual

O projeto está na **etapa 4 de 4** (pré-alfa). **Ainda não dá para jogar** e **ainda não existem releases**. Hoje, o executável compila no Linux e no Windows x86-64, confere sua ROM (tamanho e SHA-1) e roda algumas telas do jogo a partir do C do decomp, na velocidade do console, com teclado e controle já ligados (ainda sem uso: nada aparece na tela). A primeira tela já é desenhada de forma nativa: o cartão de título, idêntico ao original pixel a pixel. As outras telas ainda ficam pretas. Ainda não há saves, configurações, mods, widescreen nem gameplay.

| Etapa | Marco | Estado |
|---|---|---|
| 1 | Esqueleto (CMake, download do SDL3, checagem SHA-1 da ROM, janela vazia, CI) | ✅ Feito |
| 2 | Camada de plataforma (laço de quadros, controles; [desenho](docs/proposals/video-layer.md)) | ✅ Feito |
| 3 | Interface de som ([desenho](docs/proposals/sound.md); o tocador espera o decomp) | ✅ Desenhada |
| 4 | Primeiros menus nativos (cartão de título pronto, idêntico pixel a pixel) | ⏳ Em andamento |

## Nem 1% emulador

O jogo final não contém **nenhum** modelo da CPU do SNES (65816), chip de vídeo (PPU), processador de som (SPC700/S-DSP) ou coprocessador DSP-4.

* **Lógica do jogo:** vem do C legível do [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp), onde cada rotina portada é comprovada contra a ROM original em um teste diferencial.
* **Vídeo e som:** reescritos nativamente, desenhando cada tela e tocando áudio direto dos dados que o jogo monta na memória (tile maps, listas de sprites, paletas, tabelas de pista por linha).
* **DSP-4:** os comandos do coprocessador viram funções nativas em C.

**Teste prático:** se o código de vídeo ou de som conseguisse exibir ou tocar *outro* jogo de SNES dada a memória dele, então é um emulador — e isso é proibido aqui.

## Requisitos do sistema

* **Sistema:** Windows 10 ou superior (x86-64) ou Linux (x86-64)
* **Hardware:** nada exótico — o port usa SDL3 para janela, controles e áudio.

## Planejado

Os itens abaixo são **metas, não promessas** — nenhum deles existe ainda:

* Renderizador de vídeo nativo desenhado por tela a partir dos dados do jogo na memória
* Reprodutor de som nativo para músicas e efeitos sonoros
* Funções matemáticas nativas do DSP-4
* Mapeamento configurável de teclado e controle
* Versões prontas para baixar, para Linux e Windows x86-64

## Perguntas frequentes (FAQ)

#### Isso é uma recompilação estática?
Não — apesar do nome. "Recompiled" dá nome ao tipo de port nativo para PC. Nada é recompilado estaticamente a partir da ROM; a lógica do jogo é C legível escrito à mão no [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp) e verificado rotina por rotina contra o original.

#### Qual é a relação com o projeto de decompilação?
Este repositório consome o [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp) como um submódulo git somente leitura, fixado em um commit específico em `extern/tg3000-decomp/`.

#### É um emulador?
Não. Não há emulação de CPU 65816, PPU, SPC700/S-DSP ou DSP-4 no port. Veja [Nem 1% emulador](#nem-1-emulador).

#### Por que o MSVC não é suportado?
O C da decompilação usa builtins de GCC/Clang (como `__builtin_trap()`). Use GCC ou Clang (no Windows, MinGW-w64 UCRT via MSYS2 ou WinLibs).

#### Precisa do firmware do DSP-4?
Não. O arquivo `dsp4.rom` nunca é necessário, pois os comandos do DSP-4 serão implementados como funções nativas em C.

## Como compilar

Ainda não existem binários prontos em Releases. Início rápido (Linux ou Windows MSYS2 UCRT64):

```bash
git clone --recursive https://github.com/MaChInEgUn3/TopGear3000-Recompiled.git
cd TopGear3000-Recompiled
cmake -P tools/fetch_deps.cmake
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build --output-on-failure
```

Veja o [BUILDING.md](BUILDING.md) (em inglês) para os pré-requisitos completos, notas do Ubuntu 20.04, configuração no Windows, uso do SDL3 do sistema e comandos de teste.

## Estrutura do projeto

```text
extern/tg3000-decomp/   submódulo somente leitura com o C verificado da lógica do jogo
port/                   código do port nativo para PC (main.c, rom.c, sha1.c)
port/platform/          o lado do port nas chamadas de plataforma do jogo (laço de quadros, controles)
tools/                  scripts auxiliares de CMake (fetch_deps.cmake)
docs/                   decisões (DECISIONS.md) e propostas de desenho aceitas (proposals/)
deps/                   dependências baixadas (SDL3, ignorado pelo git)
input/                  local padrão da ROM do usuário (TG3000.sfc, ignorado pelo git)
build/                  diretório de compilação (ignorado pelo git)
```

## Contribuindo

Pull requests são bem-vindos. Nunca inclua dados da ROM, assets extraídos, firmware ou screenshots em commits ou PRs.

Alterações na lógica do jogo em si devem ser enviadas ao [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp), não aqui.

## Bibliotecas usadas e agradecimentos

* [SDL3](https://github.com/libsdl-org/SDL) (licença zlib) para janela, entrada e áudio multiplataforma
* [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp) (GPL-3.0-or-later) pela decompilação verificada em C da lógica do jogo

## Aviso legal

Licenciado sob **GPL-3.0-or-later** — veja [LICENSE](LICENSE).

Top Gear 3000 é © 1994 Kemco / Gremlin Graphics. Este é um projeto de fã não oficial, sem ligação com elas ou com a Nintendo.

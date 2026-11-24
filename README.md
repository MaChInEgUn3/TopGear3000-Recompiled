# Top Gear 3000 — port nativo (Linux e Windows)

Port nativo para PC (Linux e Windows x86-64) do jogo de SNES **Top Gear 3000**, usando a lógica em C legível e verificada do projeto irmão [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp), fixado como submódulo em `extern/`.

**Estado atual:** Etapa 1: esqueleto. Abre uma janela preta e confere a sua ROM. Ainda não roda o jogo.

## Regra do projeto: "nem 1% emulador"

O executável final não contém nenhum modelo de CPU (65816), PPU (chip de vídeo), SPC700/S-DSP (som) ou DSP-4.
Vídeo e áudio são desenhados e tocados nativamente a partir dos dados que o jogo monta na RAM, com código de desenho próprio por tela (e a matemática do DSP-4 vira C nativo).

**Teste prático:** se o código de vídeo ou som conseguiria exibir ou tocar *outro* jogo de SNES dada a memória dele, então é um emulador — e é proibido aqui.

Nenhum dado protegido por direitos autorais entra no git (sem ROM, sem gráficos/sons extraídos, sem firmware, sem screenshots).

## ROM necessária

Você precisa da sua própria cópia legal da ROM **Top Gear 3000 (USA)**:
- Tamanho exato: `1.048.576` bytes (sem header de copier)
- SHA-1: `058aadf1ee719cb9e0c7333e665797f990a6381d`
- Coloque o arquivo em `input/TG3000.sfc` (pasta ignorada pelo git) ou passe o caminho como argumento na linha de comando.

O firmware do DSP-4 (`dsp4.rom`) **não** é necessário.

## Como compilar

### Linux

Instale as dependências de compilação e do SDL3 (exemplo para Debian/Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols \
  libegl1-mesa-dev libgl1-mesa-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libudev-dev libdbus-1-dev libdrm-dev libgbm-dev
```

(Em Ubuntu antigo, como o 20.04, o `libpipewire-0.3-dev` não existe: é só tirar da lista. O CMake
precisa ser 3.24 ou mais novo; no 20.04 use o repositório da Kitware.)

Clone com submódulos, baixe o SDL3 fixado e compile:

```bash
git clone --recursive <url-do-repositorio>
cd tg3000
cmake -P tools/fetch_deps.cmake
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Para rodar:

```bash
./build/tg3000
```

### Windows

Use **MSYS2 (UCRT64)** ou **WinLibs MinGW-w64 (UCRT)** com `cmake` e `ninja` no PATH.
*(MSVC não é suportado porque o C da decompilação usa `__builtin_trap()`.)*

No terminal UCRT64 (ou MSYS2/Git Bash com MinGW-w64):

```bash
git clone --recursive <url-do-repositorio>
cd tg3000
cmake -P tools/fetch_deps.cmake
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/tg3000.exe
```

### Usando SDL3 do sistema

Se você já tiver o SDL3 instalado no sistema e não quiser compilar a partir de `deps/SDL3`:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTG3000_SYSTEM_SDL=ON
```

## Estrutura do repositório

- `extern/tg3000-decomp/` — submódulo somente leitura com o C da lógica do jogo
- `port/` — código do port nativo (`main.c`, `rom.c`, `sha1.c`)
- `tools/` — scripts auxiliares (`fetch_deps.cmake`)
- `deps/` — dependências baixadas localmente (ex.: `SDL3`, ignorado pelo git)
- `input/` — local padrão para colocar sua `TG3000.sfc` (ignorado pelo git)
- `build/` — diretório de compilação (ignorado pelo git)
- `docs/` — decisões de arquitetura e escopo

## Licença

Licenciado sob **GPL-3.0-or-later** (veja `LICENSE`).

---

Top Gear 3000 é © 1994 Kemco / Gremlin Graphics. Projeto de fã não oficial, sem ligação com elas ou com a Nintendo.

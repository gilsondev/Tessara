# Começando (português)

Espelho de [`../getting-started.md`](../getting-started.md). A documentação canônica é a versão em **inglês**.

## O que você precisa

| Objetivo | Requisitos |
|---|---|
| Testes nativos + E2E headless | Python 3.10+, PlatformIO no `.venv`, CMake, Ninja, pkg-config, SDL2, FreeType |
| Simulador interativo | O mesmo + display (janela) |
| Flash / monitor USB | Cabo + placa; porta serial muda por SO |

Hardware é **opcional** para a maior parte do trabalho. Prefira `make finish-host`.

## Pacotes

### macOS

```bash
brew install cmake ninja pkg-config sdl2 freetype python3
```

### Linux (Debian / Ubuntu)

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  libsdl2-dev libfreetype-dev \
  python3 python3-venv python3-pip git
```

### Linux (Fedora)

```bash
sudo dnf install -y \
  gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config \
  SDL2-devel freetype-devel python3 python3-pip git
```

### Linux (Arch)

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf sdl2 freetype2 python python-pip git
```

## Venv + PlatformIO

```bash
python3 -m venv .venv
.venv/bin/pip install -U pip platformio
.venv/bin/pio pkg install -e jc3248w535
# ou:
./tools/bootstrap-env.sh
```

## Verificar sem placa

```bash
make finish-host
make sim
```

Artefatos E2E: `/tmp/tessara-sim-out/` (ou `$SIM_OUT`).

## Placa (opcional)

Só depois de `make finish-host` verde: `make flash` e `make device`.  
Portas tipicas: macOS `/dev/cu.usbmodem…`, Linux `/dev/ttyACM0`. Ajuste `platformio.ini`.  
Não commite `include/secrets.h`, `.pio/` nem `.venv/`.

Detalhes e troubleshooting: use o guia em inglês.

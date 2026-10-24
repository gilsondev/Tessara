# Tessara painel — tasks do harness / finish gate
#
# Ao finalizar uma implementação, nesta ordem:
#   1. make test     — testes automatizados (PlatformIO native)
#   2. make e2e      — E2E no simulador (build + screenshots; inspecionar $SIM_OUT)
#   3. make flash    — grava no JC3248 (só com 1 e 2 verdes)
#
# Atalhos:
#   make finish       — test + e2e + flash
#   make finish-host  — test + e2e (sem placa)
#   make device       — monitor USB para teste manual nas telas do aparelho
#   make help         — lista targets

.PHONY: help test test-native e2e sim sim-build sim-run verify flash finish finish-host device device-snap monitor

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PIO  := $(ROOT)/.venv/bin/pio
SIM  := $(ROOT)/simulator
PAINEL := $(SIM)/build/painel
OUT  ?= /tmp/tessara-sim-out
SCENARIO ?= normal
export SIM_OUT := $(OUT)

help: ## Lista as tasks do harness
	@echo "Tessara painel — finish gate"
	@echo ""
	@echo "  make test         1) testes automatizados (pio test -e native)"
	@echo "  make e2e          2) E2E simulador (verify_all: nativos+build+screenshots)"
	@echo "  make flash        3) flash no dispositivo (tools/flash.sh)"
	@echo "  make device       4) acessar o aparelho (monitor USB) — teste manual / ver telas"
	@echo ""
	@echo "  make finish       gate completo (e2e já inclui nativos + flash)"
	@echo "  make finish-host  e2e sem flash (CI / sem placa)"
	@echo ""
	@echo "Simulador interativo:"
	@echo "  make sim-run      ./simulator/build/painel --scenario \$$SCENARIO --fixtures …"
	@echo "  make sim          sim-build + sim-run (default SCENARIO=normal)"
	@echo "  make sim-build    só compila o painel SDL"
	@echo ""
	@echo "Extras:"
	@echo "  make verify       alias de make e2e"
	@echo "  make device-snap  captura framebuffer do painel (SNAP → PNG)"
	@echo "  make monitor      alias de make device"
	@echo ""
	@echo "Artefatos E2E/SNAP: \$$SIM_OUT (default $(OUT))"
	@echo "Doc: docs/guides/finish-gate.md"

# --- 1. Testes automatizados -------------------------------------------------

test: test-native ## Criar/atualizar cobertura e rodar nativos

test-native: ## Suite Unity native (domínio, allowlist, compose, embed, screen_rest, …)
	cd $(ROOT) && $(PIO) test -e native

# --- 2. E2E simulador --------------------------------------------------------

e2e: verify ## E2E: nativos + build sim + screenshots headless (inspecionar design)

verify: ## ./simulator/scripts/verify_all.sh
	SIM_OUT=$(OUT) $(ROOT)/simulator/scripts/verify_all.sh

sim-build: ## Compila o binário SDL (painel)
	cmake -S $(SIM) -B $(SIM)/build -DCMAKE_BUILD_TYPE=Release
	cmake --build $(SIM)/build -j"$$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

sim-run: ## Roda o painel já compilado (SCENARIO=normal por padrão)
	@test -x $(PAINEL) || { echo "faltando $(PAINEL) — rode: make sim-build"; exit 1; }
	$(PAINEL) --scenario $(SCENARIO) --fixtures $(SIM)/fixtures

sim: sim-build sim-run ## Compila e abre o simulador interativo

# --- 3. Flash ----------------------------------------------------------------

flash: ## Build + upload no JC3248 (USB bootloader)
	$(ROOT)/tools/flash.sh

# --- 4. Dispositivo (teste manual) -------------------------------------------

device: ## Monitor USB — teste manual e ver telas no JC3248
	$(ROOT)/tools/device.sh monitor

monitor: device ## Alias de make device

device-snap: ## Captura a tela atual do painel (SNAP → $$SIM_OUT/device-snap.png)
	mkdir -p $(OUT)
	$(ROOT)/tools/device.sh snap $(OUT)/device-snap.png

# --- Finish gate -------------------------------------------------------------

finish: e2e flash ## Gate completo (e2e já roda nativos; depois flash)
	@echo "==> finish OK — nativos + sim + flash"
	@echo "    próximo: make device  # teste manual nas telas"

finish-host: e2e ## Gate host (sem flash)
	@echo "==> finish OK (host only)"

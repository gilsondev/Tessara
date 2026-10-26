# Tessera panel — harness / finish gate
#
# After any implementation, in this order:
#   1. make test     — automated tests (PlatformIO native)
#   2. make e2e      — simulator E2E (build + screenshots; inspect $SIM_OUT)
#   3. make flash    — program JC3248 (only when 1 and 2 are green)
#
# Shortcuts:
#   make finish       — e2e + flash (e2e already runs native)
#   make finish-host  — e2e without a board
#   make device       — USB monitor for manual on-device checks
#   make help         — list targets
#   ./tools/bootstrap-env.sh — once per machine (after OS packages)

.PHONY: help test test-native e2e sim sim-build sim-run verify flash finish finish-host device device-snap monitor pack-agentd push-agentd agent-install

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PIO  := $(ROOT)/.venv/bin/pio
SIM  := $(ROOT)/simulator
PAINEL := $(SIM)/build/painel
OUT  ?= /tmp/tessera-sim-out
SCENARIO ?= normal
export SIM_OUT := $(OUT)

help: ## List harness tasks
	@echo "Tessera panel — finish gate"
	@echo ""
	@echo "  make test         1) automated tests (pio test -e native)"
	@echo "  make e2e          2) simulator E2E (verify_all: native+build+screenshots)"
	@echo "  make flash        3) flash device (tools/flash.sh)"
	@echo "  make device       4) USB monitor — manual on-device check"
	@echo ""
	@echo "  make finish       full gate (e2e already includes native + flash)"
	@echo "  make finish-host  e2e without flash (CI / no board)"
	@echo "  ./tools/bootstrap-env.sh  once: venv + PlatformIO + lib deps"
	@echo ""
	@echo "Interactive simulator:"
	@echo "  make sim-run      ./simulator/build/painel --scenario \$$SCENARIO --fixtures …"
	@echo "  make sim          sim-build + sim-run (default SCENARIO=normal)"
	@echo "  make sim-build    compile SDL painel only"
	@echo ""
	@echo "Extras:"
	@echo "  make verify       alias of make e2e"
	@echo "  make device-snap  capture panel framebuffer (SNAP → PNG)"
	@echo "  make monitor      alias of make device"
	@echo ""
	@echo "Agent (Mac companion — agentd):"
	@echo "  make agent-install  build agentd+app e instala neste Mac"
	@echo "  make pack-agentd    agentd/pack_sd.sh → dist/sd/tessera/agentd.tgz"
	@echo "  make push-agentd    pack + dicas PUT /agentd/agentd.tgz"
	@echo ""
	@echo "E2E/SNAP artifacts: \$$SIM_OUT (default $(OUT))"
	@echo "Docs: docs/getting-started.md · docs/guides/finish-gate.md"

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

# --- Agent v2 SD pack (cutover Task 7) ---------------------------------------

AGENTD_SD := $(ROOT)/agentd/dist/sd/tessera/agentd

pack-agentd: ## Empacota agentd + Tessera.app para staging SD (sem segredos)
	$(ROOT)/agentd/pack_sd.sh

agent-install: ## Build agentd + Tessera.app e instala neste Mac (LaunchAgent + /Applications)
	bash $(ROOT)/tools/agent-install.sh

push-agentd: pack-agentd ## Pack agentd para SD / portal (PUT /agentd/agentd.tgz)
	@echo "==> push-agentd: artefatos em $(AGENTD_SD)"
	@echo "    Copiar ao cartão: ./agentd/pack_sd.sh /Volumes/TESSERA"
	@echo "      (grava também tessera/agentd.tgz flat — o que o portal serve)"
	@echo "    Instalar neste Mac: make agent-install"
	@echo "    Portal: PUT /agentd/agentd.tgz (com agent_push armado) ou upload na página Agente"

# Tessara Agent MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Separar **Agente Tessara** (status + instalação via SD/web) de **AI Tokens** (cards + Pedir cotas) no aparelho e no portal, com handshake `AGENT` e bootstrap em `~/.tessera/agent/`.

**Architecture:** Domínio `agent::` (RAM) parseia `AGENT\tv1\t…`. Settings ganha `Page::Agent`. Portal serve `/agent/install.command` e `/agent/agent.tgz` **streamando do SD** (`/tessera/agent/…`), sem embutir na flash. Mac: evoluir `tools/aitokens_bridge` → `tools/tessera_agent` (launchd `com.tessera.agent`, envia `AGENT` no connect + mantém `AITOKENS`).

**Tech Stack:** ESP32 Arduino + LVGL9, `sd_host`, portal WiFi raw sockets, Python 3 + pyserial + launchd, Unity native tests.

**Spec:** `docs/superpowers/specs/2026-10-24-tessera-agent-design.md`

## Global Constraints

- Nome de UI: **Agente Tessara** (não “Bridge” no hub).
- Pacote do instalador só no **SD** (`/tessera/agent/`); flash não cresce com tarball.
- **Pedir cotas** (`AITOKENS?`) só na página AI Tokens das configs (não na página Agente).
- Cotas USB continuam `AITOKENS` / `AITOKENS?` (compatível).
- Handshake novo: `AGENT\tv1\t1` ou `AGENT\tv1\t0\t<motivo>`.
- Paths Mac: `~/.tessera/agent/`, label `com.tessera.agent` (migrar `com.tessera.aitokens-bridge`).
- Fora: sinais extras do Mac; flip clock (follow-up); toggles de fontes.

## File map

| Path | Role |
|---|---|
| `firmware/components/core_services/agent/state.*` | Parse `AGENT`, View ok/detail/received_ms |
| `test/test_native/test_agent_state.cpp` | Testes parse |
| `src/platform/console.*` | Despachar linhas `AGENT` |
| `src/app/controller.cpp` | Guardar estado agente; publish no portal |
| `src/ui/settings_panel.*` | `Page::Agent` + AI Tokens cards + Pedir cotas |
| `src/ui/shell.cpp` / `ui.h` | Wire `show_agent` / refresh cotas a partir de settings |
| `src/services/web_portal.*` / `web_page.h` | `agent` no `/api/state`; stream SD; página Agente |
| `agent/` (monorepo sibling) | Mac Tessara Agent + pack_sd |
| `tools/aitokens_bridge/*` | Legado → aponta para `agent/` |

---

### Task 1: Domínio `agent::` + testes

**Files:**
- Create: `src/agent/state.h`, `src/agent/state.cpp`
- Create: `test/test_native/test_agent_state.cpp`
- Modify: `platformio.ini` (src filter native incluir `agent`)

**Interfaces:**
- Produces:
  ```cpp
  namespace agent {
    constexpr int kDetailMax = 48;
    struct View {
      bool ready = false;   // recebeu ≥1 AGENT válido neste boot
      bool ok = false;
      char detail[kDetailMax] = {};
      uint32_t received_ms = 0;
    };
    bool apply_line(View &view, const char *line, uint32_t now_ms);
  }
  ```
- Linha válida: `AGENT\tv1\t1` ou `AGENT\tv1\t0\tmotivo` (motivo opcional, sem tabs).
- Ignora `AGENT?` e linhas curtas demais.

- [ ] **Step 1: Teste falhando**

```cpp
#include <unity.h>
#include "agent/state.h"

void test_agent_ok_line() {
  agent::View v{};
  TEST_ASSERT_TRUE(agent::apply_line(v, "AGENT\tv1\t1", 1000));
  TEST_ASSERT_TRUE(v.ready);
  TEST_ASSERT_TRUE(v.ok);
}

void test_agent_fail_with_reason() {
  agent::View v{};
  TEST_ASSERT_TRUE(agent::apply_line(v, "AGENT\tv1\t0\tserial busy", 2000));
  TEST_ASSERT_TRUE(v.ready);
  TEST_ASSERT_FALSE(v.ok);
  TEST_ASSERT_EQUAL_STRING("serial busy", v.detail);
}

void test_agent_ignores_garbage() {
  agent::View v{};
  TEST_ASSERT_FALSE(agent::apply_line(v, "AGENT\tv0\t1", 1));
  TEST_ASSERT_FALSE(agent::apply_line(v, "AITOKENS\tv1\t1", 1));
}
```

- [ ] **Step 2:** `pio test -e native -f test_agent_state` → FAIL (sem impl)

- [ ] **Step 3: Implementar** `apply_line` espelhando o estilo de `aitokens/snapshot.cpp` (strtok_r / campos tab).

- [ ] **Step 4:** `pio test -e native -f test_agent_state` → PASS

- [ ] **Step 5: Commit** `feat(agent): parse AGENT USB handshake`

---

### Task 2: Console + controller wire

**Files:**
- Modify: `src/platform/console.h` (handler `on_agent(const char *line)`)
- Modify: `src/platform/console.cpp` — se `strncmp(text,"AGENT",5)==0` e não termina em `?`, chamar `on_agent`
- Modify: `src/app/controller.cpp` — membro `agent::View agent_view_`; em `on_agent` aplicar + `settings_panel::show_agent` + republish portal
- Modify: `src/ui/settings_panel.h` — `void show_agent(const agent::View &view);`
- Modify: `src/ui/shell.cpp` / `ui.h` se o paint passar pelo shell (seguir padrão `show_aitokens`)

**Interfaces:**
- Consumes: `agent::apply_line`
- Produces: estado agent disponível para settings + `web_portal::Snapshot`

- [ ] **Step 1:** Adicionar `on_agent` no console; controller aplica linha com `millis()`.

- [ ] **Step 2:** Estender `web_portal::Snapshot`:

```cpp
struct AgentInfo {
  bool ready;
  bool ok;
  char detail[48];
};
// em Snapshot:
AgentInfo agent;
```

Preencher em `publish` a partir do controller. Em `state()` JSON:

```json
"agent": { "ready": true, "ok": true, "detail": "" }
```

- [ ] **Step 3:** Build `pio run -e jc3248` (ou env do projeto) → OK

- [ ] **Step 4: Commit** `feat(agent): wire AGENT serial into controller and /api/state`

---

### Task 3: Settings aparelho — Agente + AI Tokens redesenhado

**Files:**
- Modify: `src/ui/settings_panel.cpp` (e `.h` se callbacks)

**Comportamento:**
- `enum class Page`: adicionar `Agent` (manter `AiTokens`).
- Hub: `link_row` **Agente Tessara** (tom distinto, ex. `palette::primary()` ou novo `widget_agent`) detalhe = Off / Ok / Falha / desatualizado; **AI Tokens** detalhe = resumo curto das fontes (não mais “Bridge ok”).
- `Page::Agent` (`fill_agent_status`):
  - Título status + motivo
  - Nota install via web
  - **Sem** botão Pedir cotas
- `Page::AiTokens` (`fill_aitokens_status` redesign):
  - Três cards compactos (nome, display, barra simples se pct, reset)
  - Botão **Pedir cotas** → callback `on_request_aitokens` (shell/controller manda `AITOKENS?`)
  - Rodapé: “Cotas via Agente Tessara”
  - Remover texto de install/launchd

**Interfaces:**
```cpp
// settings_panel.h
using RequestAiTokensFn = void (*)();
void open(ChangeFn, DismissFn = nullptr, RequestAiTokensFn = nullptr);
void show_agent(const agent::View &view);
```

- [ ] **Step 1:** Hub + página Agent (status only).

- [ ] **Step 2:** Redesign AI Tokens settings + wire Pedir cotas → `Serial.println("AITOKENS?")` (mesmo path do refresh da tela AI Tokens).

- [ ] **Step 3:** Flash + abrir Configurações: duas entradas; Agente sem cotas; AI Tokens com botão.

- [ ] **Step 4: Commit** `feat(settings): Agente Tessara page; AI Tokens cards + pedir cotas`

---

### Task 4: Portal web — página Agente + stream SD

**Files:**
- Modify: `src/services/web_page.h` — hub jump Agente; `page-agent` com status + botão download + passos
- Modify: `src/services/web_portal.cpp` — rotas GET:
  - `/agent/install.command` → stream `/tessera/agent/install.command`
  - `/agent/agent.tgz` → stream `/tessera/agent/agent.tgz`
- Paths SD sem prefixo de volume: `sd_host` já usa paths tipo `/tessera/...` (ver `weather_icons` / `list_dir("/tessera")`).

**Stream (obrigatório — não carregar tarball em `String`):**

```cpp
// Em send() ou rota dedicada antes de Reply:
bool send_sd_file(WiFiClient &client, const char *sd_path, const char *content_type,
                  const char *download_name) {
  if (!sd_host::ready() || !sd_host::exists(sd_path)) {
    send(client, text_reply(404, "pacote do Agente ausente no SD"));
    return true;
  }
  // Abrir via SD.open(sd_path) (mesmo backend que sd_host) OU
  // sd_host::read_file só para install.command pequeno;
  // para .tgz: ler em chunks 2–4 KiB e client.write.
  ...
}
```

Se `sd_host` não expõe open/chunk, adicionar:

```cpp
// sd_host.h
bool open_read(const char *path); // ou File-like callback
size_t read(uint8_t *buf, size_t n);
void close_read();
```

Preferir uma API mínima chunked em `sd_host` a duplicar `SD.open` no portal.

**UI web (`page-agent`):**
- Status de `s.agent` no `load()`
- Link/botão: `href="/agent/install.command"` download
- Nota: o `.command` baixa o tarball de `/agent/agent.tgz` no mesmo host **ou** o `install.command` no SD já embute URL relativa; preferir `.command` que usa `curl`/`curl` para `http://$HOST/agent/agent.tgz` passando host detectado — **mais simples:** `install.command` no SD é estático e instrui baixar `agent.tgz` do mesmo diretório de download (usuário baixa os dois), **ou** um único `.command` gerado que só descompacta se o tgz estiver ao lado.  
  **Escolha fixa no plano:** botão principal baixa `install.command`; o script tenta `curl -fsSL "http://<painel>/agent/agent.tgz"` usando o Referer/host — o HTML passa o host atual:

```html
<a class="act" id="agent-dl" download="install-tessera-agent.command">Baixar instalador</a>
<script>
document.getElementById('agent-dl').href='/agent/install.command';
</script>
```

O `install.command` no SD contém placeholder `TESSERA_PANEL_URL` **ou** lê a 1ª linha de args; mais robusto: página gera query `?host=http://192.168.x.x` e o firmware **não** reescreve o file — o `.command` usa:

```bash
PANEL="${1:-http://tessera.local}"
curl -fsSL "$PANEL/agent/agent.tgz" -o /tmp/tessera-agent.tgz
```

e o botão da web é:

```html
<a download class="act" href="/agent/install.command">Baixar instalador</a>
```

Documentar no card: após baixar, no Terminal: `bash install-….command http://IP_DO_PAINEL` (ou double-click se o script auto-detectar).

**Simplificação aceita:** double-click do `.command` usa `http://tessera.local` por padrão; fallback documentado na UI.

- [ ] **Step 1:** API chunked em `sd_host` + rotas stream.

- [ ] **Step 2:** HTML/JS página Agente + hub; pintar `agent` no `draw()`/`load()`.

- [ ] **Step 3:** Sem SD → 404 texto amigável na UI (`agent_pack: false` no state se `exists`).

Adicionar em Snapshot/state: `"agent_pack": true` se ambos os arquivos existem.

- [ ] **Step 4: Commit** `feat(web): Agente Tessara page and SD-streamed installer`

---

### Task 5: Pacote Mac — `tools/tessera_agent`

**Files:**
- Create/move: `tools/tessera_agent/` a partir de `tools/aitokens_bridge/` (bridge.py → agent.py, mesmas sources)
- Create: `tools/tessera_agent/pack_sd.sh` — gera `agent.tgz` + `install.command` em pasta de saída (copiar para o SD em `/tessera/agent/`)
- Create: `install.command` template (bash): venv em `~/.tessera/agent`, extrai tgz, plist `com.tessera.agent`, `launchctl bootout/bootstrap`, unload label antigo se existir
- Modify: agent envia ao conectar (e periodicamente leve):

```python
def agent_hello(ok: bool, detail: str = "") -> str:
    if ok:
        return "AGENT\tv1\t1\n"
    return "AGENT\tv1\t0\t" + detail.replace("\t", " ")[:40] + "\n"
```

No `main`, após `ser.open`: `ser.write(agent_hello(True))` (ou False se sources impossíveis). Manter push `AITOKENS`.

- Update: `tools/aitokens_bridge/README.md` → aponta para tessera_agent
- Update: spec AI Tokens settings paragraph → link para tessera-agent spec (opcional, 1 linha)

- [ ] **Step 1:** Mover/adaptar código + hello AGENT.

- [ ] **Step 2:** `pack_sd.sh` produz artefatos; documentar cópia para o cartão.

- [ ] **Step 3:** Instalar via script local (dev) e ver painel `agent.ok` + cotas.

- [ ] **Step 4: Commit** `feat(agent): tessera_agent package, launchd, SD pack script`

---

### Task 6: Verificação ponta a ponta

- [ ] **Step 1:** `pack_sd.sh` → copiar para SD → inserir no painel → `SD` remount se preciso.

- [ ] **Step 2:** Flash firmware; abrir web → Agente → baixar `.command` → instalar no Mac.

- [ ] **Step 3:** Confirmar: settings Agente = Ok; AI Tokens cards + Pedir cotas atualiza; home AI Tokens ok.

- [ ] **Step 4:** Atualizar `docs/superpowers/specs/2026-10-24-ai-tokens-design.md` (settings Bridge → “ver tessera-agent-design”).

- [ ] **Step 5: Commit** `docs: point AI Tokens settings at Tessara Agent spec`

---

## Follow-up (não neste plano)

- Screensaver flip: bordas dos dígitos extremos + animação mais fluida (`bs6`).

## Spec coverage check

| Spec | Task |
|---|---|
| Hub Agente + AI Tokens separados | 3, 4 |
| Agente status, sem Pedir cotas | 3 |
| AI Tokens cards + Pedir cotas | 3 |
| Web install + status | 4, 2 |
| SD package, não flash | 4, 5 |
| `AGENT` handshake | 1, 2, 5 |
| Bootstrap `~/.tessera/agent` | 5 |
| Migração label launchd | 5 |

# Tessara Agent

Pacote Mac do monorepo Tessara (irmão do firmware em `../src`).

Recursos via USB: **AI Tokens** (cotas), **Calendar** (Google, OAuth no Mac) e **MEDIA** (Agora tocando + play/pause/prev/next — Music/Spotify via AppleScript). Ver `capabilities/calendar/README.md`.

### Mac / MEDIA (painel)

O agente empurra `MEDIA\tv1\t…` (~8 s) e responde a `MEDIA?` / `MEDIA\tPLAYPAUSE|NEXT|PREV`. Fase 1 lê **Music** e **Spotify** (Automation do macOS pode pedir permissão na 1ª vez). Sem sessão → `stopped` / “Nada tocando” no cartão **Mac**.

USB: após abrir a porta o agente liga **DTR** (sem pulso de reset no open). Sem DTR o CDC do ESP32 não envia comandos do painel → Mac (play/pause, `CALENDAR?`, etc.).

### Cursor (cotas no painel)

O firmware **não** chama a API da Cursor. O agente no Mac resolve a sessão nesta ordem e empurra `AITOKENS`:

1. IDE — `~/Library/Application Support/Cursor/User/globalStorage/state.vscdb`
2. Keychain — `cursor-access-token` / `cursor-refresh-token` (conta `cursor-user`)
3. `~/.cursor/auth.json` (após `agent login` ou `AGENT_CLI_CREDENTIAL_STORE=file`)
4. Env — `CURSOR_API_KEY=crsr_…` (exchange) ou `CURSOR_TOKEN` (Bearer)

JWT curto é renovado em `POST /oauth/token`. Se todas as fontes falharem (`shouldLogout` / 401), abra o Cursor e entre de novo (ou `agent login`).

### Claude (Code + Desktop = uma fonte)

Uma linha `claude` no painel. O agente junta as duas superfícies da mesma conta:

1. Claude Desktop — Local Storage (janela de cota escrita ao conversar)
2. Claude Desktop — cookies → `GET claude.ai/api/organizations/{org}/usage`
3. Claude Code — `statusLine` → `~/.tessera/claude_status.json`

A leitura mais recente com `%` válido vence (**% consumido**, não restante). Se a checagem não achar limite novo, mantém `~/.tessara/claude_usage_cache.json` (última cota). Requer Claude Desktop logado (e/ou Code com statusLine instalado pelo `install.command`).

## Layout

```
agent/
  src/agent/                 # pacote Python
    __main__.py              # python -m agent
    runtime.py               # loop USB + handshake AGENT
    protocol.py
    capabilities/aitokens/
    capabilities/calendar/   # Google Calendar (readonly)
    capabilities/media/      # Now Playing + transport controls
  scripts/statusline_claude.sh      # Claude Code → ~/.tessera/claude_status.json
  scripts/claude_desktop_usage.js   # Claude Desktop cookies → claude.ai/usage
  pack_sd.sh                 # gera instalador p/ cartão SD do painel
  requirements.txt
```

Instalação no Mac (depois do pack no SD / download pela página web):

```text
~/.tessera/agent/            # runtime instalado
launchd: com.tessera.agent
```

## Dev (clone)

```bash
cd agent
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
PYTHONPATH=src .venv/bin/python -m agent
```

## Pacote no painel (SD)

`PUT /agent/agent.tgz` só aceita upload com a **janela armada**: USB `AGENT_PUSH`, `POST /api/agent/arm`, ou **janela de boot** (~3 min após reset — para `make flash-agent`). Depois do PUT 200 a janela fecha.

Versão: fonte única em `agent/__init__.__version__`. O pack grava `VERSION` no tarball + `agent.version` sidecar; o painel guarda `/tessera/agent.version` (header `X-Tessara-Agent-Version`). `make push-agent` compara e **pula** se igual (`FORCE=1` força).

Atualização no Mac (automática):
- `make push-agent` / `flash-agent` após o PUT roda `tools/install_agent_mac.sh` (pule com `SKIP_MAC_INSTALL=1`).
- O agente em `~/.tessera/agent` consulta `/api/state` ~2 min e, se a versão do painel diferir, baixa `install.command` e reinstala (desligue com `TESSARA_AGENT_NO_UPDATE=1`). Host: `TESSARA_HOST`.

```bash
# Deploy automatizado (painel já no app, Wi-Fi + SD):
make push-agent                 # pack + AGENT_PUSH + PUT
# ou, após firmware:
make flash-agent                # flash.sh → espera /api/state → push-agent
# Host do painel: TESSARA_HOST=192.168.1.x make push-agent

# Manual: pack + portal
./pack_sd.sh                    # → dist/sd/tessara/agent/agent.tgz
# Portal → Agente → escolher agent.tgz → Enviar (arma e envia) → instalar no Mac
```

O `.command` o painel gera no `GET /agent/install.command` (download livre).

Opcional (provisionamento único no Mac com cartão montado):

```bash
./pack_sd.sh /Volumes/TESSARA   # grava /tessera/agent/… no cartão
```

# Tessara Agent

Pacote Mac do monorepo Tessara (irmão do firmware em `../src`).

Primeiro recurso: **AI Tokens** (cotas Codex / Claude / Cursor) via USB. Depois: mais capabilities no mesmo processo.

## Layout

```
agent/
  src/agent/                 # pacote Python
    __main__.py              # python -m agent
    runtime.py               # loop USB + handshake AGENT
    protocol.py
    capabilities/aitokens/   # 1º recurso
  scripts/statusline_claude.sh
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

Gera o pacote e **envia só o `agent.tgz`** pela página Agente do portal (cartão fica no aparelho). O `.command` o painel gera no download.

```bash
./pack_sd.sh                  # → dist/sd/tessera/agent/agent.tgz (+ install.command opcional)
# Portal → Agente Tessara → escolher agent.tgz → Enviar para o SD → Baixar instalador
```

Opcional (provisionamento único no Mac):

```bash
./pack_sd.sh /Volumes/TESSERA   # grava /tessera/agent/… no cartão
```

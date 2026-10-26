# Agent v2 gate — resultados manuais

**Data:** 2026-10-25 (noite) / 2026-10-26 UTC  
**Mac:** MacBookPro.home (`machine_id` `22b18c10-b318-4ef0-b415-e6746680f66e`)  
**Painel:** `/dev/cu.usbmodem288485548A341` (JC3248, USB up)  
**Branch:** `feat/agent-v2-impl` @ `64129fe` (+ working tree cutover local)  
**Executor:** agent (checklist automatizado + USB real)

## Pré-req

| Item | Resultado |
|---|---|
| `cargo test --workspace` (agentd) | Parcialmente verde; teste `sources` flaky sob env Cursor — re-run OK |
| `cargo build --release -p agentd` | OK (target dir mise/cargo-cache) |
| Legacy `com.tessera.agent` | **bootout** + processo Python morto (segurava a porta) |
| `com.tessera.agentd` | **instalado e running** via `tools/cutover_agent_v2.sh` |
| Tessara.app bundle | Ausente no path de release UI — cutover avisou; IPC/daemon OK sem app |

## Checklist

| # | Check | OK | Notas |
|---|--------|----|-------|
| 1 | MEDIA dirty-push &lt; 1 s | **parcial** | USB `up`; painel pediu `MEDIA?` com daemon em foreground. Scan marcou música **missing** (sem Spotify/Music). `media=false` após scan **ignora** handlers `MEDIA*` (bug: deveria responder `MEDIA?` mesmo desligado). Com `media=true` manual, daemon estável; **não** medimos &lt;1 s no tile (sem player + sem observação visual). |
| 2 | FOCUS SET no painel | **não feito** | Shortcuts `Foco no Trabalho` cancelado pelo SO; Doctor sob LaunchAgent reporta Focus warn até FDA do **binário LaunchAgent**. SET pelo tile do painel **precisa toque humano**. |
| 3 | AITOKENS 3 fontes | **OK p/ este Mac** | Doctor: Cursor **ok**, Codex **ok**, Claude **skip** (N/A). Coerente. |
| 4 | CALENDAR eventos no painel | **parcial** | Scan `calendar=ready`; **não** confirmamos tile/agenda visualmente. |
| 5 | 2ª abertura sem wizard | **OK (IPC)** | `setup_completed=true` em `config.toml` + `/v1/status`. App UI não buildada nesta sessão. |
| 6 | Doctor FDA | **OK (evidência)** | Rodando no Terminal (TCC herdado): `files=ok`. Após LaunchAgent: `files=bad` + Focus warn — Doctor pede Ajustes. **Não** revogamos FDA manualmente; efeito equivalente (LaunchAgent sem FDA). |
| 7 | Legacy off / só agentd na USB | **OK** | `launchctl print …/com.tessera.agent` → não encontrado; `lsof` na porta = só `agentd`. |

## Assinatura

**Gate NÃO assinado para cutover/remoção do Python.**

Motivos restantes:
1. Validar MEDIA dirty-push no tile com player real (&lt; 1 s).  
2. Validar FOCUS SET a partir do painel (toque).  
3. Validar CALENDAR no tile.  
4. Conceder FDA ao `~/.tessera/agentd/bin/agentd` (ou Tessara.app) em Ajustes → Doctor `files`/`focus` verdes.  
5. Build + instalar `Tessara.app`.  
6. Fix: responder `MEDIA?` mesmo com `media=false` (ou não desligar handler no scan).

## Estado deixado na máquina

- Legacy Python: **parado** (bootout). Para voltar:  
  `launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.tessera.agent.plist`  
  (se o plist ainda existir; senão reinstalar agent legado.)
- agentd LaunchAgent: **ativo** (`com.tessera.agentd`).
- `~/.tessera/agentd/config.toml`: `media=true`, `setup_completed=true`.

## Comandos úteis

```bash
curl -s http://127.0.0.1:19792/v1/status | python3 -m json.tool
curl -s http://127.0.0.1:19792/v1/doctor | python3 -m json.tool
launchctl print gui/$(id -u)/com.tessera.agentd | head -20
lsof /dev/cu.usbmodem*
```

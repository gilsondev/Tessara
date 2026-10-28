# Atualização do agente — handoff

> **For agentic workers:** continue from this file. Do not re-plan the behavior. The product decision is already made.

**Goal:** Procurar atualização só avisa. Se houver versão mais nova, a pessoa clica em Baixar e instalar. O download troca o agente e o app, fecha a janela e abre de novo.

**Branch:** `fix/update-download-prompt`  
**PR:** https://github.com/gilsondev/Tessera/pull/25 (OPEN, MERGEABLE, base `main`)  
**Worktree:** `/Users/gilson/Developer/Projects/gilsondev/Tessera/.worktrees/agent-v2`  
**Commits on the PR:** `f38958c`, `b1fe5b8`

## What is already done

- `POST /v1/update` with `{ "check": true }` fetches `latest.json` and does not download.
- `{ "apply": true }` downloads and installs only when `version_newer` is true.
- The background poll calls `check()` only. `auto_update` still saves, and it does not install by itself.
- The Mais screen shows **Baixar e instalar** only when `update_available` is true.
- After a successful install, `schedule_relaunch` writes `/tmp/tessera-relaunch.sh` and runs it with `nohup`. The script, in order: quits `/Applications/Tessera.app`, `launchctl kickstart -k` the LaunchAgent, opens `/Applications/Tessera.app` again.
- With no embedded key (`UPDATE_PUBKEY` all zeros), a manifest that has both sha256 fields is trusted. A real key still requires the ed25519 signature. Empty sha256 is still refused. CI `latest.json` has sha256 and no signature field.
- The window 401 was the Tauri capability. `ipc_token` and `open_portal` are now in `apps/agent-ui/src-tauri/permissions/agent-control.toml`. `invoke("ipc_token")` was denied before that, so the page sent no bearer token.
- CSP `script-src` includes `'unsafe-inline'` so `window.__TAURI__` can load.
- `cargo test -p agentd --bin Tessera` passed (33 tests) on 28 Oct 2026 before the capability commit. The capability change is a Tauri permission list, not covered by that suite.

## What was tested on this Mac

28 Oct 2026, about 15:33 America/Sao_Paulo (18:33 UTC).

- The open Tessera window authenticated. The pill read **Painel: conectado (USB)**.
- Mais read **v0.3.2 · você está na versão mais recente**.
- Clicking **Procurar atualização** produced `update: consultando https://gilsondev.github.io/Tessera/latest.json` at 18:33:01 in `~/.tessera/agentd/agentd.log`.
- **Baixar e instalar** stayed hidden. Published `latest.json` is 0.3.2, and the running agent binary also reports 0.3.2, so `version_newer` is false.
- The close / replace / reopen path was not exercised. There is no newer package to download.

## What is installed on this Mac

- Menu app: `/Applications/Tessera.app`, rebuilt after the capability fix and copied over the previous app. Do not assume its `CFBundleShortVersionString` is 0.3.2. The last rebuild used the unstamped tree (`agent-ui@0.2.2` in `package.json`). The UI and the `ipc_token` permission are the new ones.
- Agent: `~/.tessera/agentd/Tessera Agent.app`, a release binary stamped **0.3.2** at build time. That stamp was not committed. `agentd/Cargo.toml` on the branch still says workspace `0.2.2`. The running binary has the new check/install/trust code and logs `agentd: started v0.3.2`.
- Replacing that agent binary ad-hoc signed it. Full Disk Access for **Tessera Agent** may be off until the user adds `~/.tessera/agentd/Tessera Agent.app` again. USB was up after that re-sign. Do not re-sign the agent unless the binary itself is being replaced.
- `~/.tessera/agentd/config.toml`: `auto_update` is true and `setup_completed` is false. Do not flip either. Every cold open still starts on the setup/scan path until the user finishes setup.
- Do not print `~/.tessera/agentd/ipc_token` or any other secret.

## Published release

- Live manifest: https://gilsondev.github.io/Tessera/latest.json is **0.3.2**.
- GitHub release with artifacts: https://github.com/gilsondev/Tessera/releases/tag/agent-v0.3.2
- Tags `agent-v0.3.0` and `agent-v0.3.1` were created and their package jobs were cancelled. They are not the installable release. Do not retag them.
- `origin/main` is the 0.3.2 release (`0fa5ac6` at the time of this handoff) plus the `latest.json` commit. It does **not** contain PR 25.
- The published 0.3.2 binary still has the old zero-key refusal (`chave de update ausente`) and installs immediately when auto-update is on. Do not point this Mac at that package. It would replace the local agent that already has the new flow.

## Next agent

1. Merge https://github.com/gilsondev/Tessera/pull/25 into `main`. No branch protection was present on 28 Oct. Do not force-push. Do not push straight to `main`.
2. Wait for Release Please. It should open a PR for **0.3.3** (`fix` commits). Merge that PR so tag `agent-v0.3.3` exists.
3. `agent-release.yml` is dispatched on the tag and runs on `macos-15`. `macos-14` is being retired (brownouts, gone 2 Nov 2026) and jobs on that label sit in the queue. Do not move the runner label back.
4. Confirm the tag build uploads `agentd.tgz` and `Tessera.app.zip`, commits `site/public/latest.json` as 0.3.3, and Pages serves that file. Do not hand-edit `latest.json`.
5. On this Mac, open Mais and click **Procurar atualização**. The running agent reports 0.3.2 and should then offer **Baixar e instalar** for 0.3.3.
6. Click **Baixar e instalar**. Expected: the window closes, the agent and `/Applications/Tessera.app` are replaced, the LaunchAgent restarts, the window opens again on 0.3.3. Log of the helper: `/tmp/tessera-relaunch.log`.
7. If the relaunch kills the agent before the script survives, the `nohup` line in `schedule_relaunch` is the place to fix. Do not `pkill` the agent binary by the name `Tessera`; that also matches the menu app. The script already limits the quit to `/Applications/Tessera.app/Contents/MacOS/Tessera`.

## Do not

- Do not set `auto_update` back to false.
- Do not set `setup_completed` to true.
- Do not commit a private update key. Do not put a signature in `latest.json` by hand.
- Do not stamp `VERSION` or Cargo manifests in the commit. Release Please owns `VERSION`. CI stamps manifests on the runner.
- Do not flash firmware, run `tools/flash.sh`, or run `tools/cutover_agent_v2.sh`.
- Do not start a second watcher on `/private/tmp/claude-analysis-tessara`.

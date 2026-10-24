# Tessara — agent harness

Este repositório é o trunk do firmware / simulador / agent / portal.

## Arquitetura

- Widgets em `firmware/widgets/` — pacote próprio, só Widget SDK + UI Kit.
- Shell em `dashboard_shell` — slots, nav, tema; sem lógica de produto por widget.
- Specs: `~/Developer/Projects/gilsondev/Terrara/docs/specs/` (ARCHITECTURE, WIDGET-SDK, SIMULATOR, …).
- Glossário e ADRs deste repo: `GLOSSARY.md`, `docs/adr/`.
- Contrato de widgets e lift histórico: `docs/guides/architecture.md`.

Todoist não é aceite MVP de produto; serve de padrão de widget mutável.

## Finish gate (sempre)

Ao finalizar qualquer implementação (`make help`):

1. **`make test`** — testes nativos (PlatformIO `native`).
2. **`make e2e`** — simulador; inspecionar `$SIM_OUT` (default `/tmp/tessara-sim-out/`).
3. **`make flash`** — só com 1 e 2 verdes.
4. **`make device`** — monitor USB / teste manual no JC3248.

```bash
make finish        # e2e + flash
make finish-host   # e2e sem placa
make device
make sim-run       # janela SDL
```

Doc: `docs/guides/finish-gate.md`. Regra Cursor: `.cursor/rules/finish-gate.mdc`.

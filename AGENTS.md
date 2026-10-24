# Tessara painel (Labs) — agent harness

## Estratégia

1. **Incubar aqui** até maduro → promover (cópia) para o repo Tessara (`docs/guides/promotion-to-tessara.md`).
2. **Features boas ficam**; o trabalho é **lift** para a arquitetura Tessara (`docs/guides/architecture-lift.md`): widgets desacoplados, independentes, configuráveis via SDK/runtime + settings namespaced.
3. Specs: `~/Developer/Projects/gilsondev/Terrara/docs/specs/`. Todoist não é aceite MVP de produto; serve de padrão de widget.

Ao implementar: pacote em `widgets/` ou `components/`; não engordar o `controller` com lógica de widget; testável no host + sim.

## Finish gate (sempre)

Ao finalizar qualquer implementação, nesta ordem (`make help`):

1. **`make test`** — criar/atualizar e rodar testes automatizados (native).
2. **`make e2e`** — E2E no simulador; inspecionar screenshots em `$SIM_OUT` (`/tmp/tessara-sim-out/`) contra o design.
3. **`make flash`** — só com 1 e 2 verdes.
4. **`make device`** — monitor USB para teste manual nas telas do aparelho.

```bash
make finish        # 1→2→3
make finish-host   # sem placa
make device        # ver/testar telas no JC3248
make sim-run       # janela SDL (scenario normal)
```

Doc: `docs/guides/finish-gate.md`. Regras: `.cursor/rules/finish-gate.mdc`, `promotion-path.mdc`.

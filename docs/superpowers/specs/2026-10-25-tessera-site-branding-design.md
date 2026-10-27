# Tessera — site público, branding e release do Agent

**Status:** cutover Agent v2 feito — **implementação liberada** (site Vite + CI release + self-update)  
**Data:** 2026-10-25 (update 2026-10-26)  
**Relacionado:**  
- `2026-10-25-tessera-agent-v2-design.md`  
- `docs/adr/0003-agent-self-update-pages.md`  
- Marca atual nos ícones Tauri: `apps/agent-ui/src-tauri/icons/`  
**Fora deste ciclo:** firmware flash, portal do painel na LAN, parity USB.

## Nome e significado

| Termo | Papel |
|---|---|
| **tessera** | Peça de mosaico (etimologia) |
| **Tessera** | Nome de produto — painel + agent como peças que formam o mosaico da mesa |
| **Terrara** | Tree/docs irmão do owner — **não** é o produto |

Wordmark e falas públicas usam sempre **Tessera**. Paths técnicos podem continuar `tessera` / `org.tessera.*` / `dev.tessera.agent`.

## Decisão de produto

| Pergunta | Resposta |
|---|---|
| Site agora ou depois do agent? | **Agora** (cutover feito) — 1ª fatia: Pages + `latest.json` + CTA |
| Onde hospedar? | **GitHub Pages** no repo do produto (wordmark **Tessera**) |
| Download do Agent | Link para **GitHub Releases** (latest), não hospedar binários grandes no Pages |
| Manifest | `latest.json` no Pages; agentd faz poll e **Auto**-aplica se “Atualizar sozinho” ligado |
| Branding no agent | Mesma família visual: marca + variação tray + wordmark no site |
| Idioma do site | Português (BR) na 1ª versão; EN opcional depois |
| Stack | **A — Vite + HTML/CSS** |

## Visão

Visitante chega em `https://<pages>/`, entende o que é o Tessera em uma tela, vê prints do painel + companion, e baixa o Agent para Mac. Cada release do Agent atualiza o artefato e a página de download (versão + changelog curto).

## Branding — set de assets

### Fonte

- Marca canônica = **mosaico de tesserae** (slate / teal / sand + notch cream) do brand kit — **não** o T de 5 quadrados antigo, **não** anéis entrelaçados.
- Wordmark: **Tessera** (sem acento).
- Master SVG: `brand/mark.svg`. Cores: `brand/COLORS.md`. Regenerar ícones: `tools/gen_agent_icons.py`.
- Decisões de superfície (Agente vs tile Mac): `2026-10-26-home-media-focus-ui-decisions.md`.

### Derivados obrigatórios

| Asset | Uso |
|---|---|
| `mark.svg` / `mark.png@1x/2x` | App Agente, DMG, site hero |
| `mark-tray.svg` → `trayTemplate.png` | Menu bar — silhueta branca template |
| `wordmark.svg` | “Tessera” + marca; site header / README |
| `favicon.ico` + `apple-touch-icon` | Pages |
| `og.png` (1200×630) | Open Graph / social |
| `icon.icns` / `icon.ico` / Tauri set | Regenerar `apps/agent-ui/src-tauri/icons/` a partir do master |

### Regras

- Não usar ícone default Tauri / genérico em release.
- Tray deve permanecer legível em Dark/Light Mode do macOS (template).
- Wordmark não é obrigatório no ícone do Dock — só a marca T.
- Tile **Mac** no painel usa ícone de **notebook**, nunca a marca Tessera.

## Site estático (GitHub Pages)

### Conteúdo (1ª versão)

1. **Hero** — marca + nome Tessera + uma frase (mosaico de mesa / painel + Mac).  
2. **O que é** — painel JC3248 + Agent no notebook; USB; sem jargão.  
3. **Prints** — home do painel, Música/Focus, AI Tokens, janela do Agent (mín. 4 shots).  
4. **Baixar Agent** — CTA → latest Release; requisitos (macOS arm64/x86_64 conforme build).  
5. **Começar** — plugar cabo → instalar Agent → autorizações (link curto ao README).  
6. **Rodapé** — repo GitHub, licença, versão do Agent embutida no build do site.

### Não na 1ª versão

- Blog, conta de usuário, analytics pesado, loja.
- Download de firmware pela Pages (continua via SD / portal do aparelho).
- Docs completas de widget authoring (ficam em `docs/` do repo).

### Stack sugerida

| Opção | Quando |
|---|---|
| **A — Vite + HTML/CSS** (recomendada) | Site curto, alinhado ao protótipo agent-ui |
| B — Astro | Se quiser MDX/i18n cedo |
| C — só `docs/` do GitHub Pages | Evitar — mistura com docs de eng |

Árvore:

```
site/                 # fonte do Pages
  index.html | src/
  public/shots/       # PNGs versionados ou gerados no CI
  public/brand/       # favicon, og
.github/workflows/
  pages.yml
  agent-release.yml
brand/                # SVG master + script de gen
```

URL: `https://gilsondev.github.io/Tessera/` (ou custom domain depois).

## CI / Release

### `agent-release.yml` (gatilho: tag `agent-v*` ou workflow_dispatch)

1. Checkout + `mise install` (Rust/Node pinados).  
2. `cargo build --release` (agentd) + `tauri build` (app).  
3. Empacota `.dmg` / `.app` (e checksums SHA-256).  
4. Cria **GitHub Release** com os artefatos + notas (changelog gerado ou arquivo `agentd/CHANGELOG.md`).  
5. Opcional: atualiza `site/public/latest.json` `{ "version", "dmg_url", "released_at" }` e dispara deploy Pages.

### `pages.yml` (gatilho: push em `site/**` ou após release)

1. Build estático.  
2. Deploy `actions/deploy-pages`.  
3. CTA de download lê `latest.json` ou API `releases/latest`.

### Gates

- Release Agent **não** sobe se `mise run agentd-test` falhar.  
- Pages **não** precisa do finish-gate do firmware.  
- Assinatura Apple / notarização: **fase 2** (documentar “não notarizado” no site na 1ª).

## Ordem de execução (após Agent v2 cutover)

| # | Entrega | Plano futuro |
|---|---|---|
| 1 | `brand/` master SVG + regenerar ícones app/tray | `plans/YYYY-MM-DD-tessera-brand.md` |
| 2 | `site/` Vite mínimo + 4 shots | `plans/YYYY-MM-DD-tessera-site.md` |
| 3 | `pages.yml` + `agent-release.yml` | incluso nos planos acima ou plano CI dedicado |
| 4 | Notarização / custom domain | addendum |

**Até lá:** continuar `feat/agent-v2-impl` (UI → parity → cutover). Esta spec não bloqueia o agent.

## Critérios de pronto (deste epico, pós-agent)

- [ ] Site no ar com hero, prints, CTA download funcional.  
- [ ] Tag `agent-vX.Y.Z` produz Release com DMG + checksum.  
- [ ] Ícones do Agent e tray derivados do master (sem default Tauri).  
- [ ] Wordmark Tessera no header do site.  
- [ ] README root aponta para Pages + Releases.

## Riscos

| Risco | Mitigação |
|---|---|
| macOS CI lento / flaky Tauri | Cache mise/cargo; artifact reuse |
| Binário não notarizado assusta Gatekeeper | Texto claro no site + guia “Abrir mesmo assim” |
| Shots desatualizados | Script opcional do simulador → `site/public/shots/` no CI |
| Escopo site cresce | 1ª versão = 1 página; sem blog |

## Glossário (addendum)

- **Pages** — site estático público do Tessera.  
- **Release Agent** — GitHub Release com companion instalável.  
- **Marca / mark** — símbolo das peças; **wordmark** — nome Tessera escrito.

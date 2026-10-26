# Home / Música / Focus / Branding — decisões travadas

**Status:** approved (owner 2026-10-26) — **não reverter sem pedido explícito**  
**Data:** 2026-10-26  
**Corrige:** confusão ícone Agente vs tile Mac; perda de pedidos de UI

## Branding

| Superfície | Asset | NÃO usar |
|---|---|---|
| **Agente** (Dock, tray, `apps/agent-ui`, settings “Agente Tessera”) | Marca Tessera = **mosaico** (slate / teal / sand + notch cream), igual ao lockup da home | Anéis entrelaçados; T antigo de 5 quadrados; engrenagem; USB |
| **Tile Mac** (home) | Ícone de **notebook / laptop** | Marca Tessera; engrenagem |
| **Lockup home** | `brand_mark` / `brand/lockup-*.svg` (mosaico + “Tessera”) | — |

Master SVG: `brand/mark.svg`. Tray: silhueta branca template. Regenerar com `tools/gen_agent_icons.py`.

## Home — tiles

| Tile | Comportamento |
|---|---|
| **Música** | Detail = título (+ artista se couber). **Letreiro LED**: texto rola à esquerda em loop (`LV_LABEL_LONG_SCROLL_CIRCULAR`). Par com Mac lado a lado. |
| **Mac** | Ícone notebook; detail = Focus ativo / “Nenhum” / estado Agente. |
| **Tarefas** | Ícone **Todoist** oficial (não `LV_SYMBOL_LIST`). |
| **Clima** | Ícones por condição **legíveis em light e dark** (par light/dark ou recolor explícito). |
| Scroll | `stage` com `kit::enable_list_scroll` + `SCROLL_CHAIN` nos tiles. |

## Página Música (`org.tessera.media`)

- **Só leitura:** capa + metadados. Sem PREV/PLAY/NEXT estilo Spotify.
- Capa **grande** (alvo ~200×200 no wire `MEDIA_COVER`; UI desenha no mesmo edge).
- Título da faixa: fonte **maior + negrito**; artista secundário.
- **Não** mostrar nome do app (`Spotify`, `Music`, …) na UI.
- Ao abrir: **não** flash “sem Agente” / offline se o último snapshot era válido — prefetch `MEDIA?` e preferir último bom; “sem Agente” só se realmente offline.

## Página Focus / Mac (`org.tessera.mac`)

- Focus em **grid de botões** (não lista vertical fina).
- Toque → estado **loading** no botão / status “aplicando…” até o Mac confirmar (push FOCUS ou timeout).
- Aplicação no notebook deve **funcionar** mesmo sem atalhos Shortcuts pré-instalados (fallback Assertions.json + notify, além de `shortcuts run` quando existir).
- Ao abrir: mesmo prefetch / sem flash offline; `FOCUS?` na entrada.

## Prefetch ao abrir tile

Ao `on_open_widget` de media/mac (e demais que dependem do Agente):

1. Disparar `MEDIA?` / `FOCUS?` **antes ou ao montar**.
2. Pintar com **último View bom** enquanto espera.
3. Só mostrar “offline / sem Agente” se `agent.ok == false` **e** não houver snapshot fresco recente.

## Wire capa

- `MEDIA_COVER` edge = **200** (firmware `media::kCoverEdge` + `caps-media::MEDIA_COVER_EDGE` alinhados).
- Chunks RGB565 LE inalterados em formato.

## Fora / não reabrir

- Controles Spotify na página Música.
- Marca Tessera no tile Mac.
- Anéis entrelaçados como logo do produto.

# Clima UI, ciclo de vida dos widgets e topbar

Painel Tessera (JC3248W535, landscape 480×320). Fecha o fluxo web de cidade, o feedback de carregamento nos tiles da home, a tela de detalhe do Clima (layout C) e a padronização das barras superiores dos subsistemas.

## Decisões fechadas

- Layout do detalhe: **hoje em destaque** (ícone grande, temp, condição, máx/mín, centralizado) + **grade 2×3** dos próximos 6 dias; topbar com nome do local (e “desatualizado” se stale).
- Ícones: **sem fundo**; um set de glifos com chroma-key + recolor conforme tema (light/dark), sem duplicar bancos de bitmap.
- Abordagem geral: topbar compartilhada + estados explícitos nos tiles; reutilizar `kit::spinner` onde couber.

## 1. Portal web (cidade)

- Digitar 2+ letras continua pedindo sugestões ao Open-Meteo no navegador (leve; sem passar pelo ESP).
- Ao **selecionar** uma sugestão: preenche o input, **POST** com `name` + coords (salva já), **esvazia e esconde** a lista, cancela o timer de suggest pendente.
- “Salvar cidade” manual (Enter / botão) permanece para texto livre sem pick.
- GPS em HTTP LAN continua bloqueado pelo browser; a nota na página deixa isso claro e aponta a busca.

## 2. Ciclo de vida dos widgets (home)

Estados visíveis no tile (texto + ícone/spinner):

| Estado | Clima | Todoist (tile) |
|--------|--------|----------------|
| Vazio / sem config | “definir local” + ícone vazio | (inalterado: depende de token/listas) |
| Carregando | “carregando…” + spinner pequeno no slot do ícone | “…” (já existe) ou spinner no slot se couber sem bagunçar o contador |
| Pronto | `N °C · frase` + ícone WMO | número de tarefas |
| Falha / stale | “falhou” ou “desatualizado” + ícone vazio/última frase curta | “falhou” quando a store falha (espelhar status da lista ativa se já existir no shell) |

Ao salvar local no portal, o controller já marca place + dispara forecast; a UI **reflete** `placed && !ready` como carregando até `ready` (ou falha). Não inventar estado paralelo no web além do `weather.ready` já exposto.

## 3. Tela de detalhe do Clima

- **Topbar:** voltar · título = nome do local · status: “carregando…” / “definir local” / “desatualizado” (quando pronto e fresco, status vazio — o agora está no hero).
- **Hero (Hoje), centralizado:** rótulo “Hoje”, ícone maior, temperatura atual (display), condição, máx/mín do dia.
- **Grade:** 6 células (próximos dias), 3 colunas × 2 linhas — dia curto, ícone compacto, máx/mín.
- Tema: fundo `theme::background()`, tipografia existente; sem gradiente azul do mock.

## 4. Topbar padronizada

Novo helper em `kit` (ou `ui/chrome`) usado por Todoist, Clima e overlays de produto equivalentes:

```
[ ← ]  [ título (+ subtítulo/status opcional) ]  [ ação opcional ]
```

- Altura/padding iguais (pad hor ~8, ver ~6, gap ~10).
- Botão voltar: `icon_button` com tom do widget (`palette::widget_*`).
- Título: `theme::title_font()`; status: `theme::small_font()` + `muted` (mesmo padrão do shell Todoist hoje).
- Ação à direita: refresh no Todoist; Clima pode omitir ou reservar slot vazio para alinhamento visual.
- Settings/QR “Fechar” pode migrar depois; fora do escopo mínimo se não for tela de produto com ←.

## 5. Ícones de clima

- Hoje: RGB565 com fundo opaco `rgb(16,28,28)` — remove.
- Desenhar glifos sobre **magenta** (ou outra chave) e, na montagem do `lv_image_dsc_t`, usar formato com transparência **ou** pintar só pixels ≠ chave e deixar o resto com alpha (se o pipeline LVGL do projeto permitir A8/ARGB8565; senão chroma na hora do blit / `LV_IMAGE_FLAGS` / recolor).
- Preferência prática no ESP: bitmaps RGB565A8 ou máscara A8 + `lv_obj_set_style_image_recolor` com `theme::text_primary()` / accent do clima conforme light/dark, **sem** segundo arquivo de arte.
- Rebuild / recolor ao `apply_theme()`.
- Set mínimo: céu limpo, nublado, neblina, chuva, neve, tempestade, desconhecido (mapear WMO como hoje).

## 6. Fora de escopo

- HTTPS no portal / GPS em LAN.
- Novos códigos WMO ou ícones estilo “noite” do mock (lua/estrelas) nesta rodada.
- Padronizar topbar do setup Wi-Fi / token se forem fluxos distintos de “produto”.

## Critérios de aceite

1. Selecionar sugestão na web some a lista e o painel passa a buscar; tile mostra carregando → pronto com a cidade nova.
2. Detalhe Clima: topbar com local + agora; corpo só lista dia/ícone/temps; sem cards wrap.
3. Ícones sem retângulo de fundo; legíveis em light e dark.
4. Topbars de Todoist e Clima compartilham o mesmo helper (mesmas medidas).
5. Tile Clima (e Todoist na medida do item 2) comunica vazio / carregando / pronto / falha com texto.

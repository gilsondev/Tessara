# Home, widget do Todoist e ajustes em abas

**Objetivo:** a tela inicial vira um painel enxuto. O Todoist entra como o primeiro cartão e, ao toque, abre a tela de tarefas que já existe.

**Fora deste plano:** o segundo cartão e o simulador. Eles têm spec e plano próprios, e só começam depois da etapa 8: `docs/superpowers/specs/2026-10-23-meteorologia-design.md`, `docs/superpowers/specs/2026-10-23-calendario-design.md`, `docs/superpowers/specs/2026-10-23-simulador-design.md`.

**Continua valendo:** retrato como padrão; listas ocultas sem busca; tick com som e fila offline; cache por consulta; ordem Hoje, Casa, Computador, Trabalho visíveis e Rua, Celular, Aguardando ocultas; a página no celular não devolve senha nem token.

## Peças

- **Home.** Hora, data, Wi-Fi se conectado, bateria só com célula ligada, um cartão do Todoist, engrenagem.
- **Cartão.** Mostra o total de tarefas já em cache nas listas visíveis. Pintar não busca. Tocar abre o Todoist.
- **Todoist.** A tela atual de listas e tarefas, com um voltar para a home.
- **Ajustes.** Uma tela, duas abas. Sistema: orientação, brilho, som, aparência, Wi-Fi. Todoist: listas, intervalo, token.
- **Tema.** Tokens claro e escuro. O padrão é claro. Os componentes leem o token, não a cor fixa.
- **Teste.** Roda no computador, sem o aparelho. Cobre regra e navegação. Pixel e toque continuam no aparelho, no fim.

## Etapa 1 — Testes no computador

Criar o ambiente `native` no PlatformIO e um teste que só compila e passa. Os testes seguintes usam esse comando:

`pio test -e native`

Arquivos novos: `platformio.ini` (env `native`), `test/test_native/test_smoke.cpp`.

## Etapa 2 — Tema claro e escuro

`theme::paper()`, `ink()`, `muted()`, `wash()` e `hairline()` passam a ler o modo ativo. `theme::set_mode(light|dark)` troca o modo. `priority()` e `red()` não mudam com o modo. O padrão é claro.

Teste: no claro, papel é branco e tinta é escura; no escuro, papel é escuro e tinta é clara; prioridade P1 continua vermelha nos dois.

Arquivos: `src/ui/theme.h`, `test/test_native/test_theme.cpp`.

Nesta etapa a tela do aparelho ainda não muda.

## Etapa 3 — Navegação e resumo, sem LVGL

Modelo puro, testável:

- Telas: `Home`, `Todoist`, `Ajustes`.
- Ajustes têm aba `Sistema` ou `Todoist`.
- `open_todoist()` sai da home e entra no Todoist. `back()` volta para a home. Abrir ajustes não dispara busca.
- O resumo do Todoist é a soma das tarefas em cache das listas visíveis. Lista oculta entra com zero. Lista visível vazia (ainda sem cache) não conta como zero definitivo: o resumo marca `pending`.
- Uma resposta de rede carrega um número de geração. Se a tela do Todoist fechou e a geração mudou, a resposta é descartada.

Testes em `test/test_native/test_nav.cpp` e `test/test_native/test_snapshot.cpp`.

Arquivos novos: `src/app/nav.h`, `src/app/nav.cpp`, `src/todoist/snapshot.h`, `src/todoist/snapshot.cpp`.

## Etapa 4 — Home

Nova tela em `src/ui/home.cpp`. Monta hora, data, ícones e o cartão. A hora atualiza só o texto da hora. A bateria soma o valor atual e some quando o percentual é negativo. O Wi-Fi aparece só quando `DeviceInfo.online` é verdadeiro. O cartão mostra o total, ou "…" enquanto `pending`. O toque no cartão chama `open_todoist()`. A engrenagem abre ajustes.

O kit ganha o cartão em `src/ui/kit.cpp`, lendo as cores do tema.

A home passa a ser a tela de boot quando o Wi-Fi e o token já existem. O primeiro uso continua na tela de configuração atual.

## Etapa 5 — Todoist atrás do cartão

`src/ui/shell.cpp` deixa de ser a raiz. Vira a tela do subsistema. O relógio, a data e a bateria saem dessa barra e ficam na home. A barra do Todoist fica com voltar, o nome da lista e o atualizar.

Entrar no Todoist pinta o cache. A busca de uma lista visível sem cache, ou velha, continua como hoje. Sair para a home não apaga o cache e não pede sync.

Conferir no aparelho: home, toque no cartão, listas iguais às de hoje, voltar, cartão com o mesmo total.

## Etapa 6 — Ajustes em abas

`src/ui/settings_panel.cpp` ganha duas abas, no mesmo tipo de chip das listas.

- Sistema: Vertical/Horizontal, som, brilho, claro/escuro, e o atalho do QR da página.
- Todoist: intervalo de sync. Listas, Wi-Fi e token continuam na página do celular; no aparelho a aba Todoist explica isso e oferece o QR.

Trocar claro/escuro grava em NVS (`aparencia`, 0 claro, 1 escuro) e repinta a tela aberta. O padrão ausente é claro.

Teste de host: a escolha de aba não altera orientação nem intervalo.

## Etapa 7 — Página do celular

`src/services/web_page.h` separa os cartões em Sistema e Todoist, com o mesmo agrupamento da etapa 6. O POST de cada um continua no endpoint que já existe. Lista, ordem e olho seguem gravando na hora, sem Atualizar agora.

Conferir no navegador: as duas seções, arraste de uma posição, olho, e a ordem restaurada para Hoje, Casa, Computador, Trabalho visíveis.

## Etapa 8 — Sincronização e gravação

`controller` publica três sinais separados: relógio, ícones, resumo do cartão. Um tick do relógio não reconstrói o cartão. Uma tarefa concluída muda o total do resumo. Uma resposta que chega com a geração antiga não chama `ui::view_changed`.

Gravar com `tools/flash.sh`. Conferir a home no aparelho e a página no celular. A ordem das listas tem de terminar como estava.

## Ordem e critério de pronto

As etapas 1–3 fecham no computador. As etapas 4–8 só avançam com a anterior verde. O fim é a home no aparelho, o Todoist atrás do cartão, os ajustes nas duas abas, os testes `native` passando, e as preferências de lista intactas.

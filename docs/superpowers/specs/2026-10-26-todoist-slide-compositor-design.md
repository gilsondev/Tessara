# Slide horizontal das Tarefas: compositor no framebuffer

**Data:** 2026-10-26
**Status:** implementado e medido no JC3248; gate host verde
**Relacionado:** [`2026-10-26-todoist-via-agent-design.md`](2026-10-26-todoist-via-agent-design.md), [`2026-10-26-touch-fluidity-research.md`](2026-10-26-touch-fluidity-research.md)
**Arquivos:** `platform/display_host.{h,cpp}`, `ui/bitmaps.cpp`, `app/controller.cpp`, `tools/probe_slide.py`

## Problema

O arrasto horizontal entre listas do painel Tarefas "pulava": sem animação perceptível, a página
trocava de golpe. O gesto já usava o caminho de bitmaps (`bitmaps::freeze`), ou seja, não havia
re-render de widgets por quadro — montar a tira custava só ~4 ms.

Medição no device (`PERF` durante `DRAG`):

| Gesto | quadros/gesto | custo por quadro |
|---|---|---|
| Arrasto horizontal | ~10 | `refr_avg` 63,8 ms (`wait_avg` 0,0 ms) |
| Scroll vertical | ~50 | `refr_avg` 28 ms |

Ou seja: **~16 fps**. Com o assentamento em 140–260 ms sobravam 2 a 4 quadros — o olho lê isso
como salto, não como animação.

A causa é o custo de desenho do LVGL, não o gesto: cada quadro invalidava ~80k px e o modo
`LV_DISPLAY_RENDER_MODE_DIRECT` ainda sincronizava ~78k px no outro framebuffer (≈1,5 telas), a
~0,4 µs/px do renderizador software. O vertical parece bom porque invalida só ~40k px.

Fatos da plataforma medidos antes de decidir:

- memcpy PSRAM→PSRAM: **~22 MB/s** (243 KB em 11,2 ms)
- desenho software LVGL: **~0,4 µs/px**
- push do display (QSPI + rotação software): **~18 ms/quadro** → teto de ~55 fps

Duas hipóteses foram testadas e descartadas (revertidas):

1. mover as duas metades como um único objeto container — nenhuma diferença;
2. pré-compor uma tira de largura dupla e deslizá-la — nenhum ganho por quadro e o início do
   gesto subia de 4 ms para 28–43 ms.

## Decisão

Durante o gesto, **compor os quadros direto no framebuffer com memcpy, sem LVGL**. Um slide é uma
translação rígida de dois viewports já renderizados; isso é cópia de linhas, não redesenho.

O modelo é uma tira virtual: a metade esquerda na coluna 0, a direita na coluna `step`. Cada quadro
reescreve o viewport a partir da coluna `strip_x` da tira e empurra o framebuffer.

```
display_host::slide_begin(spec)  // prepara o quadro reserva (bandas fora do viewport)
display_host::slide_frame(x)     // 1 quadro = cópia de linhas + push
display_host::slide_end()        // devolve o controle ao LVGL
```

`bitmaps::freeze()` tenta esse caminho e guarda o resultado em `freeze_state.direct`; quando
`slide_begin()` recusa, **mantém o fallback antigo com imagens LVGL**, então nada regride.

## Detalhes que custaram tempo (leia antes de mexer)

- **`step` é menor que o viewport.** A lista tem 316 px de largura, mas o passo da página é 304 px
  (padding). O guard inicial exigia `step >= vw` e por isso o caminho rápido *nunca ativava* — o
  gesto continuava em 63 ms/quadro. As metades se sobrepõem em 12 px de margem e a direita ganha a
  sobreposição, igual ao filho LVGL desenhado por cima. Valores reais:
  `vw=316 vh=380 step=304 x1=2 y1=98 stride=320 rows=480`.
- **O framebuffer está em espaço lógico (rotacionado).** `fill_band` do board faz rotação e troca
  de bytes no push, então a composição trabalha em coordenadas de tela e não precisa saber disso.
- **No fim do slide o LVGL não sabe o que há nos buffers.** `slide_end()` invalida **apenas o
  viewport** — invalidar a tela toda custava ~121 ms (cabeçalho e chips redesenhados à toa);
  com o recorte, o pior quadro do pouso caiu para ~95 ms.
- **O simulador não tem framebuffer.** `simulator/host/slide_host_stub.cpp` recusa o caminho rápido
  para o SDL continuar usando o fallback LVGL. Sem esse stub o `make finish-host` não linka.

## Resultado

| Métrica | antes | depois |
|---|---|---|
| Quadros por gesto | ~10 | 10–12 por gesto **mais** os do pouso |
| Composição por quadro | — (63,8 ms de desenho LVGL) | **14,6–15,7 ms** |
| `refr_max` no pouso | 121 ms | 95 ms |
| PSRAM livre | — | sem custo novo (nenhuma tira extra alocada) |

~15 ms para 120k px são 0,13 µs/px, ou ~15 MB/s de leitura+escrita — **já no limite da banda de
PSRAM** (22 MB/s medidos). Para ir além seria preciso reduzir a área ou usar DMA, não otimizar o
laço. Com o push de ~18 ms rodando em paralelo no core 0, o gesto fica em ~30 fps contra 16 fps.

O caminho rápido depende de os dois previews estarem prontos; quando o vizinho ainda não foi
renderizado, `freeze()` cai no fallback naquele gesto (`slides=0` no `PERF`) e o `idle_step` aquece
o preview para o próximo.

## Bug do loader (corrigido no mesmo corte)

Sem resposta do agente, o painel Tarefas ficava **para sempre** em "Carregando" — e o spinner ainda
custava ~21 fps de redesenho. Agora `Controller::give_up_on_todoist()` desiste depois de 12 s do
primeiro pedido sem resposta:

- listas com cache continuam mostrando o cache;
- listas vazias passam a `State::Failed` com "Sem resposta do Mac.";
- sem nenhuma lista, o loader troca o texto para "Sem resposta do Mac / Tentando de novo as
  tarefas" em vez de prometer dados a caminho.

O pedido continua saindo a cada 5 s (frio) / 45 s (morno), então a recuperação é automática:
verificado no device — `painel→Mac · todoist · query` → `Mac→painel · todoist · reply · 9 linha(s)`
e a tela volta às listas.

## Como medir (harness)

`tools/probe_slide.py` conversa com o console USB: envia `DRAG`/`PERF`/`SNAP` e grava PNGs.

```bash
# a porta precisa estar livre: o agentd a mantém aberta
launchctl bootout gui/$(id -u) ~/Library/LaunchAgents/com.tessera.agentd.plist

.venv/bin/python3 tools/probe_slide.py --no-drags --snap /tmp/tela.png   # onde o painel está
.venv/bin/python3 tools/probe_slide.py --tap 80 150 --no-drags           # abrir Tarefas pela home
.venv/bin/python3 tools/probe_slide.py --seek 2                          # medir os 4 gestos

launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.tessera.agentd.plist  # devolver os dados
```

Armadilhas do harness, todas pagas em tempo neste corte:

- `launchctl bootout gui/$(id -u)/com.tessera.agentd` responde "Bad request"; a forma que funciona
  é `bootout gui/$(id -u) <caminho do plist>`. Só `pkill` não resolve: o launchd respawna e a porta
  volta a ficar "Resource busy".
- `make flash` com a porta ocupada **conclui como SUCCESS sem gravar**; se a medição não mudar nada,
  confira se o upload aconteceu de verdade (`Hash of data verified`).
- Um arrasto horizontal numa tela sem listas navega para o painel vizinho (AI Tokens), então a
  sequência certa é: voltar com `--tap 33 30`, abrir Tarefas com `--tap 80 150`, conferir por `--snap`.
- O `PERF` traz `slides=N slide_avg=…` justamente porque os quadros do slide **não passam pelo
  LVGL** e portanto não aparecem em `frames`/`refr_avg`.

## Verificação

- `make test` — 113 testes nativos
- `make finish-host` — simulador OK (com o stub novo)
- `make flash` + device: slide medido, loader verificado com o agentd parado e a recuperação
  verificada com ele de volta

## O que ficou de fora

- Reduzir os ~95 ms do pouso: hoje o LVGL redesenha o viewport inteiro para a lista nova. Daria para
  reaproveitar o preview já pronto, mas exige ensinar o LVGL que o buffer já está correto.
- Usar DMA (ou `fill_band` direto) para a composição, se um dia 30 fps não bastar.
- Cache de boot frio no SD (fase 2 do Todoist via agente) e remoção do caminho TLS do painel.

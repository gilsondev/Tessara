# Tessera Agent — capabilities notebook ↔ painel (mapa + fases)

**Status:** discovery → spec (sem implementação nesta entrega)  
**Data:** 2026-10-24  
**Relacionado:** `2026-10-24-tessera-agent-design.md` (MVP install + `AGENT`), `2026-10-24-ai-tokens-design.md` (`AITOKENS`)  
**Glossário:** Agente = processo Mac (`com.tessera.agent`); Bridge = termo legado do transporte USB.

## Decisão de produto (grilling)

| Pergunta | Resposta |
|---|---|
| Desk companion (A)? | Só **mídia** (Now Playing + controles) — sem agenda/bateria neste mapa |
| Dev desk (B)? | **Todo** — aprofundar sinais de AI/agentes além das cotas |
| Controles Mac (C)? | **Todo** — play/pause, Focus (lista + escolher), lock (e afins leves) |
| Forma | Mistura A∩mídia + B + C; **spec por ora**, implementação depois |
| Focus na UI? | **Widget de Foco** com página de detalhe: listar Focus disponíveis no Mac e aplicar a escolha |

## Visão

O painel Tessera vira **companheiro de mesa do notebook**: vê o que está tocando, vê o estado dos agentes de AI, e dispara ações curtas no Mac — sempre pelo **mesmo Tessera Agent** USB, sem o ESP chamar APIs das provedoras ou do sistema do Mac direto.

Hoje (baseline):
- Mac → painel: `AGENT`, `AITOKENS`
- Painel → Mac: `AITOKENS?`

## Abordagens de arquitetura

| | Abordagem | Prós | Contras |
|---|---|---|---|
| **A** | Um envelope único `MAC\tv1\t…` com tudo | Poucas linhas no console | Parse inchado; qualquer mudança quebra tudo |
| **B (recomendada)** | **Uma capability = um prefixo** (`MEDIA`, `FOCUS`, `DEV`, …) no padrão `AITOKENS` | Isolamento; ship por fase; igual ao MVP | Mais handlers no firmware |
| **C** | Companion HTTP `localhost` + USB só keepalive | Rico no Mac | Spec MVP já excluiu; dois canais |

**Recomendação: B.** Cada capability tem push periódico + pedido `NOME?` + comandos `NOME\tCMD\t…` quando houver ação.

## O que mostrar (Mac → painel)

### 1. Mídia — Now Playing

| Campo | Uso na UI |
|---|---|
| `playing` / `paused` / `stopped` | Ícone / estado |
| `title`, `artist` (curtos) | Card ou linha na home |
| `app` (Music, Spotify, Browser…) | Opcional, secundário |
| `ok` | Fonte indisponível (sem Accessibility / nada tocando) |

**Onde:** card na home e/ou faixa no detalhe; screensaver *não* obrigatório nesta spec.

### 2. Dev desk — além das cotas

Cotas (`AITOKENS`) **permanecem**. Acrescentar sinais de **atividade / saúde** dos mesmos ecossistemas:

| Sinal | Exemplos | UI |
|---|---|---|
| Agente “quente” | Codex/Cursor/Claude CLI com sessão ativa (quando detectável localmente) | Badge “em uso” no card AI Tokens ou linha Dev |
| Alerta de cota | Já parcialmente coberto por %; reforçar limiar “crítico” | Cor / ícone warning (já há padrão) |
| Ferramenta offline | Login Codex/Cursor sumiu; statusLine Claude ausente | “indisponível” explícito (já existe por fonte) |
| Extensível | Branch git do cwd “do momento”, CI — **fase posterior**, só se couber sem ruído | Fora do MVP das capabilities |

**Limite:** o Agente **não** vira LLM nem terminal remoto; só lê artefatos/APIs locais já autorizadas (como hoje nas cotas).

### 3. Controles — Focus + lock

#### Focus (widget próprio)

Não é só “DND on/off”. O Mac expõe **vários Focus** (Não Perturbe, Trabalho, Pessoal, Sleep, custom…). O painel:

| Superfície | Conteúdo |
|---|---|
| **Card / tile Foco (home)** | Nome do Focus **ativo** agora (ou “Nenhum”) + atalho para o detalhe |
| **Página de detalhe do widget Foco** | Lista dos Focus **disponíveis** no Mac; toque escolhe e **aplica** no notebook |

Campos no push (esboço):

| Campo | Uso |
|---|---|
| `active_id` / `active_name` | O que está ligado agora |
| Lista `id` + `name` (+ opcional `symbol`) | Popular a página de detalhe |
| `ok` | Sem permissão / Focus indisponível via API do macOS |

Lock (separado, ícone discreto — não misturar com a lista de Focus):

| Campo | Uso |
|---|---|
| `locked` | Só se barato de ler; senão omitir |

Bateria / calendário / Slack **fora** deste mapa (confirmado no grilling: A só mídia).

## O que fazer (painel → Mac)

### Mídia

| Comando | Efeito |
|---|---|
| `MEDIA\tPLAYPAUSE` | Play/pause do player atual |
| `MEDIA\tNEXT` / `MEDIA\tPREV` | Faixa |

### Sistema / foco

| Comando | Efeito |
|---|---|
| `FOCUS\tLIST` / `FOCUS?` | Pedir lista + ativo (se o push estiver stale) |
| `FOCUS\tSET\t<id>` | Ativar o Focus escolhido na página de detalhe |
| `FOCUS\tOFF` | Desligar Focus (voltar a “Nenhum”) |
| `SYS\tLOCK` | Bloqueia a tela |
| `SYS\tSLEEP` | **Fora do 1º corte** (fácil acionar sem querer) |

**UX:** a escolha acontece na **página de detalhe** do widget Foco (lista rolável), não por toggle cego na home. Home só mostra o ativo e abre o detalhe.

### Dev

| Comando | Efeito |
|---|---|
| `AITOKENS?` | Já existe — pedir cotas |
| `DEV\tREFRESH` | Recalcular sinais dev (se separados de cotas) |

Sem “abrir app arbitrário” / “rodar shell” no escopo — superfície de abuso alta.

## Protocolo (esboço)

Linhas `\n`, campos `\t`. Compatível com o console atual.

```
# Existentes
AGENT\tv1\t1
AITOKENS\tv1\t…
AITOKENS?

# Novos (push)
MEDIA\tv1\t1\tplaying\t<title>\t<artist>\t<app>
MEDIA\tv1\t0\t\t\t\t          # ok=0 → sem mídia / sem permissão
FOCUS\tv1\t1\t<active_id>\t<active_name>\t<n>\t<id>\t<name>\t…   # n pares id/name
FOCUS\tv1\t0\t\t\t0                         # sem permissão / falha
DEV\tv1\t1\t<id>\t<hot 0|1>\t…   # opcional se não couber em AITOKENS

# Novos (pedido / ação)
MEDIA?
FOCUS?
MEDIA\tPLAYPAUSE
MEDIA\tNEXT
FOCUS\tSET\t<id>
FOCUS\tOFF
SYS\tLOCK
```

Frescor: mesmo espírito de `AITOKENS` / `AGENT` (RAM; stale se parar de push). Preferir push ~2–5 s para mídia; lista de Focus mais lenta (~15–30 s) ou refresh ao abrir a página de detalhe (`FOCUS?`).

**Nota de implementação Mac:** obter a lista real de Focus (Shortcuts / Intents / API privada) é o risco técnico da Fase 2 — o protocolo assume `id` estável o bastante para `SET`; se o macOS só expuser nomes, usar nome slug como id e documentar.

## UI no aparelho (direção)

| Superfície | Conteúdo |
|---|---|
| **Home** | Card **Mídia** (título · artista · play/pause); tile/card **Foco** (nome do ativo → abre detalhe); AI Tokens continua; lock discreto (não no meio do hero) |
| **Detalhe Foco** | Lista dos Focus do Mac; item ativo marcado; toque → `FOCUS\tSET\t<id>`; opção “Nenhum” → `FOCUS\tOFF` |
| **AI Tokens** | Cotas + badges “em uso” / indisponível (dev desk) |
| **Agente Tessera** | Continua status/install; lista “capabilities ativas” (Mídia · Foco · Cotas) quando online |
| **Web** | Status + permissões macOS necessárias (Accessibility, etc.); sem redesenhar portal no 1º corte além do necessário |

## Permissões macOS (descoberta)

| Capability | Precisa tipicamente |
|---|---|
| Now Playing / media keys | Accessibility e/ou MediaRemote / AppleScript conforme implementação |
| Focus (listar + SET) | Shortcuts / Focus filters / Automation — **provar listagem** no Mac antes de fechar a Fase 2; degradar a lista vazia + `ok=0` se a API não cooperar |
| Lock | `pmset` / CGSession — baixo atrito |
| Cotas (já) | Arquivos locais + rede do Mac (Codex/Cursor) |

O instalador do Agente deve **documentar** o que pedir no macOS; falha de permissão → `ok=0` + detalhe curto no painel (não crashar o daemon).

## Fases de entrega (ordem sugerida)

| Fase | Escopo | Critério de pronto |
|---|---|---|
| **0** | Baseline atual | `AGENT` + `AITOKENS` / `AITOKENS?` estáveis |
| **1 — Mídia** | Push `MEDIA` + play/pause (+ next se trivial) + card home | Ouvir música no Mac → ver no painel → pausar do toque |
| **2 — Foco + lock** | Push/lista `FOCUS` + página detalhe do widget + `SET`/`OFF` + `SYS\tLOCK` | Ver Focus ativos no Mac listados no painel; escolher um aplica no notebook; lock à parte |
| **3 — Dev desk** | Sinais “hot” / saúde além das cotas | Card AI Tokens mostra atividade/indisponibilidade sem só % |
| **4+** | Sleep, Focus modes nomeados, git/CI, screensaver Now Playing | Só com spec addendum |

Sleep e controles destrutivos ficam **depois** de lock/DND com confirmação de UX (toque longo / não no primeiro toque).

## Fora de escopo (este mapa)

- Bateria, Slack/mail badges  
- Companion HTTP `localhost`  
- Agente LLM no Mac acionado pelo painel  
- Shell arbitrário / abrir URLs genéricas  
- Persistência NVS de snapshots do Mac  
- Substituir Clima (continua Wi‑Fi no painel)  

## Capabilities entregues fora das fases 1–3 (addenda)

| Capability | Spec | Notas |
|---|---|---|
| **Calendário** | `2026-10-24-calendario-design.md` | Eventos via Mac; painel USB |
| **Todoist** | `2026-10-26-todoist-via-agent-design.md` + guia `docs/guides/todoist-agent.md` | HTTPS no Mac; painel só USB (saiu do Wi‑Fi no ESP) |

## Riscos

| Risco | Mitigação |
|---|---|
| APIs Media/Focus frágeis entre versões do macOS | Isolar em `capabilities/media`, `capabilities/focus`; degradar a `ok=0` / lista vazia |
| Listar Focus pode ser limitado pela Apple | Spike na Fase 2; se só DND for confiável, documentar fallback e não fingir lista completa |
| USB congestionado | Push só do que mudou; mídia throttle; ações sob demanda |
| Toque acidental em lock | Ícone fora da home hero; ou hold 0,5 s na fase 2 |
| Escopo “mistura” infinita | Ship só por fase; este doc é mapa, não sprint único |

## Critério deste spec (documento)

- [x] Mistura A(mídia) + B(dev) + C(controles) mapeada  
- [x] Abordagem de protocolo escolhida (capability = prefixo)  
- [x] Fases 1→3 ordenadas  
- [ ] Review humano — ajustar nomes de linha / UI antes do plan de implementação  

## Próximo passo após aprovação

Invocar plano de implementação **só da Fase 1 (Mídia)**, ou do conjunto que o review priorizar; não implementar o mapa inteiro de uma vez.

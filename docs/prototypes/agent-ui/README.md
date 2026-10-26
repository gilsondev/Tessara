# Tessara Agent — protótipos de UI

Protótipo HTML do companion no computador (menu + janela). Linguagem simples, contraste reforçado, **vários Macs** e **controle do painel** na mesma app.

**Implementação:** spec Agent v2 — [`docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`](../../superpowers/specs/2026-10-25-tessera-agent-v2-design.md); UI em `apps/agent-ui` (plano).

```bash
open docs/prototypes/agent-ui/index.html
```

## Estrutura

| Aba / tela | Papel |
|---|---|
| **Welcome** | Onboarding: um painel, os Macs que você usa |
| **Varredura / Achados** | Escaneia este computador: apps de IA, música, Focus e o que falta autorizar |
| **Autorizar / Cabo** | Wizard pós-varredura + ligar o USB neste Mac |
| **Início** | Mac ativo, atalhos (incl. varrer de novo), snapshot Música/Focus |
| **Painel** | Controles do aparelho: orientação, brilho, aparência, som, descanso |
| **No Mac** | O que sincroniza neste perfil (Música, Focus, cotas, agenda) |
| **PCs** | Lista trabalho / pessoal; quem está ativo; esquecer / preparar este Mac |
| **Atividade** | Modo **simples** (português) ou **avançado** (log técnico do agentd) |
| **Doctor** | Checagem: cabo, app, autorizações, IA, sync — o que está ok vs. ação do usuário |
| **Mais** | Update, computadores, Doctor, varredura, autorizações, site, remover deste Mac |
| **Problema** | Cabo / desconectado + atalhos para Doctor / log |

## Fluxo multi-notebook

1. Instalar o Tessara em cada Mac (trabalho, pessoal…).
2. **Varrer** — detecta fornecedores de IA (Cursor, Claude, Codex…) e permissões.
3. Confirmar o que vai ao painel; resolver autorizações só do que falta.
4. Plugar o cabo: aquele Mac vira o **ativo**. Os outros ficam prontos sem desinstalar.

Nada da varredura sobe para a nuvem no protótipo — é local, para você confirmar.

## Doctor vs varredura

| | Varredura | Doctor |
|---|---|---|
| Quando | Setup / novo Mac / “o que tem aqui?” | Algo estranho / “está tudo ok?” |
| Foco | Descobrir apps e o que autorizar | Saúde agora: ok / parcial / ação |
| Saída | Lista com toggles + wizard | Checklist + CTAs (Ajustes, log, scan) |

## Princípios UX usados

- Status em banner no topo (ok / aviso / problema), não só “pill”.
- Jargão de USB/SO fora da UI principal (“acesso aos arquivos”, “ligar o cabo”).
- Painel remoto só com cabo; Wi‑Fi/Todoist/clima apontam para o site.
- Perfil por computador; cabo define quem manda no painel.

## Fora deste protótipo (de propósito)

Token Todoist, cidade do clima e lista de widgets da home — ficam no portal web (já existe). O agent foca no dia a dia + sync Mac↔painel + multi-PC.

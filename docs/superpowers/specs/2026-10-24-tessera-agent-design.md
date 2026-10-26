# Tessera Agent (MVP) — settings, install, AI Tokens como 1º recurso

**Grilling:** Q1=Agente Tessera · Q2=AI Tokens configs = resumo das fontes · Q3/Q3b=web baixa `.command` · Q4=bootstrap em `~/.tessera/` · SD guarda o pacote · Abordagem A.

**Relacionado:** `2026-10-24-ai-tokens-design.md` (cotas/USB `AITOKENS`). Este spec **substitui** a parte “Configurações / Bridge no hub de AI Tokens” daquele doc. Glossário: Bridge = transporte USB do agente (termo legado na docs técnicas).

## Visão

O **Tessera Agent** é o processo no Mac que fala com o painel. O antigo “AI Tokens Bridge” é o embrião desse agente. **AI Tokens (cotas)** é o **primeiro recurso**; depois entram outros sinais/comandos no mesmo agente e na mesma seção de configurações.

## O que a pessoa vê

### Hub Configurações (aparelho + web)

| Entrada | Papel |
|---|---|
| **Agente Tessera** | Status e instalação; hub futuro do agente |
| **AI Tokens** | Resumo visual das fontes (cards); **sem** instalação |

Subtítulo do hub Agente: algo como `USB · status e instalação`.

### Página Agente Tessera — aparelho

- Status claro: Off / Ok / Falha / Desatualizado (+ motivo curto).
- Nota: instalar pelo site no notebook (não roda `install` no ESP).
- Sem pedido de cotas aqui (fica em AI Tokens).
- Sem toggles de fontes nesta entrega.

### Página Agente Tessera — web (notebook)

- Mesmo status (lido do painel).
- **Baixar instalador** → serve arquivo do **cartão SD** (não da flash).
- Passos curtos: baixar → abrir `.command` → agente confirma no painel.
- Se SD/pacote ausente: mensagem explícita, sem download quebrado.

### Página AI Tokens — aparelho (configs)

- Três cards compactos (Codex / Claude / Cursor): valor, barra, reset — alinhados ao visual da tela do widget.
- Botão / ação **Pedir cotas** → `AITOKENS?` (1º recurso do agente; disparado daqui, não da página Agente).
- Rodapé fino: “Cotas via Agente Tessera”.
- Sem bloco de Bridge/instalação.

### AI Tokens — web

Nesta entrega: opcional espelhar fontes se o status JSON já tiver snapshot; prioridade é a página **Agente**. Sem redesign pesado do portal além do Agente.

## Pacote no SD (não na flash)

- No SD: `/tessera/agent.tgz` (mesmo nível dos ícones de clima; Python mínimo + deps; binário Go/Rust depois).
- `GET /agent/install.command` é **gerado pelo firmware** (URL do painel embutida); não precisa estar no cartão.
- Firmware **serve** o tarball do SD via HTTP; **não** embute o pacote na partição de app.
- Script no monorepo: `agent/pack_sd.sh` gera o `agent.tgz` (e ainda um `.command` opcional para provisionamento no Mac).
- **Upload pela config web** (página Agente): um arquivo — `PUT /agent/agent.tgz` stream direto para o SD. Sem SD → erro explícito. Aparelho mostra só status do pacote (pronto/ausente).

## Instalação no Mac (bootstrap)

1. Com SD no aparelho: na página Agente do portal, envie o pacote (ou já deixe gravado no cartão).
2. Baixar instalador → abre o `.command` no notebook.
3. Script descompacta/instala em `~/.tessera/agent/`, registra launchd `com.tessera.agent` (migrar label antigo `com.tessera.aitokens-bridge` se existir), configura statusLine Claude se ainda for necessário para a 1ª capability.
4. Agente sobe, abre USB, confirma instalação/saúde no painel.

Sem dependência do path do clone `esp32-firmwares`.

## Protocolo

Linhas `\n`, tab-separadas. Cotas **permanecem** compatíveis:

```
AITOKENS\tv1\t…   /   AITOKENS?
```

Handshake / estado do agente (novo):

```
AGENT\tv1\t1
AGENT\tv1\t0\t<motivo_curto>
```

- Enviado após install bem-sucedido e/ou no start do agente.
- Painel guarda último estado do Agente em RAM (como o snapshot de cotas) para UI aparelho + web.
- Envelope `AGENT` fica pronto para capabilities futuras; nesta entrega só ok/falha (+ motivo).

**Web API:** status JSON inclui algo como `agent: { ok, detail }` derivado do último `AGENT` e, se útil, frescor do snapshot de cotas.

## Migração

- Código Mac: pacote `agent/` (irmão do firmware `src/`); `tools/aitokens_bridge/` aponta para lá.
- Spec de AI Tokens continua válido para métricas e linha `AITOKENS`; settings de Bridge naquele doc ficam obsoletas em favor deste.

## Critério de pronto

- Hub mostra **Agente Tessera** e **AI Tokens** separados (aparelho + web).
- Download do instalador só via SD; flash do firmware não cresce com o tarball.
- Após `.command`, painel mostra Ok (ou Falha com motivo) via `AGENT`.
- AI Tokens nas configs = cards das fontes, sem instalação.

## Fora desta entrega

- Sinais extras do Mac (bateria, Now Playing, calendário, etc.).
- Relógio em descanso: bordas dos flips cortadas + animação mais fluida (**próximo** após esta entrega).
- Toggles de fontes / intervalo no agente.
- Companion daemon em `localhost`.
- Persistência NVS do estado do agente.
- Capabilities novas além de cotas.

## Follow-up imediato (fora do escopo de código deste spec)

Corrigir screensaver: dígitos extremos (hora esquerda, minuto direita) com borda cortada; suavizar a troca do flip.

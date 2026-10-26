# Tessara

Painel de parede com home de widgets e telas de detalhe por subsistema.

## Language

**Clima**:
Subsistema do painel: cartão na home mais a(s) tela(s) abertas ao toque. Mostra condições meteorológicas do local configurado.
_Avoid_: Tempo (na UI — colide com o relógio), Meteorologia (só em docs/código se precisar)

**Cartão do Clima**:
Resumo do Clima na home: texto (temperatura e condição) mais ícone da condição quando há dado; sem dado, ícone genérico de indisponível. Toque abre a tela de detalhe.
_Avoid_: Widget sozinho (o cartão é a face do subsistema na home)

**Tela do Clima**:
Detalhe do Clima: bloco central (ícone grande, min/max do dia, condição atual) e grade dos próximos 6 dias.
_Avoid_: Página do clima (reserve “página” para o ajuste no celular); previsão horária (fora do escopo desta versão)

**Condição**:
Estado do tempo num momento ou dia, representado por frase curta e ícone (códigos WMO → frases/bitmaps do painel).
_Avoid_: Weather code (só no código)

**Local**:
Lugar usado nas chamadas de previsão. Gravado no painel; a pessoa define pelo celular/notebook (geolocalização do navegador e/ou busca por cidade), não pelo aparelho (sem GPS no hardware).
_Avoid_: Cidade (é um meio de chegar ao local, não o único)

**Página do celular**:
Interface web no celular ou notebook para configurar o aparelho e os widgets (incluindo o Local do Clima: geolocalização do navegador e/ou busca por cidade).
_Avoid_: App, site de ajustes

**Desatualizado**:
Marca de que ainda há previsão em cache, mas ela passou do prazo fresco (30 min) ou a última busca falhou. O painel continua mostrando o último valor bom.
_Avoid_: Offline (pode haver Wi‑Fi e cache velho ao mesmo tempo)

**Indisponível**:
Não há previsão para mostrar (sem Local, ou Local sem nenhum cache ainda e a busca falhou). Cartão/tela usam ícone genérico e texto de vazio.
_Avoid_: Erro (a pessoa não precisa ver código HTTP)

**AI Tokens**:
Subsistema do painel: cartão na home mais a tela de detalhe. Mostra uso e cotas locais de agentes de IA no computador ligado ao Tessara (MVP: Codex e Claude Code).
_Avoid_: TokenMeter (nome do projeto de inspiração), Consumo, Cotas (sozinhas — cobrem só metade do significado)

**Cartão de AI Tokens**:
Resumo do AI Tokens na home (no fim): duas linhas (Codex / Claude). Toque abre a tela de detalhe.
_Avoid_: Widget de tokens

**Tela de AI Tokens**:
Detalhe do AI Tokens: um bloco por Fonte com uso, cota/% e reset quando a Fonte expõe.
_Avoid_: Página de tokens (reserve “página” para o ajuste no celular)

**Agente Tessara** (antes: Bridge):
Programa no Mac que fala com o painel pela USB. Produto atual: Python em `agent/`. Redesign (**Agent v2**): **agentd** (Rust) + app companion — `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`. Até o cutover, labels/paths distintos.
_Avoid_: Servidor, API gateway, proxy de nuvem. Em docs técnicas antigas, “Bridge” = mesmo processo.

**agentd**:
Daemon Rust do Agent v2 (USB, Doctor, perfis). Distinto do agent Python legado (`com.tessera.agent`).

**Bridge**:
Nome legado do Agente Tessara (só transporte USB de cotas). Preferir **Agente Tessara** na UI.
_Avoid_ na UI nova.

**Fonte**:
Um provedor ou ferramenta cuja cota/uso o Bridge consegue ler no Mac (MVP: Codex, Claude Code, Cursor). Rótulos na UI: **Codex**, **Claude**, **Cursor**. Preferência de exibição: **% restante**.
_Avoid_: Provider (só no código se precisar), conta

**Mac** (subsistema):
Companion de sistema: tile **Mac** (Focus) + Tela Mac, separado do tile **Música**. USB via Agente Tessara.
_Avoid_: Notebook (colide com a página do celular), Host (só no código se precisar), Controle remoto, Remote desktop

**Música** (tile):
Half-tile na home (`org.tessara.media`): faixa/artista do Agora tocando. Toque → Tela Música (capa + metadados, só leitura).
_Avoid_: Cartão Mac (mídia)

**Cartão / tile Mac**:
Half-tile (`org.tessara.mac`): Focus ativo (“Nenhum” / offline). Toque → Tela Mac.
_Avoid_: Widget do Mac

**Tela Música**:
Detalhe de mídia: capa, título, artista, álbum; sem nome do app; sem controles.
_Avoid_: Tela Mac (mídia)

**Tela Mac**:
Lista de Focus do Mac; item ativo marcado; “Nenhum” → OFF. Lock fora deste corte.
_Avoid_: Página do Mac

**Agora tocando**:
Faixa que o macOS expõe como sessão de mídia ativa (Music/Spotify via DNC). No painel: título, artista, estado tocando/pausado.
_Avoid_: Now Playing (só no código/spec se precisar), Media session

**Mac offline**:
Sem Agente na USB. Distinto de “Nada tocando” (agente ok) e de “Nenhum” (Focus off).
_Avoid_: Desconectado (genérico demais), Offline sozinho (colide com outros subsistemas)

**Focus** (no Mac):
Modo de concentração do macOS na Tela Mac / tile Mac. Toque aplica (`FOCUS\tSET`); “Nenhum” desliga (`FOCUS\tOFF`).
_Avoid_: DND, Não perturbe (rótulo de um Focus, não do conceito), Focus Mode (só no código se precisar)

**Lock** (no Mac):
Ação na Tela Mac que trava a sessão do computador ligado ao Agente. Acionamento por segurar (hold), não por toque curto.
_Avoid_: Bloquear tela, Sleep (é outra ação, fora destas fases)

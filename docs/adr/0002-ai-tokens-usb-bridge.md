# AI Tokens via Bridge USB (não HTTP)

As cotas de Codex/Claude vivem no Mac; o painel só exibe. Em vez de Wi‑Fi/HTTP como o TokenMeter, o MVP usa **USB serial** (`AITOKENS`) porque o Tessara já provisiona e tira SNAP por esse cabo, e o cartão deve funcionar sem depender da rede do painel. O Bridge sobe no login (launchd), solta a porta no flash, e fica estruturado para um agente maior depois — no dia 1 só empurra snapshots.

# Calendar capability

OAuth Google no Mac; o painel só recebe snapshot/catálogo USB.

## Setup (uma vez)

1. No [Google Cloud Console](https://console.cloud.google.com/), crie um OAuth client **Desktop**.
2. Baixe o JSON e salve como:

```text
~/.tessera/calendar/client.json
```

Formato esperado: o JSON baixado do Google (`installed` ou `web` com client_id/secret).

3. Reinicie o Agente Tessara.
4. No portal do painel → **Agenda** → **Adicionar conta** (abre o browser no Mac).
5. Marque os calendários desejados.

Tokens ficam em `~/.tessera/calendar/accounts/`. Selection cache: `selection.json`.

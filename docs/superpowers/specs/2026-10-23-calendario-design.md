# Google Calendar

**Depende de:** a etapa 8 de `docs/superpowers/plans/2026-10-23-home-widgets.md`.

**Conta:** OAuth de aparelho com entrada limitada, escopo `https://www.googleapis.com/auth/calendar.readonly`. O `client_id` fica num cabeçalho ignorado pelo git, no mesmo estilo do token do Todoist. O refresh token fica na NVS. A página e o log não devolvem nem imprimem `client_id`, código de aparelho, access token nem refresh token. O estado só diz `calendar_set: true|false`.

## O que a pessoa vê

- **Cartão na home.** Sem conta, "ligar agenda". Com conta e dia vazio, "nada hoje". Com eventos, a hora e o título do próximo, e o total do que ainda falta hoje.
- **Tela da agenda.** Toque no cartão. Até 8 eventos de hoje, hora e título. Dia inteiro aparece como "dia inteiro". Voltar retorna à home.
- **Ligação.** Na página do celular, "Ligar agenda" pede o código ao Google e mostra o endereço `https://www.google.com/device` e o código de 8 caracteres. O painel consulta o Google até autorizar, expirar ou a pessoa cancelar. No aparelho, a aba Agenda mostra o mesmo código enquanto a ligação está aberta, e o QR da página quando a conta ainda não existe.

## Dados

Eventos de hoje no fuso `America/Sao_Paulo`, calendário `primary`, `singleEvents=true`, ordenados pelo início. Entram os que ainda não terminaram. O cartão usa o primeiro dessa lista.

## Cache e rede

- Sem refresh token, zero pedidos à API de eventos.
- Com token, uma busca a cada 15 minutos em segundo plano.
- Access token renovado com o refresh token quando a API responde 401. Um 401 depois da renovação apaga a sessão e o cartão volta a "ligar agenda".
- Falha de rede guarda a lista e marca "desatualizado".
- O cartão redesenha quando o próximo título, a hora ou o total mudam.
- Resposta com geração antiga é descartada.

## Testes no computador

- JSON de eventos de hoje produz ordem, total e o próximo.
- Evento já terminado fica de fora.
- Sem token, o plano de busca fica vazio.
- HTTP 401 uma vez renova; 401 de novo limpa a sessão.
- A página de estado serializada no teste contém `calendar_set` e não contém as chaves de token.

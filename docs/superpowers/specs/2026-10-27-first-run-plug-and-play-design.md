# Primeira instalação plug-and-play

Um notebook novo só pede o que o sistema não faz sozinho: ligar o acesso aos arquivos, entrar na conta Google e abrir o Claude Code uma vez.

## Pesquisa

- Disco completo não tem prompt. A Apple exige o interruptor em Ajustes do Sistema. O item da lista é o executável que o `launchd` roda. Um binário solto e o app da janela são identidades diferentes. Sem Developer ID, `AssociatedBundleIdentifiers` não cola os dois. A identidade estável é um app próprio, `Tessera Agent.app`, num caminho fixo.
- OAuth de app instalado da Google abre o browser e volta em `http://127.0.0.1`. PKCE é obrigatório. O endpoint de token ainda pede `client_secret` em cliente Desktop. O segredo não é confidencial nesse tipo de cliente, mas não inventamos um: o usuário escolhe o JSON baixado do Google Cloud uma vez.
- A linha de status do Claude Code recebe `rate_limits` no stdin depois da primeira resposta da sessão. O instalador grava o comando em `~/.claude/settings.json` sem apagar um comando que o usuário já definiu.

## Acesso aos arquivos

- O instalador e o app colocam o agente em `~/.tessera/agentd/Tessera Agent.app` e o LaunchAgent aponta para `Contents/MacOS/Tessera`.
- O assistente mostra um único cartão, “Acesso aos arquivos”. Focus e Claude deixam de aparecer como permissões separadas.
- “Abrir Ajustes” abre Disco completo. O agente já entra na lista ao tocar um arquivo protegido. O usuário só liga o interruptor.
- O texto diz que o app da pasta Aplicativos é só a janela.

## Varredura

- Ausente: desligado. Pronto: ligado. A escolha grava `show_aitokens_*` no painel no mesmo passo do assistente.
- Cursor e Focus sem leitura: ligados, com “aguardando acesso aos arquivos”.
- Codex pronto mostra a cota sem essa permissão.
- Claude com o app instalado: “Abra o Claude Code uma vez”. O painel, sem número, mostra `abra Code`, `acesso` ou `login` no lugar da barra vazia (até 27 caracteres).
- Agenda sem conta: “Conta Google ainda não ligada”, interruptor disponível, cartão para ligar a conta.

## Agenda

- “Adicionar conta” roda no agente, neste Mac. Não passa pelo portal do painel.
- Sem `client.json`, o cartão explica o cliente Desktop e aceita o JSON baixado.
- Com o JSON, o browser abre, o retorno local grava a conta e a lista de calendários. O que estiver marcado é o que o aparelho exibe. A seleção fica em `~/.tessera/calendar/selection.json`.

# Simulador desktop

**Depende de:** a etapa 8 da home e os modelos puros de meteorologia e calendário. A janela com os três cartões espera esses modelos. A janela só com a home pode nascer assim que a etapa 4 da home existir.

**O que é:** o mesmo UI kit, a mesma home e as mesmas telas de Todoist, clima e agenda, compilados num programa de macOS com LVGL 9 e SDL2. O mouse faz o papel do toque. Não é uma página web e não grava o aparelho.

## Alvos

- Janela padrão 320×480, retrato. Também abre em 480×320.
- Relógio fixo no cenário, para a imagem não mudar a cada segundo.
- Wi-Fi, bateria, tarefas, clima e agenda vêm de arquivos em `sim/fixtures/`.
- Ajustes do simulador vão para um diretório temporário, separado da NVS do aparelho.
- Código de widget, kit, home e telas não inclui `Arduino.h`, `WiFi.h`, `Preferences.h` nem cabeçalho de placa.

## Cenários

O comando é `sim/build/painel --viewport 320x480 --scenario <nome>`.

| Nome | O que fixa |
| --- | --- |
| `normal` | Wi-Fi ligado, bateria em 80, Todoist com 3 tarefas, clima fresco a 24 °C, um evento às 15:00 |
| `sem_bateria` | percentual −1, ícone oculto |
| `offline` | Wi-Fi desligado, clima e agenda marcados desatualizado |
| `sem_local` | cartão do clima em "definir local" |
| `sem_agenda` | cartão da agenda em "ligar agenda" |
| `resposta_atrasada` | a geração do clima muda antes do JSON chegar; o cache novo permanece |

`--screenshot caminho.png` grava um quadro e encerra. Serve para olhar a home sem deixar a janela aberta.

## Testes

- Os testes `pio test -e native` continuam sem SDL.
- Um teste de arquitetura falha se `src/ui`, `src/todoist`, `src/weather` ou `src/calendar` incluírem cabeçalho de Arduino, Wi-Fi, NVS ou placa.
- O cenário `resposta_atrasada` roda no teste nativo do modelo, e o simulador só repete esse estado na tela.

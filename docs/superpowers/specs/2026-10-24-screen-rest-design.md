# Descanso de tela (anti-ghosting)

**Decisões:** ocioso configurável → **flip clock NeXtime** (1 min) → **fade do brilho** → backlight off; 1º toque só acorda (não clica); 2º toque usa a UI. Relógio **centralizado** (sem drift).

O Tessara fica ligado por horas na mesma imagem. LCD IPS sofre image retention. O descanso primeiro **anima** (relógio flip), depois **diminui o brilho até apagar**; rede/Bridge seguem.

## Fluxo

1. Sem interação pelo tempo de **Descanso de tela** (Ajustes) → overlay fullscreen preto + **relógio flip NeXtime** (`HH:MM`) **centralizado**.
2. Animação por **1 min** (flip na virada de minuto).
3. Nos **~10 s** seguintes: brilho **fade** do valor de Ajustes → 0.
4. Backlight **off**.
5. **1º toque** (saver ou off): esconde overlay, restaura brilho, **engole** o gesto.
6. Toques seguintes usam a UI e reiniciam o timer de ociosidade.
7. **FLASH**/loader: não entra em saver/off.
8. **Nunca**: não entra em descanso pelo idle (console `REST` ainda força fases).

## Ajuste

- **Aparelho** e **página do celular** (Geral): chips `2 · 5 · 10 · 20 · 30 min · Nunca`.
- Padrão: **5 min**. Persistido em NVS (`descanso`).

## Visual do screensaver (NeXtime Flip)

- Estilo **flip/folha**: dígitos grandes em cartões com linha horizontal no meio; virada curta na mudança de minuto.
- Quatro dígitos + `:` estático.
- Fundo **preto**; contraste alto.
- Conjunto **fixo no centro** da tela (sem drift).
- Sem data obrigatória.

## Interação = atividade

Reinicia o timer / cancela saver:

- Toque filtrado válido, tecla, `DRAG` de teste.

Não reinicia: sheen, `AITOKENS`, fetch em background.

## Estados

| Fase | Duração | Display |
|---|---|---|
| Awake | — | UI + brilho de Ajustes |
| Saver | 1 min | overlay flip + backlight ligado |
| Fading | ~8–12 s | overlay ainda visível; brilho desce |
| Off | até toque | backlight 0 |

`idle_before_saver_ms` vem de `rest_minutes`; `kSaverBeforeFadeMs = 60*1000`, `kFadeMs ≈ 10000`.

Console: `REST` / `REST saver` / `REST fade` / `REST off` / `REST wake`.

## Técnico

- `platform/screen_rest` + `ui/screensaver` (overlay flip centralizado).
- `DeviceSettings::rest_minutes` + Settings Geral + portal.
- `input_host` engole o 1º gesto ao acordar.
- `display_host::set_brightness` para fade e off.

## Fora deste corte

Snow antiburn noturno; deep sleep; tempos de saver/fade ajustáveis.

## Teste

1. Ajustes → Descanso de tela → **Aparece após** (inatividade) + **Relógio fica por** (antes de apagar) / Nunca / Até tocar.
2. `REST saver` → flip centrado; SNAP.
3. `REST fade` / `REST off` / wake.
4. Nunca: idle longo não entra em saver.

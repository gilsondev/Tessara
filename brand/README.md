# Tessera brand (Logo Kit v1)

Product name: **Tessera** (no accent). All masters are **transparent** SVG; use the Light or Dark variant that matches the surface — never bake a gray plate into the home lockup.

| File | Role |
|---|---|
| `symbol-light.svg` / `symbol-dark.svg` | Mosaic mark, color |
| `lockup-light.svg` / `lockup-dark.svg` | Horizontal signature (mark + Tessera), vector type |
| `symbol-mono-*.svg` / `lockup-mono-*.svg` | Monochrome |
| `mark.svg` | Dock / app icon (slate rounded plate) |
| `mark-transparent.svg` | Alias of symbol-light for UI rows |
| `mark-tray.svg` | macOS menu-bar template (white) |
| `kit/` | Full Logo Kit v1 (PNG sizes + README + previews) |
| `COLORS.md` | Palette |

## Where each variant goes

| Surface | Asset |
|---|---|
| Home wordmark (firmware) | `lockup-light` / `lockup-dark` → RGB565A8 |
| Settings / Agente row | `symbol-light` 24×24 → RGB565A8 |
| Agent Dock | `mark.svg` |
| Menu bar | `mark-tray.svg` |

```bash
python3 tools/gen_agent_icons.py
```

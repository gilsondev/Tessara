# Tessera — brand colors

Canonical name: **Tessera** (no accent). Palette from Logo Kit v1.

| Token | Hex | Use |
|---|---|---|
| Slate | `#1F2937` | Wordmark text (light), app-icon plate |
| Slate deep | `#294551` | Symbol blocks on light |
| Slate soft | `#405967` | Symbol blocks on dark |
| Teal | `#2CA58D` | Accent tesserae |
| Sand | `#D4A95B` | Highlight tessera |
| Mist (light) | `#D5D7D5` | Small tesserae on light |
| Mist (dark) | `#B8C3C8` | Small tesserae on dark |
| Off White | `#F8F9FA` | Wordmark text (dark), surfaces |
| Stone | `#E5E7EB` | Borders / dividers |
| Sky | `#60A5FA` | Info / links (not in logo) |

## Variants

| File | Surface |
|---|---|
| `symbol-light.svg` / `lockup-light.svg` | Light UI (transparent) |
| `symbol-dark.svg` / `lockup-dark.svg` | Dark UI (transparent) |
| `symbol-mono-dark.svg` | Dark ink on light |
| `symbol-mono-light.svg` / `mark-tray.svg` | White ink on dark / menu bar |
| `mark.svg` | App icon (slate plate + symbol) |

Masters live under `brand/`; full kit (PNG exports + previews) in `brand/kit/`.

```bash
python3 tools/gen_agent_icons.py
```

# Portal web (fonte)

HTML editável do configurador. O build embute estes arquivos em PROGMEM via `tools/embed_web.py` (pre-script do PlatformIO).

| Fonte | Header gerado |
|---|---|
| `src/settings.html` | `firmware/.../web_page.h` (`kPage`) |
| `src/setup.html` | `firmware/.../setup_page.h` (`kSetupPage`) |

Não edite os `.h` à mão — altere `web/src/` e rode `python3 tools/embed_web.py` (ou qualquer `pio run` / `pio test`).

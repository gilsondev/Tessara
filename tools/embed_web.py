#!/usr/bin/env python3
"""Embed web/src/*.html into PROGMEM headers under web_configurator (Tessara web/ extract)."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web" / "src"
OUT = ROOT / "firmware" / "components" / "web_configurator" / "services"


def embed(src_name: str, header_name: str, symbol: str, raw_tag: str, banner: str) -> None:
    html = (WEB / src_name).read_text(encoding="utf-8")
    if not html.strip():
        raise SystemExit(f"empty {src_name}")
    # Keep raw-string delimiters free of the HTML body markers.
    if f"){raw_tag}\"" in html or f"R\"{raw_tag}(" in html:
        raise SystemExit(f"{src_name}: conflicts with raw-string tag {raw_tag}")
    body = f"""#pragma once

#include <pgmspace.h>

namespace web_portal {{

{banner}
const char {symbol}[] PROGMEM = R"{raw_tag}({html}){raw_tag}";

}}  // namespace web_portal
"""
    out = OUT / header_name
    out.write_text(body, encoding="utf-8")
    print(f"embed_web: {src_name} -> {out.relative_to(ROOT)} ({len(html)} chars)")


def main() -> None:
    embed(
        "settings.html",
        "web_page.h",
        "kPage",
        "HTML",
        "",
    )
    embed(
        "setup.html",
        "setup_page.h",
        "kSetupPage",
        "PAGE",
        "// Shown only while the panel is its own Wi-Fi, before the home network and the token exist.\n",
    )


if __name__ == "__main__":
    main()

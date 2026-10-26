#!/usr/bin/env python3
"""Reorganize POC tree toward Tessera ARCHITECTURE.md layout (validation spike).

Does not rewrite to Widget SDK — only folder shape + include-prefix preservation
via platformio -I paths (see docs/STRUCTURE.md written next to this).
"""
from __future__ import annotations

import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
FW = ROOT / "firmware"


def ensure(p: Path) -> Path:
    p.mkdir(parents=True, exist_ok=True)
    return p


def move(src: Path, dst: Path) -> None:
    if not src.exists():
        print(f"skip missing {src.relative_to(ROOT)}")
        return
    ensure(dst.parent)
    if dst.exists():
        print(f"exists {dst.relative_to(ROOT)}")
        return
    print(f"{src.relative_to(ROOT)} → {dst.relative_to(ROOT)}")
    shutil.move(str(src), str(dst))


def main() -> int:
    if not SRC.is_dir():
        if FW.is_dir():
            print("already restructured (no src/)")
            return 0
        raise SystemExit("no src/ to move")

    ensure(FW)

    # Entry
    move(SRC / "main.cpp", FW / "main.cpp")

    # components/hal
    move(SRC / "hal", FW / "components" / "hal")

    # boards adapter (PIO board JSON stays at repo boards/*.json)
    move(SRC / "boards" / "jc3248w535", FW / "boards" / "jc3248w535")

    # core_services
    move(SRC / "platform", FW / "components" / "core_services" / "platform")
    move(SRC / "domain", FW / "components" / "core_services" / "domain")
    move(SRC / "agent", FW / "components" / "core_services" / "agent")
    ensure(FW / "components" / "core_services" / "services")
    for name in ("net_worker.cpp", "net_worker.h", "text.cpp", "text.h",
                 "todoist_client.cpp", "todoist_client.h",
                 "weather_client.cpp", "weather_client.h"):
        move(SRC / "services" / name, FW / "components" / "core_services" / "services" / name)

    # web_configurator
    ensure(FW / "components" / "web_configurator" / "services")
    for name in ("web_portal.cpp", "web_portal.h", "web_page.h", "setup_page.h"):
        move(SRC / "services" / name, FW / "components" / "web_configurator" / "services" / name)

    # ui_kit (keep ui/ prefix for includes)
    ui_kit = ensure(FW / "components" / "ui_kit" / "ui")
    for name in ("kit.cpp", "kit.h", "palette.cpp", "palette.h", "theme.h",
                 "bitmaps.cpp", "bitmaps.h"):
        move(SRC / "ui" / name, ui_kit / name)
    for name in ("font_pt13.c", "font_pt15.c", "font_pt15_bold.c", "font_pt16.c", "font_pt18.c"):
        move(SRC / name, FW / "components" / "ui_kit" / name)

    # dashboard_shell
    shell_ui = ensure(FW / "components" / "dashboard_shell" / "ui")
    for name in ("shell.cpp", "ui.h", "home.cpp", "home.h", "screensaver.cpp", "screensaver.h",
                 "settings_panel.cpp", "settings_panel.h", "setup_panel.cpp", "setup_panel.h",
                 "loader.cpp", "loader.h", "brand_mark.cpp", "brand_mark.h",
                 "task_page.cpp", "task_page.h"):
        move(SRC / "ui" / name, shell_ui / name)
    move(SRC / "app", FW / "components" / "dashboard_shell" / "app")

    # widget stubs (SDK / runtime)
    ensure(FW / "components" / "widget_sdk")
    ensure(FW / "components" / "widget_runtime")
    (FW / "components" / "widget_sdk" / "README.md").write_text(
        "# widget_sdk (stub)\n\nPOC: still screen-based. Tessera target: WidgetContext / lifecycle.\n",
        encoding="utf-8",
    )
    (FW / "components" / "widget_runtime" / "README.md").write_text(
        "# widget_runtime (stub)\n\nPOC: scheduling still in app/controller + net_worker.\n",
        encoding="utf-8",
    )

    # widgets/weather
    move(SRC / "weather", FW / "widgets" / "weather" / "weather")
    wui = ensure(FW / "widgets" / "weather" / "ui")
    for name in ("weather_page.cpp", "weather_page.h", "weather_icons.cpp", "weather_icons.h",
                 "weather_icons_data.cpp", "weather_icons_data.h"):
        move(SRC / "ui" / name, wui / name)

    # widgets/aitokens
    move(SRC / "aitokens", FW / "widgets" / "aitokens" / "aitokens")
    aui = ensure(FW / "widgets" / "aitokens" / "ui")
    for name in ("aitokens_page.cpp", "aitokens_page.h", "aitokens_logos.cpp", "aitokens_logos.h",
                 "aitokens_logos_data.cpp", "aitokens_logos_data.h"):
        move(SRC / "ui" / name, aui / name)

    # widgets/todoist (non-MVP acceptance; kept for POC)
    move(SRC / "todoist", FW / "widgets" / "todoist" / "todoist")
    (FW / "widgets" / "todoist" / "README.md").write_text(
        "# todoist widget (POC only)\n\nOut of Tessera MVP acceptance — validates subsystem pattern.\n",
        encoding="utf-8",
    )

    # widgets/clock stub (flip clock still in dashboard_shell screensaver)
    ensure(FW / "widgets" / "clock")
    (FW / "widgets" / "clock" / "README.md").write_text(
        "# clock widget (stub)\n\nPOC: clock lives in dashboard_shell screensaver + home header.\n"
        "Tessera target: first-class clock widget package.\n",
        encoding="utf-8",
    )

    # leftovers
    if SRC.exists():
        left = list(SRC.rglob("*"))
        files = [p for p in left if p.is_file()]
        if files:
            print("leftover under src/:")
            for p in files:
                print(" ", p.relative_to(ROOT))
        else:
            # remove empty dirs
            shutil.rmtree(SRC, ignore_errors=True)

    # top-level stubs
    ensure(ROOT / "simulator")
    (ROOT / "simulator" / "README.md").write_text(
        "# simulator\n\nPOC stub. Tessera: SDL host + fixtures (SIMULATOR.md). Lab deferred this — do not skip forever.\n",
        encoding="utf-8",
    )
    ensure(ROOT / "web")
    (ROOT / "web" / "README.md").write_text(
        "# web\n\nPOC: HTML still embedded in `firmware/components/web_configurator/services/web_page.h`.\n"
        "Tessera target: `web/{src,assets}` extracted for phone/notebook settings.\n",
        encoding="utf-8",
    )

    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

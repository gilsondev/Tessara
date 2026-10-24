# PlatformIO pre-script: regenerate PROGMEM headers from web/src.
Import("env")  # type: ignore  # noqa: F821 — PlatformIO injects Import

env.Execute("$PYTHONEXE $PROJECT_DIR/tools/embed_web.py")  # type: ignore  # noqa: F821

# widget_runtime

Registry + mount/unmount + generation tags. Shell registers builtins via `register_builtin_widgets()`.

Stale-result rule: discard work whose generation is no longer live (`generation_live`).

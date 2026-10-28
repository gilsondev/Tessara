//! Register Claude Code's status line so a session writes the quota file.

use std::fs;
use std::path::Path;
use std::process::Command;

const SCRIPT: &str = r#"#!/bin/bash
# Claude Code statusLine → ~/.tessera/claude_status.json
set -euo pipefail
mkdir -p "$HOME/.tessera"
python3 /dev/fd/3 3<<'PY'
import json, sys, time
from pathlib import Path

raw = sys.stdin.read()
try:
    data = json.loads(raw) if raw.strip() else {}
except json.JSONDecodeError:
    data = {}

limits = data.get("rate_limits") or {}
five = limits.get("five_hour") or {}
seven = limits.get("seven_day") or {}
chosen = five if five.get("used_percentage") is not None else seven
pct = chosen.get("used_percentage")
resets = chosen.get("resets_at")
label = ""
if isinstance(resets, (int, float)):
    label = time.strftime("%H:%M", time.localtime(resets))

if not isinstance(pct, (int, float)):
    raise SystemExit(0)
out = {
    "generated_at": time.time(),
    "percent_used": pct,
    "display_value": f"{int(pct)}%",
    "reset_label": label,
    "source": "claude_statusline",
}
Path.home().joinpath(".tessera/claude_status.json").write_text(json.dumps(out), encoding="utf-8")
if out["display_value"]:
    print(f"5h {out['display_value']}" + (f" · {label}" if label else ""))
PY
"#;

/// Keeps `prev` when the config file cannot be read, so a transient error does not uninstall the hook.
pub fn hook_enabled<E>(prev: bool, read: Result<bool, E>) -> bool {
    match read {
        Ok(value) => value,
        Err(_) => prev,
    }
}

pub fn ensure(home: &Path, enabled: bool) {
    if !enabled {
        restore(home);
        return;
    }
    let claude_dir = home.join(".claude");
    let app = Path::new("/Applications/Claude.app");
    if !claude_dir.is_dir() && !app.is_dir() {
        return;
    }
    let script = home.join(".tessera/agentd/statusline_claude.sh");
    if let Some(dir) = script.parent() {
        let _ = fs::create_dir_all(dir);
    }
    let script_ok = fs::read(&script).ok().as_deref() == Some(SCRIPT.as_bytes());
    if !script_ok && fs::write(&script, SCRIPT).is_err() {
        return;
    }
    if !script_ok {
        let _ = Command::new("/bin/chmod")
            .args(["755"])
            .arg(&script)
            .status();
    }
    let settings_path = claude_dir.join("settings.json");
    let _ = fs::create_dir_all(&claude_dir);
    // A broken settings file must stay untouched. Replacing it with `{}` would
    // wipe the user's Claude Code configuration on the next 30s pass.
    let Some(mut doc) = read_settings(&settings_path) else {
        return;
    };
    let current = statusline_command(&doc);
    let command = script.display().to_string();
    let chain = home.join(".tessera/agentd/statusline_chain.sh");
    let chain_command = chain.display().to_string();
    if current == command || current == chain_command {
        return;
    }
    let ours = current.is_empty()
        || current.contains("statusline_claude")
        || current.contains("statusline_chain");
    let installed = if ours {
        command
    } else {
        let prev = home.join(".tessera/statusline_prev");
        if fs::write(&prev, &current).is_err() {
            return;
        }
        if fs::write(&chain, CHAIN).is_err() {
            return;
        }
        let _ = Command::new("/bin/chmod")
            .args(["755"])
            .arg(&chain)
            .status();
        chain_command
    };
    doc["statusLine"] = serde_json::json!({
        "type": "command",
        "command": installed,
    });
    let _ = write_settings(&settings_path, &doc);
}

const CHAIN: &str = r#"#!/bin/bash
# Deliver Claude's status JSON to Tessera and to the command that was already installed.
input=$(cat)
printf '%s' "$input" | "$HOME/.tessera/agentd/statusline_claude.sh" >/dev/null 2>&1 &
prev="$HOME/.tessera/statusline_prev"
if [ -s "$prev" ]; then
  printf '%s' "$input" | /bin/bash -c "$(cat "$prev")"
fi
"#;

/// Put the previous statusLine command back when the current one is ours.
pub fn restore(home: &Path) {
    let settings_path = home.join(".claude/settings.json");
    if !settings_path.is_file() {
        return;
    }
    let Some(mut doc) = read_settings(&settings_path) else {
        return;
    };
    let current = statusline_command(&doc);
    let ours = current.contains("statusline_chain.sh") || current.contains("statusline_claude.sh");
    if !ours {
        return;
    }
    let prev = fs::read_to_string(home.join(".tessera/statusline_prev")).unwrap_or_default();
    let prev = prev.trim().to_string();
    if prev.is_empty() {
        doc.as_object_mut().map(|map| map.remove("statusLine"));
    } else {
        doc["statusLine"] = serde_json::json!({
            "type": "command",
            "command": prev,
        });
    }
    if write_settings(&settings_path, &doc) {
        let _ = fs::remove_file(home.join(".tessera/statusline_prev"));
    }
}

fn statusline_command(doc: &serde_json::Value) -> String {
    doc.get("statusLine")
        .and_then(|v| v.get("command"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string()
}

/// `None` when the file exists but is not JSON. Missing file is an empty object.
fn read_settings(path: &Path) -> Option<serde_json::Value> {
    if !path.is_file() {
        return Some(serde_json::json!({}));
    }
    let raw = fs::read_to_string(path).ok()?;
    serde_json::from_str(&raw).ok()
}

fn write_settings(path: &Path, doc: &serde_json::Value) -> bool {
    let Ok(raw) = serde_json::to_string_pretty(doc) else {
        return false;
    };
    // Follow a symlink so a dotfiles repo keeps receiving the file.
    let target = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    tessera_proc::write_atomic_preserve(&target, format!("{raw}\n")).is_ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn writes_statusline_when_claude_dir_exists() {
        let temp = tempfile::tempdir().unwrap();
        fs::create_dir_all(temp.path().join(".claude")).unwrap();
        ensure(temp.path(), true);
        let settings = fs::read_to_string(temp.path().join(".claude/settings.json")).unwrap();
        assert!(settings.contains("statusline_claude.sh"));
        assert!(temp
            .path()
            .join(".tessera/agentd/statusline_claude.sh")
            .is_file());
    }

    #[test]
    fn second_ensure_keeps_other_settings() {
        let temp = tempfile::tempdir().unwrap();
        let dir = temp.path().join(".claude");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("settings.json"), "{\n  \"permissions\": {}\n}\n").unwrap();
        ensure(temp.path(), true);
        ensure(temp.path(), true);
        let settings = fs::read_to_string(dir.join("settings.json")).unwrap();
        assert!(settings.contains("permissions"));
        assert!(settings.contains("statusline_claude.sh"));
    }

    #[test]
    fn chains_a_custom_statusline() {
        let temp = tempfile::tempdir().unwrap();
        let dir = temp.path().join(".claude");
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("settings.json"),
            r#"{"statusLine":{"type":"command","command":"/tmp/mine.sh"}}"#,
        )
        .unwrap();
        ensure(temp.path(), true);
        let settings = fs::read_to_string(dir.join("settings.json")).unwrap();
        assert!(settings.contains("statusline_chain.sh"));
        let prev = fs::read_to_string(temp.path().join(".tessera/statusline_prev")).unwrap();
        assert_eq!(prev, "/tmp/mine.sh");
        restore(temp.path());
        let restored = fs::read_to_string(dir.join("settings.json")).unwrap();
        assert!(restored.contains("/tmp/mine.sh"));
        assert!(!restored.contains("statusline_chain.sh"));
    }

    #[test]
    fn script_records_the_percent_from_stdin() {
        let temp = tempfile::tempdir().unwrap();
        fs::create_dir_all(temp.path().join(".claude")).unwrap();
        ensure(temp.path(), true);
        let script = temp.path().join(".tessera/agentd/statusline_claude.sh");
        let mut child = Command::new("/bin/bash")
            .arg(&script)
            .env("HOME", temp.path())
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .spawn()
            .unwrap();
        use std::io::Write;
        child
            .stdin
            .take()
            .unwrap()
            .write_all(br#"{"rate_limits":{"five_hour":{"used_percentage":42}}}"#)
            .unwrap();
        assert!(child.wait().unwrap().success());
        let raw = fs::read_to_string(temp.path().join(".tessera/claude_status.json")).unwrap();
        assert!(raw.contains("42"), "{raw}");
    }

    #[test]
    fn invalid_settings_are_left_untouched() {
        let temp = tempfile::tempdir().unwrap();
        let dir = temp.path().join(".claude");
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("settings.json");
        let raw = "{ this is not json\n";
        fs::write(&path, raw).unwrap();
        ensure(temp.path(), true);
        restore(temp.path());
        assert_eq!(fs::read_to_string(&path).unwrap(), raw);
    }

    #[test]
    fn settings_file_mode_is_preserved() {
        use std::os::unix::fs::PermissionsExt;
        let temp = tempfile::tempdir().unwrap();
        let dir = temp.path().join(".claude");
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("settings.json");
        fs::write(&path, "{}\n").unwrap();
        let mut perms = fs::metadata(&path).unwrap().permissions();
        perms.set_mode(0o644);
        fs::set_permissions(&path, perms).unwrap();
        ensure(temp.path(), true);
        let mode = fs::metadata(&path).unwrap().permissions().mode() & 0o777;
        assert_eq!(mode, 0o644);
    }

    #[test]
    fn settings_symlink_is_preserved() {
        let temp = tempfile::tempdir().unwrap();
        let dir = temp.path().join(".claude");
        fs::create_dir_all(&dir).unwrap();
        let real = temp.path().join("real.json");
        fs::write(&real, "{}\n").unwrap();
        std::os::unix::fs::symlink(&real, dir.join("settings.json")).unwrap();
        ensure(temp.path(), true);
        assert!(fs::symlink_metadata(dir.join("settings.json"))
            .unwrap()
            .file_type()
            .is_symlink());
        let body = fs::read_to_string(&real).unwrap();
        assert!(body.contains("statusLine"), "{body}");
    }

    #[test]
    fn hook_enabled_keeps_the_previous_value_when_the_read_fails() {
        assert!(hook_enabled(true, Ok::<bool, &str>(true)));
        assert!(!hook_enabled(true, Ok::<bool, &str>(false)));
        assert!(hook_enabled(true, Err("io")));
        assert!(!hook_enabled(false, Err("io")));
    }

    #[test]
    fn disabled_toggle_restores_the_previous_command() {
        let temp = tempfile::tempdir().unwrap();
        let dir = temp.path().join(".claude");
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("settings.json"),
            r#"{"statusLine":{"type":"command","command":"/tmp/mine.sh"}}"#,
        )
        .unwrap();
        ensure(temp.path(), true);
        assert!(fs::read_to_string(dir.join("settings.json"))
            .unwrap()
            .contains("statusline_chain.sh"));
        ensure(temp.path(), false);
        let restored = fs::read_to_string(dir.join("settings.json")).unwrap();
        assert!(restored.contains("/tmp/mine.sh"));
        assert!(!restored.contains("statusline_chain"));
        assert!(!temp.path().join(".tessera/statusline_prev").exists());
    }
}

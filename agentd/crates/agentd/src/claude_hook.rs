//! Register Claude Code's status line so a session writes the quota file.

use std::fs;
use std::path::Path;
use std::process::Command;

const SCRIPT: &str = r#"#!/bin/bash
# Claude Code statusLine → ~/.tessera/claude_status.json
set -euo pipefail
mkdir -p "$HOME/.tessera"
python3 - <<'PY'
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

pub fn ensure(home: &Path) {
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
    let mut doc = fs::read_to_string(&settings_path)
        .ok()
        .and_then(|raw| serde_json::from_str::<serde_json::Value>(&raw).ok())
        .unwrap_or_else(|| serde_json::json!({}));
    let current = doc
        .get("statusLine")
        .and_then(|v| v.get("command"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    let command = script.display().to_string();
    if current == command {
        return;
    }
    let ours = current.is_empty() || current.contains("statusline_claude");
    if !ours {
        return;
    }
    doc["statusLine"] = serde_json::json!({
        "type": "command",
        "command": command,
    });
    if let Ok(raw) = serde_json::to_string_pretty(&doc) {
        let _ = fs::write(settings_path, raw + "\n");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn writes_statusline_when_claude_dir_exists() {
        let temp = tempfile::tempdir().unwrap();
        fs::create_dir_all(temp.path().join(".claude")).unwrap();
        ensure(temp.path());
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
        ensure(temp.path());
        ensure(temp.path());
        let settings = fs::read_to_string(dir.join("settings.json")).unwrap();
        assert!(settings.contains("permissions"));
        assert!(settings.contains("statusline_claude.sh"));
    }

    #[test]
    fn leaves_a_custom_statusline_alone() {
        let temp = tempfile::tempdir().unwrap();
        let dir = temp.path().join(".claude");
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("settings.json"),
            r#"{"statusLine":{"type":"command","command":"/tmp/mine.sh"}}"#,
        )
        .unwrap();
        ensure(temp.path());
        let settings = fs::read_to_string(dir.join("settings.json")).unwrap();
        assert!(settings.contains("/tmp/mine.sh"));
        assert!(!settings.contains("statusline_claude.sh"));
    }
}

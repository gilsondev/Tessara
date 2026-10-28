//! Claude account usage. The statusLine file is one source; this is another.
//! The OAuth token stays in the Keychain and is never written to disk or the log.

use std::fs;
use std::path::Path;
use std::process::Command;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde_json::Value;

const USAGE_URL: &str = "https://api.anthropic.com/api/oauth/usage";
const CACHE_FRESH_S: f64 = 180.0;

#[derive(Debug, Clone, PartialEq)]
pub struct UsageSample {
    pub percent_used: f64,
    pub reset_label: String,
}

pub fn usage_from_body(body: &str) -> Option<UsageSample> {
    let value: Value = serde_json::from_str(body).ok()?;
    let window = value
        .get("five_hour")
        .or_else(|| value.get("seven_day"))
        .or_else(|| value.pointer("/rate_limits/five_hour"))
        .or_else(|| value.pointer("/rate_limits/seven_day"))?;
    let used = window
        .get("utilization")
        .or_else(|| window.get("used_percentage"))
        .and_then(|v| v.as_f64().or_else(|| v.as_i64().map(|n| n as f64)))?;
    if !used.is_finite() || used < 0.0 {
        return None;
    }
    let resets = window
        .get("resets_at")
        .and_then(|v| v.as_str())
        .unwrap_or("");
    Some(UsageSample {
        percent_used: used.clamp(0.0, 100.0),
        reset_label: reset_label_iso(resets),
    })
}

fn reset_label_iso(resets_at: &str) -> String {
    let Ok(dt) = chrono::DateTime::parse_from_rfc3339(resets_at) else {
        return String::new();
    };
    dt.with_timezone(&chrono::Local)
        .format("%H:%M")
        .to_string()
}

fn cache_is_fresh(path: &Path) -> bool {
    let Ok(raw) = fs::read_to_string(path) else {
        return false;
    };
    let Ok(value) = serde_json::from_str::<Value>(&raw) else {
        return false;
    };
    let Some(generated) = value.get("generated_at").and_then(|v| v.as_f64()) else {
        return false;
    };
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_secs_f64();
    (now - generated).abs() < CACHE_FRESH_S
}

fn access_token() -> Option<String> {
    let output = tessera_proc::command_output_timeout(
        Command::new("security").args([
            "find-generic-password",
            "-s",
            "Claude Code-credentials",
            "-w",
        ]),
        Duration::from_secs(20),
    )?;
    if !output.status.success() {
        return None;
    }
    let raw = String::from_utf8(output.stdout).ok()?;
    let value: Value = serde_json::from_str(raw.trim()).ok()?;
    let token = value
        .pointer("/claudeAiOauth/accessToken")
        .or_else(|| value.get("accessToken"))
        .and_then(|v| v.as_str())
        .filter(|t| !t.is_empty())?;
    Some(token.to_string())
}

/// Writes `claude_usage_cache.json` when the account answers. An error leaves the previous file.
pub fn refresh_usage_cache(home: &Path) {
    let path = home.join(".tessera/claude_usage_cache.json");
    if cache_is_fresh(&path) {
        return;
    }
    let Some(token) = access_token() else {
        report_usage("sem token");
        return;
    };
    let agent = ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(2))
        .timeout(Duration::from_secs(3))
        .build();
    let response = agent
        .get(USAGE_URL)
        .set("Authorization", &format!("Bearer {token}"))
        .set("anthropic-beta", "oauth-2025-04-20")
        .call();
    drop(token);
    let response = match response {
        Ok(response) => response,
        Err(ureq::Error::Status(code, _)) => {
            report_usage(&format!("HTTP {code}"));
            return;
        }
        Err(_) => {
            report_usage("rede");
            return;
        }
    };
    let Ok(body) = response.into_string() else {
        report_usage("resposta vazia");
        return;
    };
    let Some(sample) = usage_from_body(&body) else {
        report_usage("resposta sem percentual");
        return;
    };
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_secs_f64();
    let doc = serde_json::json!({
        "generated_at": now,
        "percent_used": sample.percent_used,
        "reset_label": sample.reset_label,
        "source": "claude_oauth",
    });
    if let Some(dir) = path.parent() {
        let _ = fs::create_dir_all(dir);
    }
    if tessera_proc::write_atomic(&path, doc.to_string()).is_ok() {
        report_usage("");
    }
}

/// One log line per state change. Never includes the token or the Authorization header.
fn report_usage(reason: &str) {
    use std::sync::Mutex;
    static LAST: Mutex<String> = Mutex::new(String::new());
    let mut last = LAST.lock().unwrap_or_else(|poison| poison.into_inner());
    if *last == reason {
        return;
    }
    if reason.is_empty() {
        if !last.is_empty() {
            eprintln!("claude: uso atualizado");
        }
    } else {
        eprintln!("claude: uso indisponível ({reason})");
    }
    *last = reason.to_string();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_account_usage() {
        let sample = usage_from_body(
            r#"{"five_hour":{"utilization":37.0,"resets_at":"2026-10-28T01:00:00Z"},"seven_day":{"utilization":26.0}}"#,
        )
        .unwrap();
        assert_eq!(sample.percent_used, 37.0);
        assert!(!sample.reset_label.is_empty());
    }

    #[test]
    fn parses_statusline_shaped_body() {
        let sample = usage_from_body(
            r#"{"rate_limits":{"five_hour":{"used_percentage":42}}}"#,
        )
        .unwrap();
        assert_eq!(sample.percent_used, 42.0);
    }
}

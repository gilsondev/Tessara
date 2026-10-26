//! Codex / ChatGPT rate limits via local auth → wham/usage.

use std::fs;
use std::path::Path;
use std::time::Duration;

use chrono::{Local, TimeZone, Timelike};
use serde_json::Value;

use crate::{AiTokenSource, SourceKind};

const USAGE_URL: &str = "https://chatgpt.com/backend-api/wham/usage";

fn remaining(used: Option<f64>) -> Option<i32> {
    let used = used?;
    Some((100.0 - used).round().clamp(0.0, 100.0) as i32)
}

fn reset_label(reset_at: Option<f64>) -> String {
    let Some(ts) = reset_at.filter(|t| *t > 0.0) else {
        return String::new();
    };
    let Some(dt) = Local.timestamp_opt(ts as i64, 0).single() else {
        return String::new();
    };
    format!("{:02}:{:02}", dt.hour(), dt.minute())
}

/// Reads Codex auth and fetches live WHAM usage when possible.
pub fn read_codex(home: &Path) -> AiTokenSource {
    let path = home.join(".codex/auth.json");
    let Ok(raw) = fs::read_to_string(path) else {
        return AiTokenSource::unavailable("codex");
    };
    let Ok(json) = serde_json::from_str::<Value>(&raw) else {
        return AiTokenSource::unavailable("codex");
    };
    let tokens = json.get("tokens").cloned().unwrap_or(Value::Null);
    let Some(access) = tokens
        .get("access_token")
        .and_then(|v| v.as_str())
        .filter(|t| !t.is_empty())
    else {
        return AiTokenSource::unavailable("codex");
    };
    let account = tokens
        .get("account_id")
        .and_then(|v| v.as_str())
        .unwrap_or("");

    let mut req = ureq::get(USAGE_URL)
        .set("Authorization", &format!("Bearer {access}"))
        .set("User-Agent", "TesseraAiTokens/1.0")
        .timeout(Duration::from_secs(12));
    if !account.is_empty() {
        req = req.set("ChatGPT-Account-Id", account);
    }
    let Ok(resp) = req.call() else {
        return AiTokenSource::unavailable("codex");
    };
    let Ok(data) = resp.into_json::<Value>() else {
        return AiTokenSource::unavailable("codex");
    };

    let rate = data.get("rate_limit").cloned().unwrap_or(Value::Null);
    let primary = rate.get("primary_window").cloned().unwrap_or(Value::Null);
    let secondary = rate
        .get("secondary_window")
        .cloned()
        .unwrap_or(Value::Null);
    let used5 = primary
        .get("used_percent")
        .and_then(|v| v.as_f64().or_else(|| v.as_i64().map(|n| n as f64)));
    let used_week = secondary
        .get("used_percent")
        .and_then(|v| v.as_f64().or_else(|| v.as_i64().map(|n| n as f64)));
    let mut rem5 = remaining(used5);
    let mut rem_week = remaining(used_week);
    if rem5.is_none() && rem_week.is_none() {
        return AiTokenSource::unavailable("codex");
    }
    if rem5.is_none() {
        rem5 = rem_week;
    }
    if rem_week.is_none() {
        rem_week = rem5;
    }
    let rem5 = rem5.unwrap_or(0);
    let rem_week = rem_week.unwrap_or(rem5);
    let reset_at = primary
        .get("reset_at")
        .or_else(|| secondary.get("reset_at"))
        .and_then(|v| v.as_f64().or_else(|| v.as_i64().map(|n| n as f64)));
    AiTokenSource::available(
        "codex",
        SourceKind::Pct,
        rem5,
        format!("{rem5}% · {rem_week}%"),
        reset_label(reset_at),
    )
}

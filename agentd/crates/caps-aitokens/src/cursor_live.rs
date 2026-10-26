//! Live Cursor plan usage (`GetCurrentPeriodUsage` / `GetPlanInfo`).

use std::time::{Duration, SystemTime, UNIX_EPOCH};

use chrono::{Datelike, Local, TimeZone};
use serde_json::Value;

use crate::{AiTokenSource, CursorSession, CursorSource, SourceKind};

const USAGE_URL: &str = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetCurrentPeriodUsage";
const PLAN_URL: &str = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetPlanInfo";
const OAUTH_URL: &str = "https://api2.cursor.sh/oauth/token";
const EXCHANGE_URL: &str = "https://api2.cursor.sh/auth/exchange_user_api_key";
const OAUTH_CLIENT_ID: &str = "KbZUR41cY7W6zRSdpSUJ7I7mLYBKOCmB";
const REFRESH_SKEW_S: u64 = 300;

const MONTHS: [&str; 12] = [
    "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez",
];

fn now_s() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_secs_f64()
}

fn source_label(source: CursorSource) -> &'static str {
    match source {
        CursorSource::StateDb => "Cursor",
        CursorSource::Keychain => "Keychain",
        CursorSource::AuthJson => "auth.json",
        CursorSource::TokenEnv => "CURSOR_TOKEN",
        CursorSource::ApiKeyEnv => "CURSOR_API_KEY",
    }
}

fn b64url_decode(input: &str) -> Option<Vec<u8>> {
    let mut s = input.replace('-', "+").replace('_', "/");
    while s.len() % 4 != 0 {
        s.push('=');
    }
    fn val(c: u8) -> Option<u8> {
        match c {
            b'A'..=b'Z' => Some(c - b'A'),
            b'a'..=b'z' => Some(c - b'a' + 26),
            b'0'..=b'9' => Some(c - b'0' + 52),
            b'+' => Some(62),
            b'/' => Some(63),
            _ => None,
        }
    }
    let bytes = s.as_bytes();
    let mut out = Vec::with_capacity(bytes.len() * 3 / 4);
    let mut i = 0;
    while i + 3 < bytes.len() {
        let a = val(bytes[i])?;
        let b = val(bytes[i + 1])?;
        let pad_c = bytes[i + 2] == b'=';
        let pad_d = bytes[i + 3] == b'=';
        let c = if pad_c { 0 } else { val(bytes[i + 2])? };
        let d = if pad_d { 0 } else { val(bytes[i + 3])? };
        out.push((a << 2) | (b >> 4));
        if !pad_c {
            out.push((b << 4) | (c >> 2));
        }
        if !pad_d {
            out.push((c << 6) | d);
        }
        i += 4;
    }
    Some(out)
}

fn jwt_exp(token: &str) -> Option<f64> {
    let payload = token.split('.').nth(1)?;
    let raw = b64url_decode(payload)?;
    let claims: Value = serde_json::from_slice(&raw).ok()?;
    claims
        .get("exp")?
        .as_f64()
        .or_else(|| claims.get("exp")?.as_u64().map(|v| v as f64))
}

fn token_needs_refresh(token: &str) -> bool {
    match jwt_exp(token) {
        Some(exp) => now_s() >= (exp - REFRESH_SKEW_S as f64),
        None => false,
    }
}

fn refresh_access(session: &CursorSession) -> Option<(String, Option<String>)> {
    let refresh = session.refresh.as_deref()?;
    let body = serde_json::json!({
        "grant_type": "refresh_token",
        "client_id": OAUTH_CLIENT_ID,
        "refresh_token": refresh,
    });
    let resp = ureq::post(OAUTH_URL)
        .set("Content-Type", "application/json")
        .timeout(Duration::from_secs(12))
        .send_json(body)
        .ok()?;
    let data: Value = resp.into_json().ok()?;
    if data.get("shouldLogout").and_then(|v| v.as_bool()) == Some(true) {
        return None;
    }
    let access = data
        .get("access_token")
        .or_else(|| data.get("accessToken"))
        .and_then(|v| v.as_str())?
        .to_string();
    let new_refresh = data
        .get("refresh_token")
        .or_else(|| data.get("refreshToken"))
        .and_then(|v| v.as_str())
        .map(str::to_string);
    Some((access, new_refresh))
}

pub fn exchange_api_key(api_key: &str) -> Option<CursorSession> {
    let resp = ureq::post(EXCHANGE_URL)
        .set("Authorization", &format!("Bearer {api_key}"))
        .set("Content-Type", "application/json")
        .timeout(Duration::from_secs(12))
        .send_string("{}")
        .ok()?;
    let data: Value = resp.into_json().ok()?;
    let access = data
        .get("accessToken")
        .or_else(|| data.get("access_token"))
        .and_then(|v| v.as_str())?
        .to_string();
    let refresh = data
        .get("refreshToken")
        .or_else(|| data.get("refresh_token"))
        .and_then(|v| v.as_str())
        .map(str::to_string);
    Some(CursorSession {
        access: Some(access),
        refresh,
        source: CursorSource::ApiKeyEnv,
    })
}

fn bearer(session: &mut CursorSession) -> Option<String> {
    if let Some(access) = session.access.clone() {
        if !token_needs_refresh(&access) {
            return Some(access);
        }
    }
    if let Some((access, new_refresh)) = refresh_access(session) {
        session.access = Some(access.clone());
        if let Some(r) = new_refresh {
            session.refresh = Some(r);
        }
        eprintln!("Tessera: cursor token renovado via {}", source_label(session.source));
        return Some(access);
    }
    session.access.clone()
}

fn post_json(url: &str, token: &str) -> Result<Value, Option<u16>> {
    match ureq::post(url)
        .set("Authorization", &format!("Bearer {token}"))
        .set("Content-Type", "application/json")
        .set("Connect-Protocol-Version", "1")
        .timeout(Duration::from_secs(12))
        .send_json(serde_json::json!({}))
    {
        Ok(resp) => resp.into_json().map_err(|_| None),
        Err(ureq::Error::Status(code, resp)) => {
            let _ = resp.into_string();
            Err(Some(code))
        }
        Err(_) => Err(None),
    }
}

fn post_session(url: &str, session: &mut CursorSession) -> Option<Value> {
    let token = bearer(session)?;
    match post_json(url, &token) {
        Ok(data) => Some(data),
        Err(Some(401)) => {
            let (access, new_refresh) = refresh_access(session)?;
            session.access = Some(access.clone());
            if let Some(r) = new_refresh {
                session.refresh = Some(r);
            }
            post_json(url, &access).ok()
        }
        Err(_) => None,
    }
}

fn used_pct(raw: &Value) -> Option<i32> {
    let n = raw
        .as_f64()
        .or_else(|| raw.as_i64().map(|v| v as f64))
        .or_else(|| raw.as_u64().map(|v| v as f64))?;
    Some(n.round().clamp(0.0, 100.0) as i32)
}

fn cycle_ts(ms: &Value) -> Option<i64> {
    let value = ms
        .as_f64()
        .or_else(|| ms.as_i64().map(|v| v as f64))
        .or_else(|| ms.as_u64().map(|v| v as f64))?;
    let secs = if value > 1e12 { value / 1000.0 } else { value };
    Some(secs as i64)
}

fn cycle_reset_label(ms: Option<&Value>, period: Option<&str>) -> String {
    let Some(ms) = ms else {
        return String::new();
    };
    let Some(secs) = cycle_ts(ms) else {
        return String::new();
    };
    let Some(dt) = Local.timestamp_opt(secs, 0).single() else {
        return String::new();
    };
    let day = dt.day();
    let mon = MONTHS[(dt.month0() as usize).min(11)];
    if period.is_some_and(|p| p.to_ascii_uppercase().contains("WEEKLY")) {
        return format!("{day:02} {mon}");
    }
    format!("{day:02} {mon}")
}

/// Fetch live Cursor plan usage for a resolved session.
pub fn fetch_usage(mut session: CursorSession) -> AiTokenSource {
    let label = source_label(session.source);
    let Some(usage) = post_session(USAGE_URL, &mut session) else {
        let mut out = AiTokenSource::unavailable("cursor");
        out.display = format!("{label}: 401/refresh falhou");
        return out;
    };
    let plan = usage.get("planUsage").cloned().unwrap_or(Value::Null);
    let auto = plan.get("autoPercentUsed").and_then(used_pct);
    let api = plan.get("apiPercentUsed").and_then(used_pct);
    // Prefer both pools; if the API omits one, treat as 0 rather than dropping the whole source.
    let (auto_used, api_used) = match (auto, api) {
        (None, None) => {
            let mut out = AiTokenSource::unavailable("cursor");
            out.display = format!("{label}: sem planUsage");
            return out;
        }
        (a, b) => (a.unwrap_or(0), b.unwrap_or(0)),
    };
    let rem = (100 - auto_used.max(api_used)).max(0);
    let mut cycle_end = usage.get("billingCycleEnd").cloned();
    let mut period: Option<String> = None;
    if let Some(plan_info) = post_session(PLAN_URL, &mut session) {
        if let Some(info) = plan_info.get("planInfo") {
            period = info
                .get("includedUsagePeriod")
                .and_then(|v| v.as_str())
                .map(str::to_string);
            if let Some(v) = info.get("billingCycleEnd") {
                cycle_end = Some(v.clone());
            }
            if let Some(v) = info.get("includedUsageResetsAt") {
                cycle_end = Some(v.clone());
            }
        }
    }
    AiTokenSource::available(
        "cursor",
        SourceKind::Pct,
        rem,
        format!("{auto_used}% · {api_used}%"),
        cycle_reset_label(cycle_end.as_ref(), period.as_deref()),
    )
}

//! Live Cursor plan usage (`GetCurrentPeriodUsage` / `GetPlanInfo`).

use std::time::{Duration, SystemTime, UNIX_EPOCH};

use chrono::{Datelike, Local, TimeZone};
use serde_json::Value;

use crate::{AiTokenSource, CursorSession, CursorSource, SourceKind};

const USAGE_URL: &str = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetCurrentPeriodUsage";
const PLAN_URL: &str = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetPlanInfo";
const EXCHANGE_URL: &str = "https://api2.cursor.sh/auth/exchange_user_api_key";
const REFRESH_SKEW_S: u64 = 300;
const OPEN_CURSOR: &str = "abra Cursor";

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
    while !s.len().is_multiple_of(4) {
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

/// Access token kept by the Cursor app. An expired token is not renewed here.
fn usable_access(session: &CursorSession) -> Option<String> {
    let access = session.access.as_deref()?;
    if token_needs_refresh(access) {
        return None;
    }
    Some(access.to_string())
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

fn post_session(
    url: &str,
    session: &CursorSession,
    post: &mut impl FnMut(&str, &str) -> Option<Value>,
) -> Option<Value> {
    let token = usable_access(session)?;
    post(url, &token)
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
pub fn fetch_usage(session: CursorSession) -> AiTokenSource {
    fetch_usage_with(session, |url, token| post_json(url, token).ok())
}

fn fetch_usage_with(
    session: CursorSession,
    mut post: impl FnMut(&str, &str) -> Option<Value>,
) -> AiTokenSource {
    let label = source_label(session.source);
    if usable_access(&session).is_none() {
        let mut out = AiTokenSource::unavailable("cursor");
        out.display = OPEN_CURSOR.into();
        return out;
    }
    let Some(usage) = post_session(USAGE_URL, &session, &mut post) else {
        let mut out = AiTokenSource::unavailable("cursor");
        out.display = format!("{label}: indisponível");
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
    if let Some(plan_info) = post_session(PLAN_URL, &session, &mut post) {
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

#[cfg(test)]
mod tests {
    use super::*;

    fn b64url(data: &[u8]) -> String {
        const ALPHA: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        let mut out = String::new();
        let mut i = 0;
        while i < data.len() {
            let b0 = data[i];
            let b1 = if i + 1 < data.len() { data[i + 1] } else { 0 };
            let b2 = if i + 2 < data.len() { data[i + 2] } else { 0 };
            out.push(ALPHA[(b0 >> 2) as usize] as char);
            out.push(ALPHA[(((b0 & 0x03) << 4) | (b1 >> 4)) as usize] as char);
            if i + 1 < data.len() {
                out.push(ALPHA[(((b1 & 0x0f) << 2) | (b2 >> 6)) as usize] as char);
            }
            if i + 2 < data.len() {
                out.push(ALPHA[(b2 & 0x3f) as usize] as char);
            }
            i += 3;
        }
        out
    }

    fn jwt_exp(exp: i64) -> String {
        let header = b64url(br#"{"alg":"none"}"#);
        let payload = b64url(format!(r#"{{"exp":{exp}}}"#).as_bytes());
        format!("{header}.{payload}.")
    }

    #[test]
    fn expired_cursor_token_is_not_refreshed() {
        let session = CursorSession {
            access: Some(jwt_exp(1)),
            refresh: Some("refresh-token".into()),
            source: CursorSource::StateDb,
        };
        let mut urls = Vec::new();
        let source = fetch_usage_with(session, |url, _token| {
            urls.push(url.to_string());
            None
        });
        assert!(urls.is_empty(), "urls={urls:?}");
        assert!(!source.available);
        assert_eq!(source.display, "abra Cursor");
    }

    #[test]
    fn live_cursor_token_never_posts_oauth() {
        let session = CursorSession {
            access: Some(jwt_exp(4_102_444_800)),
            refresh: Some("refresh-token".into()),
            source: CursorSource::AuthJson,
        };
        let mut urls = Vec::new();
        let source = fetch_usage_with(session, |url, _token| {
            urls.push(url.to_string());
            if url.ends_with("GetCurrentPeriodUsage") {
                Some(serde_json::json!({
                    "planUsage": {"autoPercentUsed": 3, "apiPercentUsed": 13}
                }))
            } else {
                Some(serde_json::json!({"planInfo": {}}))
            }
        });
        assert!(urls.iter().all(|url| !url.contains("/oauth/token")), "urls={urls:?}");
        assert_eq!(urls.len(), 2);
        assert!(source.available);
        assert_eq!(source.display, "3% · 13%");
    }
}

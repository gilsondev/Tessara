//! Proxy to the panel Wi‑Fi portal (`/api/state`, `/api/settings`, `/api/refresh`).
//! Device day-to-day settings are not on the USB agent protocol — they live on the portal.
//!
//! Uses `/usr/bin/curl` because the ESP32 HTTP stack is unreliable with reqwest/rustls
//! (POST often fails while GET works).

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::process::Command;
use std::time::Duration;

pub fn panel_base_url() -> String {
    std::env::var("TESSERA_PANEL_URL").unwrap_or_else(|_| "http://tessera.local".into())
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PanelSettings {
    pub portrait: bool,
    pub sound: bool,
    pub appearance: u8,
    pub brightness: u8,
    pub rest_minutes: u8,
    pub clock_minutes: u8,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PanelInfo {
    pub host: String,
    pub ip: String,
    pub online: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PanelWidget {
    pub id: String,
    pub title: String,
    pub shown: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PanelState {
    pub ok: bool,
    pub reachable: bool,
    pub settings: Option<PanelSettings>,
    pub info: Option<PanelInfo>,
    /// Full portal `settings` object (includes show_aitokens_* etc.).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub device_settings: Option<Value>,
    /// Portal `widget_settings` map (home_tile_size, etc.).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub widget_settings: Option<Value>,
    /// Home layout: which widgets are on the panel (`/api/state` → `widgets`).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub widgets: Option<Vec<PanelWidget>>,
    /// Portal weather snapshot (city, ready, …).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub weather: Option<Value>,
    /// Portal calendar snapshot (accounts, selection, …).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub calendar: Option<Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub token_set: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

fn map_widgets(v: &Value) -> Option<Vec<PanelWidget>> {
    let arr = v.get("widgets")?.as_array()?;
    let mut out = Vec::with_capacity(arr.len());
    for item in arr {
        let id = item.get("id")?.as_str()?.to_string();
        if id.is_empty() {
            continue;
        }
        let title = item
            .get("title")
            .and_then(|t| t.as_str())
            .unwrap_or(id.as_str())
            .to_string();
        let shown = item.get("shown").and_then(|s| s.as_bool()).unwrap_or(false);
        out.push(PanelWidget { id, title, shown });
    }
    Some(out)
}

fn panel_url_cache_path() -> std::path::PathBuf {
    std::env::var_os("HOME")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::PathBuf::from("."))
        .join(".tessera/agentd/panel_base_url")
}

fn resolve_base(url: &str) -> String {
    let Ok(parsed) = url::Url::parse(url) else {
        return url.to_string();
    };
    let Some(host) = parsed.host_str() else {
        return url.to_string();
    };
    if host.parse::<std::net::Ipv4Addr>().is_ok() {
        return url.to_string();
    }
    let ip = tessera_proc::command_output_timeout(
        Command::new("/sbin/ping").args(["-c", "1", "-t", "2", host]),
        Duration::from_secs(4),
    )
    .and_then(|o| {
        let text = String::from_utf8_lossy(&o.stdout);
        let start = text.find('(')?;
        let end = text[start + 1..].find(')')? + start + 1;
        let inside = text[start + 1..end].to_string();
        if inside.parse::<std::net::Ipv4Addr>().is_ok() {
            Some(inside)
        } else {
            None
        }
    });
    let Some(ip) = ip else {
        return url.to_string();
    };
    let port = parsed.port().map(|p| format!(":{p}")).unwrap_or_default();
    format!("{}://{ip}{port}", parsed.scheme())
}

fn candidate_bases() -> Vec<String> {
    let raw = panel_base_url();
    let mut out = Vec::new();
    if let Ok(cached) = std::fs::read_to_string(panel_url_cache_path()) {
        let c = cached.trim();
        if !c.is_empty() {
            out.push(c.to_string());
        }
    }
    let resolved = resolve_base(&raw);
    if !out.iter().any(|u| u == &resolved) {
        out.push(resolved.clone());
    }
    if resolved != raw && !out.iter().any(|u| u == &raw) {
        out.push(raw);
    }
    out
}

fn remember_base(base: &str) {
    if let Some(parent) = panel_url_cache_path().parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let _ = std::fs::write(panel_url_cache_path(), format!("{base}\n"));
}

fn map_settings(v: &Value) -> Option<PanelSettings> {
    let s = v.get("settings")?;
    Some(PanelSettings {
        portrait: s.get("portrait")?.as_bool()?,
        sound: s.get("sound")?.as_bool()?,
        appearance: s.get("appearance")?.as_u64()? as u8,
        brightness: s.get("brightness")?.as_u64()? as u8,
        rest_minutes: s.get("rest_minutes")?.as_u64()? as u8,
        clock_minutes: s.get("clock_minutes")?.as_u64()? as u8,
    })
}

fn map_info(v: &Value) -> Option<PanelInfo> {
    let i = v.get("info")?;
    Some(PanelInfo {
        host: i.get("host")?.as_str()?.to_string(),
        ip: i.get("ip")?.as_str()?.to_string(),
        online: i.get("online")?.as_bool()?,
    })
}

fn curl_get(url: &str) -> Result<String, String> {
    let out = tessera_proc::command_output_timeout(
        Command::new("/usr/bin/curl").args([
            "-fsS",
            "--max-time",
            "8",
            "-H",
            "Accept: application/json",
            url,
        ]),
        Duration::from_secs(12),
    )
    .ok_or_else(|| "curl excedeu o tempo".to_string())?;
    if !out.status.success() {
        let err = String::from_utf8_lossy(&out.stderr);
        return Err(if err.trim().is_empty() {
            format!("curl exit {}", out.status)
        } else {
            err.trim().to_string()
        });
    }
    String::from_utf8(out.stdout).map_err(|e| e.to_string())
}

fn curl_post_json(url: &str, body: &str) -> Result<String, String> {
    let out = tessera_proc::command_output_timeout(
        Command::new("/usr/bin/curl").args([
            "-fsS",
            "--max-time",
            "8",
            "-X",
            "POST",
            "-H",
            "Content-Type: application/json",
            "-H",
            "Accept: application/json",
            "--data-binary",
            body,
            url,
        ]),
        Duration::from_secs(12),
    )
    .ok_or_else(|| "curl excedeu o tempo".to_string())?;
    if !out.status.success() {
        let err = String::from_utf8_lossy(&out.stderr);
        return Err(if err.trim().is_empty() {
            format!("curl exit {}", out.status)
        } else {
            err.trim().to_string()
        });
    }
    String::from_utf8(out.stdout).map_err(|e| e.to_string())
}

pub async fn fetch_state() -> PanelState {
    tokio::task::spawn_blocking(|| {
        let mut last_err = String::from("painel inacessível");
        for base in candidate_bases() {
            match curl_get(&format!("{base}/api/state")) {
                Ok(raw) => match serde_json::from_str::<Value>(&raw) {
                    Ok(v) => {
                        remember_base(&base);
                        return PanelState {
                            ok: true,
                            reachable: true,
                            settings: map_settings(&v),
                            info: map_info(&v),
                            device_settings: v.get("settings").cloned(),
                            widget_settings: v.get("widget_settings").cloned(),
                            widgets: map_widgets(&v),
                            weather: v.get("weather").cloned(),
                            calendar: v.get("calendar").cloned(),
                            token_set: v.get("token_set").and_then(|t| t.as_bool()),
                            error: None,
                        };
                    }
                    Err(e) => last_err = format!("json: {e}"),
                },
                Err(e) => last_err = e,
            }
        }
        PanelState {
            ok: false,
            reachable: false,
            settings: None,
            info: None,
            device_settings: None,
            widget_settings: None,
            widgets: None,
            weather: None,
            calendar: None,
            token_set: None,
            error: Some(last_err),
        }
    })
    .await
    .unwrap_or_else(|e| PanelState {
        ok: false,
        reachable: false,
        settings: None,
        info: None,
        device_settings: None,
        widget_settings: None,
        widgets: None,
        weather: None,
        calendar: None,
        token_set: None,
        error: Some(e.to_string()),
    })
}

async fn post_json(path: &str, body: &Value) -> Result<(), String> {
    let body = body.to_string();
    let path = path.to_string();
    tokio::task::spawn_blocking(move || {
        let mut last_err = String::from("painel inacessível");
        for base in candidate_bases() {
            match curl_post_json(&format!("{base}{path}"), &body) {
                Ok(_) => {
                    remember_base(&base);
                    return Ok(());
                }
                Err(e) => last_err = e,
            }
        }
        Err(last_err)
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Patch portal settings (partial JSON). Returns refreshed state.
pub async fn patch_settings(patch: Value) -> Result<PanelState, String> {
    post_json("/api/settings", &patch).await?;
    tokio::time::sleep(Duration::from_millis(350)).await;
    Ok(fetch_state().await)
}

/// Namespaced widget setting → portal `/api/widget_settings`.
/// Body: `{ "id": "org.tessera.calendar", "key": "home_tile_size", "value": "L" }`.
pub async fn post_widget_settings(body: Value) -> Result<PanelState, String> {
    post_json("/api/widget_settings", &body).await?;
    tokio::time::sleep(Duration::from_millis(350)).await;
    Ok(fetch_state().await)
}

/// Home layout (show/hide widgets) → portal `/api/dashboard`.
/// Body: `{ "widgets": [{ "id", "title"?, "shown" }] }`.
pub async fn post_dashboard(body: Value) -> Result<PanelState, String> {
    post_json("/api/dashboard", &body).await?;
    tokio::time::sleep(Duration::from_millis(350)).await;
    Ok(fetch_state().await)
}

pub async fn refresh_panel() -> Result<(), String> {
    post_json("/api/refresh", &json!({})).await
}

/// Todoist API token → portal `/api/token`.
pub async fn post_token(body: Value) -> Result<PanelState, String> {
    post_json("/api/token", &body).await?;
    tokio::time::sleep(Duration::from_millis(350)).await;
    Ok(fetch_state().await)
}

/// Weather place → portal `/api/weather`.
pub async fn post_weather(body: Value) -> Result<PanelState, String> {
    post_json("/api/weather", &body).await?;
    tokio::time::sleep(Duration::from_millis(450)).await;
    Ok(fetch_state().await)
}

/// Calendar selection CSV → portal `/api/calendar/selection`.
pub async fn post_calendar_selection(body: Value) -> Result<PanelState, String> {
    post_json("/api/calendar/selection", &body).await?;
    tokio::time::sleep(Duration::from_millis(350)).await;
    Ok(fetch_state().await)
}

/// Start Google account OAuth on the Mac → portal `/api/calendar/add_account`.
pub async fn post_calendar_add_account() -> Result<PanelState, String> {
    post_json("/api/calendar/add_account", &json!({})).await?;
    tokio::time::sleep(Duration::from_millis(350)).await;
    Ok(fetch_state().await)
}

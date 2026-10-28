//! Google Calendar fetch — refresh token + events for USB snapshot.
//! Tokens stay on the Mac under `~/.tessera/calendar/accounts/`.
//! The agent reads the Mac Calendar database instead. This module stays for the
//! old account files and is not on the collection path.
#![allow(dead_code)]

use std::fs;
use std::path::{Path, PathBuf};

use chrono::{DateTime, Duration, Local, NaiveDate, TimeZone, Timelike, Utc};
use serde::{Deserialize, Serialize};
use serde_json::Value;

use crate::{CalendarEvent, CalendarPaths, EVENT_LIMIT};

#[derive(Debug, Deserialize, Serialize)]
struct AccountFile {
    #[serde(default)]
    account: String,
    client_id: String,
    client_secret: String,
    refresh_token: String,
    token: String,
    token_uri: String,
    #[serde(default)]
    expiry: String,
    #[serde(flatten)]
    extra: Value,
}

#[derive(Debug, Serialize)]
struct EventsCache {
    fetched_at: String,
    events: Vec<CachedEvent>,
}

#[derive(Debug, Serialize)]
struct CachedEvent {
    start_hhmm: String,
    end_hhmm: String,
    all_day: bool,
    title: String,
    location: String,
    duration_min: u32,
    color: String,
    day_offset: u8,
}

fn first_account_path(dir: &Path) -> Option<PathBuf> {
    let mut files: Vec<_> = fs::read_dir(dir)
        .ok()?
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|ext| ext == "json"))
        .collect();
    files.sort();
    files.into_iter().next()
}

fn load_account(path: &Path) -> Option<AccountFile> {
    let raw = fs::read_to_string(path).ok()?;
    serde_json::from_str(&raw).ok()
}

fn save_account(path: &Path, acc: &AccountFile) {
    if let Ok(raw) = serde_json::to_string_pretty(acc) {
        let _ = fs::write(path, raw);
    }
}

fn pct_encode(value: &str) -> String {
    let mut out = String::with_capacity(value.len() * 3);
    for b in value.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char);
            }
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

fn refresh_access_token(acc: &mut AccountFile) -> Result<(), String> {
    let form = [
        ("client_id", acc.client_id.as_str()),
        ("client_secret", acc.client_secret.as_str()),
        ("refresh_token", acc.refresh_token.as_str()),
        ("grant_type", "refresh_token"),
    ];
    let resp = ureq::post(&acc.token_uri)
        .timeout(std::time::Duration::from_secs(20))
        .send_form(&form)
        .map_err(|e| match e {
            ureq::Error::Status(_, resp) => {
                let body = resp.into_string().unwrap_or_default();
                if login_expired(&body) {
                    format!("login_expired: {body}")
                } else {
                    format!("token refresh: {body}")
                }
            }
            other => format!("token refresh: {other}"),
        })?;
    let body: Value = resp
        .into_json()
        .map_err(|e| format!("token json: {e}"))?;
    let token = body
        .get("access_token")
        .and_then(|v| v.as_str())
        .ok_or_else(|| "token refresh missing access_token".to_string())?;
    acc.token = token.to_string();
    if let Some(secs) = body.get("expires_in").and_then(|v| v.as_u64()) {
        let exp = Utc::now() + Duration::seconds(secs as i64 - 60);
        acc.expiry = exp.to_rfc3339();
    }
    Ok(())
}

pub(crate) fn login_expired(err: &str) -> bool {
    let lower = err.to_ascii_lowercase();
    lower.contains("invalid_grant") || lower.contains("login_expired")
}

fn token_probably_valid(acc: &AccountFile) -> bool {
    if acc.token.is_empty() {
        return false;
    }
    let Ok(exp) = DateTime::parse_from_rfc3339(&acc.expiry) else {
        return false;
    };
    exp.with_timezone(&Utc) > Utc::now() + Duration::seconds(60)
}

fn ensure_access_token(path: &Path, acc: &mut AccountFile) -> Result<(), String> {
    if token_probably_valid(acc) {
        return Ok(());
    }
    refresh_access_token(acc)?;
    save_account(path, acc);
    Ok(())
}

fn parse_offset_datetime(raw: &str) -> Option<DateTime<Local>> {
    DateTime::parse_from_rfc3339(raw)
        .ok()
        .map(|dt| dt.with_timezone(&Local))
}

fn fetch_calendar_events(
    token: &str,
    calendar_id: &str,
    time_min: &str,
    time_max: &str,
) -> Result<Vec<Value>, String> {
    let url = format!(
        "https://www.googleapis.com/calendar/v3/calendars/{}/events?timeMin={}&timeMax={}&singleEvents=true&orderBy=startTime&maxResults=12",
        pct_encode(calendar_id),
        pct_encode(time_min),
        pct_encode(time_max),
    );
    let resp = ureq::get(&url)
        .set("Authorization", &format!("Bearer {token}"))
        .timeout(std::time::Duration::from_secs(20))
        .call()
        .map_err(|e| format!("events {calendar_id}: {e}"))?;
    let body: Value = resp
        .into_json()
        .map_err(|e| format!("events json: {e}"))?;
    Ok(body
        .get("items")
        .and_then(|v| v.as_array())
        .cloned()
        .unwrap_or_default())
}

/// Days ahead the panel may show. The device clamps `day_offset` to 14 and keeps 12 events, so a
/// week is enough to always have a "próximo compromisso" without flooding the wire.
const WINDOW_DAYS: i64 = 8;

/// Today first, then the next days. Events already finished today drop out; never clamp negative
/// offsets to 0 (that made past/multi-day starts look like "hoje").
fn item_to_event(item: &Value, today: NaiveDate, now: DateTime<Local>) -> Option<CalendarEvent> {
    let title = item
        .get("summary")
        .and_then(|v| v.as_str())
        .unwrap_or("(sem título)")
        .to_string();
    let location = item
        .get("location")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    let start = item.get("start")?;
    let end = item.get("end")?;

    if let Some(date) = start.get("date").and_then(|v| v.as_str()) {
        // Google all-day: exclusive end date. Single-day "Dia todo" only — multi-day spans would
        // repeat on every card without saying anything useful.
        let start_day = NaiveDate::parse_from_str(date, "%Y-%m-%d").ok()?;
        let offset = day_offset(today, start_day)?;
        let next_day = start_day.checked_add_signed(Duration::days(1))?;
        let end_day = end
            .get("date")
            .and_then(|v| v.as_str())
            .and_then(|d| NaiveDate::parse_from_str(d, "%Y-%m-%d").ok())
            .unwrap_or(next_day);
        if end_day != next_day {
            return None;
        }
        return Some(CalendarEvent {
            start_hhmm: "0000".into(),
            end_hhmm: "0000".into(),
            all_day: true,
            title,
            location,
            duration_min: 1440,
            color: "1A73E8".into(),
            day_offset: offset,
        });
    }

    let start_raw = start.get("dateTime")?.as_str()?;
    let end_raw = end.get("dateTime")?.as_str()?;
    let s = parse_offset_datetime(start_raw)?;
    let e = parse_offset_datetime(end_raw)?;
    let offset = day_offset(today, s.date_naive())?;
    if e <= now {
        return None; // already finished
    }
    let duration_min = (e - s).num_minutes().max(1) as u32;
    Some(CalendarEvent {
        start_hhmm: format!("{:02}{:02}", s.hour(), s.minute()),
        end_hhmm: format!("{:02}{:02}", e.hour(), e.minute()),
        all_day: false,
        title,
        location,
        duration_min,
        color: "1A73E8".into(),
        day_offset: offset,
    })
}

/// Days from today to `day`, or `None` when it falls outside what the panel can show.
fn day_offset(today: NaiveDate, day: NaiveDate) -> Option<u8> {
    let days = (day - today).num_days();
    if !(0..=14).contains(&days) {
        return None;
    }
    Some(days as u8)
}

/// Refresh Google events into `events_cache.json`. Returns event count.
pub fn refresh_events_cache(paths: &CalendarPaths, selected: &[String]) -> Result<usize, String> {
    let account_path =
        first_account_path(&paths.accounts_dir()).ok_or_else(|| "sem conta".to_string())?;
    let mut acc = load_account(&account_path).ok_or_else(|| "conta inválida".to_string())?;
    ensure_access_token(&account_path, &mut acc)?;

    let now = Local::now();
    let today = now.date_naive();
    // Window = today plus the next days, so the panel still has a next appointment to show once
    // today is over.
    let last_day = today
        .checked_add_signed(Duration::days(WINDOW_DAYS))
        .unwrap_or(today);
    let day_start = Local
        .from_local_datetime(&today.and_hms_opt(0, 0, 0).unwrap_or_default())
        .single()
        .unwrap_or(now);
    let day_end = Local
        .from_local_datetime(&last_day.and_hms_opt(0, 0, 0).unwrap_or_default())
        .single()
        .unwrap_or(now + Duration::days(WINDOW_DAYS));
    let time_min = day_start.to_rfc3339();
    let time_max = day_end.to_rfc3339();

    let mut events: Vec<CalendarEvent> = Vec::new();
    for cal_id in selected.iter().take(10) {
        let items = match fetch_calendar_events(&acc.token, cal_id, &time_min, &time_max) {
            Ok(items) => items,
            Err(err) => {
                if err.contains("401") {
                    refresh_access_token(&mut acc)?;
                    save_account(&account_path, &acc);
                    fetch_calendar_events(&acc.token, cal_id, &time_min, &time_max)
                        .unwrap_or_default()
                } else {
                    continue;
                }
            }
        };
        for item in items {
            if let Some(ev) = item_to_event(&item, today, now) {
                events.push(ev);
            }
        }
    }

    // Nearest day first; inside a day, Dia todo before the timed ones.
    events.sort_by(|a, b| {
        (a.day_offset, !a.all_day, a.start_hhmm.as_str())
            .cmp(&(b.day_offset, !b.all_day, b.start_hhmm.as_str()))
    });
    events.truncate(EVENT_LIMIT);

    let cache = EventsCache {
        fetched_at: now.to_rfc3339(),
        events: events
            .iter()
            .map(|e| CachedEvent {
                start_hhmm: e.start_hhmm.clone(),
                end_hhmm: e.end_hhmm.clone(),
                all_day: e.all_day,
                title: e.title.clone(),
                location: e.location.clone(),
                duration_min: e.duration_min,
                color: e.color.clone(),
                day_offset: e.day_offset,
            })
            .collect(),
    };
    let _ = fs::create_dir_all(&paths.root);
    let raw = serde_json::to_string_pretty(&cache).map_err(|e| e.to_string())?;
    fs::write(paths.events_cache_json(), raw).map_err(|e| e.to_string())?;
    Ok(cache.events.len())
}

#[cfg(test)]
mod login_tests {
    use super::login_expired;

    #[test]
    fn flags_revoked_google_login() {
        assert!(login_expired("login_expired: invalid_grant"));
        assert!(login_expired("{\"error\":\"invalid_grant\"}"));
        assert!(!login_expired("token refresh: connection refused"));
    }
}

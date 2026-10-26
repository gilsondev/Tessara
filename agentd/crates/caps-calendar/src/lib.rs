//! CALENDAR capability — Google OAuth data stays on the Mac; only snapshots
//! and calendar catalog metadata are sent to the panel over USB.

mod fetch;

use std::fs;
use std::path::PathBuf;
use std::time::{Duration, Instant};

pub(crate) const EVENT_LIMIT: usize = 12;
const TITLE_LIMIT: usize = 47;
const LOCATION_LIMIT: usize = 31;
const STATUS_LIMIT: usize = 40;
/// How often to refresh events from Google (in-process, no external scripts).
pub const CALENDAR_REFRESH_INTERVAL_S: u64 = 120;
/// USB heartbeat for calendar snapshot (panel drops stale views).
pub const CALENDAR_PUSH_INTERVAL_S: u64 = 30;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CalendarEvent {
    pub start_hhmm: String,
    pub end_hhmm: String,
    pub all_day: bool,
    pub title: String,
    pub location: String,
    pub duration_min: u32,
    pub color: String,
    pub day_offset: u8,
}

fn wire_field(value: &str, max_chars: usize) -> String {
    value
        .chars()
        .map(|c| match c {
            '\t' | '\r' | '\n' => ' ',
            c => c,
        })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
        .chars()
        .take(max_chars)
        .collect()
}

fn hhmm(value: &str) -> String {
    let bytes = value.as_bytes();
    if bytes.len() == 4 && bytes.iter().all(u8::is_ascii_digit) {
        value.to_string()
    } else {
        "0000".to_string()
    }
}

fn color(value: &str) -> String {
    let value = value.trim().trim_start_matches('#');
    if value.len() == 6 && value.bytes().all(|c| c.is_ascii_hexdigit()) {
        value.to_ascii_uppercase()
    } else {
        "1A73E8".to_string()
    }
}

/// Encodes the legacy `CALENDAR\tv1` snapshot. Events are capped to the
/// device's fixed 12-event cache and invalid offsets are dropped.
pub fn encode_calendar_line(linked: bool, events: &[CalendarEvent]) -> String {
    let mut fields = vec![
        "CALENDAR".to_string(),
        "v1".to_string(),
        "1".to_string(),
        if linked { "1" } else { "0" }.to_string(),
    ];
    if !linked {
        return fields.join("\t");
    }
    for event in events.iter().take(EVENT_LIMIT) {
        let (start, end, all_day) = if event.all_day {
            ("0000".to_string(), "0000".to_string(), "1")
        } else {
            (hhmm(&event.start_hhmm), hhmm(&event.end_hhmm), "0")
        };
        fields.extend([
            start,
            end,
            all_day.to_string(),
            wire_field(&event.title, TITLE_LIMIT),
            wire_field(&event.location, LOCATION_LIMIT),
            event.duration_min.to_string(),
            color(&event.color),
            event.day_offset.to_string(),
        ]);
    }
    fields.join("\t")
}

pub fn calendar_line_changed(last_pushed: Option<&str>, encoded: &str) -> bool {
    last_pushed != Some(encoded)
}

/// Calendar days between the cache timestamp and today (0 when written today).
pub fn cache_age_in_days(fetched_at: &str) -> Option<i64> {
    let written = chrono::DateTime::parse_from_rfc3339(fetched_at).ok()?;
    let days = (chrono::Local::now().date_naive() - written.date_naive()).num_days();
    Some(days.max(0))
}

#[derive(Debug, Clone)]
pub struct CalendarPaths {
    pub root: PathBuf,
}

impl CalendarPaths {
    pub fn default_home() -> Self {
        let home = std::env::var_os("HOME")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("."));
        Self {
            root: home.join(".tessera/calendar"),
        }
    }

    pub fn client_json(&self) -> PathBuf {
        self.root.join("client.json")
    }

    pub fn accounts_dir(&self) -> PathBuf {
        self.root.join("accounts")
    }

    pub fn selection_json(&self) -> PathBuf {
        self.root.join("selection.json")
    }

    pub fn events_cache_json(&self) -> PathBuf {
        self.root.join("events_cache.json")
    }
}

pub struct CalendarCapability {
    paths: CalendarPaths,
    selected: Vec<String>,
    last_pushed: Option<String>,
    auth_status: String,
    last_refresh: Instant,
}

impl CalendarCapability {
    pub fn new(paths: CalendarPaths) -> Self {
        let selected = fs::read_to_string(paths.selection_json())
            .ok()
            .and_then(|raw| serde_json::from_str::<Vec<String>>(&raw).ok())
            .unwrap_or_default();
        Self {
            paths,
            selected,
            last_pushed: None,
            auth_status: String::new(),
            last_refresh: Instant::now()
                .checked_sub(Duration::from_secs(CALENDAR_REFRESH_INTERVAL_S))
                .unwrap_or_else(Instant::now),
        }
    }

    pub fn default_home() -> Self {
        Self::new(CalendarPaths::default_home())
    }

    pub fn last_pushed_line(&self) -> Option<&str> {
        self.last_pushed.as_deref()
    }

    pub fn note_pushed(&mut self, line: &str) {
        self.last_pushed = Some(line.to_string());
    }

    fn has_account(&self) -> bool {
        fs::read_dir(self.paths.accounts_dir())
            .ok()
            .is_some_and(|entries| {
                entries.flatten().any(|entry| {
                    entry
                        .path()
                        .extension()
                        .is_some_and(|extension| extension == "json")
                })
            })
    }

    fn linked(&self) -> bool {
        self.has_account()
    }

    fn load_cached_events(&self) -> Vec<CalendarEvent> {
        let Ok(raw) = fs::read_to_string(self.paths.events_cache_json()) else {
            return Vec::new();
        };
        let Ok(value) = serde_json::from_str::<serde_json::Value>(&raw) else {
            return Vec::new();
        };
        let Some(items) = value.get("events").and_then(|v| v.as_array()) else {
            return Vec::new();
        };
        // Offsets are relative to the day the cache was written. A Mac that slept past midnight
        // would otherwise label today's events as "amanhã".
        let aged_days = value
            .get("fetched_at")
            .and_then(|v| v.as_str())
            .and_then(cache_age_in_days)
            .unwrap_or(0);
        items
            .iter()
            .filter_map(|item| {
                Some(CalendarEvent {
                    start_hhmm: item.get("start_hhmm")?.as_str()?.to_string(),
                    end_hhmm: item.get("end_hhmm")?.as_str()?.to_string(),
                    all_day: item.get("all_day").and_then(|v| v.as_bool()).unwrap_or(false),
                    title: item.get("title")?.as_str()?.to_string(),
                    location: item
                        .get("location")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    duration_min: item
                        .get("duration_min")
                        .and_then(|v| v.as_u64())
                        .unwrap_or(0) as u32,
                    color: item
                        .get("color")
                        .and_then(|v| v.as_str())
                        .unwrap_or("1A73E8")
                        .to_string(),
                    day_offset: item
                        .get("day_offset")
                        .and_then(|v| v.as_u64())
                        .unwrap_or(0) as u8,
                })
            })
            .filter_map(|mut e| {
                let offset = i64::from(e.day_offset) - aged_days;
                if offset < 0 {
                    return None; // the day already passed while the cache sat there
                }
                e.day_offset = offset.min(14) as u8;
                Some(e)
            })
            .take(EVENT_LIMIT)
            .collect()
    }

    /// Refresh Google Calendar events in-process (no external scripts).
    pub fn refresh_cache_if_due(&mut self) {
        if self.last_refresh.elapsed() < Duration::from_secs(CALENDAR_REFRESH_INTERVAL_S) {
            return;
        }
        self.last_refresh = Instant::now();
        if !self.has_account() {
            return;
        }
        match fetch::refresh_events_cache(&self.paths, &self.selected) {
            Ok(n) => eprintln!("Tessera: calendar refresh ok ({n} eventos)"),
            Err(err) => eprintln!("Tessera: calendar refresh: {err}"),
        }
    }

    pub fn build_events_line(&mut self) -> String {
        self.refresh_cache_if_due();
        encode_calendar_line(self.linked(), &self.load_cached_events())
    }

    pub fn build_catalog_lines(&self) -> Vec<String> {
        let status = if self.has_account() {
            if self.paths.events_cache_json().is_file() {
                "ok"
            } else {
                "conta ok · sincronizando"
            }
        } else if self.paths.client_json().is_file() {
            "sem conta"
        } else {
            "configure client.json"
        };
        let ok = self.has_account();
        vec![format!(
            "CALENDAR_CATALOG\tv1\t{}\t{}",
            if ok { "1" } else { "0" },
            wire_field(status, STATUS_LIMIT)
        )]
    }

    fn auth_line(&self, pending: bool, status: &str) -> String {
        format!(
            "CALENDAR_AUTH\tv1\t{}\t{}",
            if pending { "1" } else { "0" },
            wire_field(status, STATUS_LIMIT)
        )
    }

    fn set_selection(&mut self, ids: Vec<String>) {
        self.selected = ids;
        if fs::create_dir_all(&self.paths.root).is_ok() {
            if let Ok(json) = serde_json::to_string(&self.selected) {
                let _ = fs::write(self.paths.selection_json(), json);
            }
        }
    }

    /// Handles all legacy panel commands without starting a browser. Missing
    /// configuration, unavailable OAuth, and absent accounts are valid USB
    /// responses rather than daemon errors.
    pub fn handle_line(&mut self, text: &str) -> Vec<String> {
        let raw = text.trim();
        if raw.starts_with("CALENDAR_AUTH?") {
            if !self.paths.client_json().is_file() {
                self.auth_status = "falta client.json".to_string();
                return vec![
                    self.auth_line(false, &self.auth_status),
                    self.build_catalog_lines().remove(0),
                ];
            }
            if self.has_account() {
                self.auth_status = "conta ligada".to_string();
                return vec![
                    self.auth_line(false, &self.auth_status),
                    self.build_catalog_lines().remove(0),
                ];
            }
            self.auth_status = "adicione a conta no portal".to_string();
            return vec![self.auth_line(false, &self.auth_status)];
        }
        if raw.starts_with("CALENDAR_CATALOG?") {
            return self.build_catalog_lines();
        }
        if raw.starts_with("CALENDAR_SEL\t") {
            self.set_selection(
                raw.split('\t')
                    .skip(1)
                    .map(str::trim)
                    .filter(|id| !id.is_empty())
                    .map(ToOwned::to_owned)
                    .collect(),
            );
            self.last_refresh = Instant::now()
                .checked_sub(Duration::from_secs(CALENDAR_REFRESH_INTERVAL_S))
                .unwrap_or_else(Instant::now);
            let mut reply = vec![self.build_events_line()];
            reply.extend(self.build_catalog_lines());
            return reply;
        }
        if raw.starts_with("CALENDAR?") {
            let mut reply = vec![self.build_events_line()];
            reply.extend(self.build_catalog_lines());
            return reply;
        }
        vec![]
    }
}

impl Default for CalendarCapability {
    fn default() -> Self {
        Self::default_home()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encode_keeps_tomorrow_events() {
        let line = encode_calendar_line(
            true,
            &[
                CalendarEvent {
                    start_hhmm: "0900".into(),
                    end_hhmm: "1000".into(),
                    all_day: false,
                    title: "Hoje".into(),
                    location: "".into(),
                    duration_min: 60,
                    color: "1A73E8".into(),
                    day_offset: 0,
                },
                CalendarEvent {
                    start_hhmm: "1100".into(),
                    end_hhmm: "1200".into(),
                    all_day: false,
                    title: "Amanhã".into(),
                    location: "".into(),
                    duration_min: 60,
                    color: "1A73E8".into(),
                    day_offset: 1,
                },
            ],
        );
        // The panel needs the next appointment even when it is not today; it groups by day_offset.
        assert!(line.contains("Hoje"));
        assert!(line.contains("Amanhã"));
        assert!(line.ends_with("\t1"), "last field is the day_offset: {line}");
    }

    #[test]
    fn encodes_main_event_line_like_python() {
        let line = encode_calendar_line(
            true,
            &[CalendarEvent {
                start_hhmm: "0930".into(),
                end_hhmm: "1015".into(),
                all_day: false,
                title: "Daily\tstandup".into(),
                location: "Sala\nAzul".into(),
                duration_min: 45,
                color: "#1a73e8".into(),
                day_offset: 0,
            }],
        );
        assert_eq!(
            line,
            "CALENDAR\tv1\t1\t1\t0930\t1015\t0\tDaily standup\tSala Azul\t45\t1A73E8\t0"
        );
    }

    #[test]
    fn calendar_query_degrades_without_client_json() {
        let temp = tempfile::tempdir().unwrap();
        let mut cap = CalendarCapability::new(CalendarPaths {
            root: temp.path().join("calendar"),
        });
        assert_eq!(
            cap.handle_line("CALENDAR?"),
            vec![
                "CALENDAR\tv1\t1\t0",
                "CALENDAR_CATALOG\tv1\t0\tconfigure client.json"
            ]
        );
        assert_eq!(
            cap.handle_line("CALENDAR_AUTH?")[0],
            "CALENDAR_AUTH\tv1\t0\tfalta client.json"
        );
    }
}

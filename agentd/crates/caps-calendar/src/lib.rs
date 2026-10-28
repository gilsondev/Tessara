//! CALENDAR capability — events come from the Mac Calendar app.
//! Only the snapshot and calendar names go to the panel over USB.

mod auth;
mod fetch;
mod local;

pub use auth::{
    auth_status, has_tessera_client, import_client_json, list_catalog, mark_login_expired,
    save_selection, start_account_link, start_with, AuthStatus, CalendarRow, ClientSource,
};
pub use local::{
    current_snapshot, probe, refresh_local, save_selection_local, status_short, LocalProbe, LocalSnapshot,
};

use std::fs;
use std::path::PathBuf;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

pub(crate) const EVENT_LIMIT: usize = 12;
const TITLE_LIMIT: usize = 47;
const LOCATION_LIMIT: usize = 31;
const STATUS_LIMIT: usize = 40;
/// How often to re-read the Mac Calendar database.
pub const CALENDAR_REFRESH_INTERVAL_S: u64 = 30;
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
    pub calendar_db: PathBuf,
    pub accounts_db: PathBuf,
}

impl CalendarPaths {
    pub fn default_home() -> Self {
        let home = std::env::var_os("HOME")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("."));
        Self::under_home(home)
    }

    pub fn under_home(home: PathBuf) -> Self {
        Self {
            root: home.join(".tessera/calendar"),
            calendar_db: home.join(
                "Library/Group Containers/group.com.apple.calendar/Calendar.sqlitedb",
            ),
            accounts_db: home.join("Library/Accounts/Accounts4.sqlite"),
        }
    }

    /// Paths that do not touch the real Calendar database. Used by tests.
    pub fn isolated(root: PathBuf) -> Self {
        Self {
            calendar_db: root.join("Calendar.sqlitedb"),
            accounts_db: root.join("Accounts4.sqlite"),
            root,
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
    last_pushed: Option<String>,
    health: LocalSnapshot,
    last_refresh: Instant,
    pending: Option<JoinHandle<LocalSnapshot>>,
    /// A background read finished and the panel should get the line now.
    refresh_ready: bool,
}

impl CalendarCapability {
    pub fn new(paths: CalendarPaths) -> Self {
        let health = local::read_snapshot(&paths).unwrap_or(LocalSnapshot {
            ok: false,
            phase: "missing".into(),
            message: String::new(),
            event_count: 0,
            checked_at: String::new(),
            calendars: Vec::new(),
        });
        Self {
            paths,
            last_pushed: None,
            health,
            last_refresh: Instant::now()
                .checked_sub(Duration::from_secs(CALENDAR_REFRESH_INTERVAL_S))
                .unwrap_or_else(Instant::now),
            pending: None,
            refresh_ready: false,
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

    fn linked(&self) -> bool {
        // A confirmed block or a missing Calendar.app stays unlinked. Anything
        // else keeps the last good cache on screen, including while a refresh runs.
        if self.health.phase == "blocked" || self.health.phase == "missing" {
            return false;
        }
        self.health.ok || !self.load_cached_events().is_empty()
    }

    /// True once, after a background refresh lands. The USB loop pushes then,
    /// instead of waiting for the next heartbeat.
    pub fn take_refresh_ready(&mut self) -> bool {
        let ready = self.refresh_ready;
        self.refresh_ready = false;
        ready
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
                    all_day: item
                        .get("all_day")
                        .and_then(|v| v.as_bool())
                        .unwrap_or(false),
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
                    day_offset: item.get("day_offset").and_then(|v| v.as_u64()).unwrap_or(0) as u8,
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

    /// Re-read the Mac Calendar database and record whether that check is healthy.
    pub fn refresh_cache_if_due(&mut self) {
        if let Some(handle) = self.pending.take() {
            if handle.is_finished() {
                if let Ok(health) = handle.join() {
                    self.health = health;
                    self.refresh_ready = true;
                    if self.health.ok {
                        eprintln!(
                            "Tessera: calendar refresh ok ({} eventos)",
                            self.health.event_count
                        );
                    } else {
                        eprintln!("Tessera: calendar refresh: {}", self.health.message);
                    }
                }
            } else {
                self.pending = Some(handle);
                return;
            }
        }
        if self.last_refresh.elapsed() < Duration::from_secs(CALENDAR_REFRESH_INTERVAL_S) {
            return;
        }
        self.last_refresh = Instant::now();
        let paths = self.paths.clone();
        self.pending = Some(std::thread::spawn(move || refresh_local(&paths)));
    }

    pub fn build_events_line(&mut self) -> String {
        self.refresh_cache_if_due();
        encode_calendar_line(self.linked(), &self.load_cached_events())
    }

    pub fn build_catalog_lines(&self) -> Vec<String> {
        vec![format!(
            "CALENDAR_CATALOG\tv1\t{}\t{}",
            if self.health.ok { "1" } else { "0" },
            wire_field(status_short(&self.health.phase), STATUS_LIMIT)
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
        let _ = save_selection_local(&self.paths, &ids);
        self.last_refresh = Instant::now()
            .checked_sub(Duration::from_secs(CALENDAR_REFRESH_INTERVAL_S))
            .unwrap_or_else(Instant::now);
    }

    /// Handles all legacy panel commands without starting a browser. Missing
    /// configuration, unavailable OAuth, and absent accounts are valid USB
    /// responses rather than daemon errors.
    pub fn handle_line(&mut self, text: &str) -> Vec<String> {
        let raw = text.trim();
        if raw.starts_with("CALENDAR_AUTH?") {
            self.refresh_cache_if_due();
            let status = status_short(&self.health.phase).to_string();
            return vec![
                self.auth_line(false, &status),
                self.build_catalog_lines().remove(0),
            ];
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
        assert!(
            line.ends_with("\t1"),
            "last field is the day_offset: {line}"
        );
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
    fn first_line_uses_the_saved_cache() {
        let temp = tempfile::tempdir().unwrap();
        let root = temp.path().join("calendar");
        std::fs::create_dir_all(&root).unwrap();
        // Offsets are counted from the cache day. A fixed date drops the event once that day passes.
        let now = chrono::Local::now().to_rfc3339();
        std::fs::write(
            root.join("health.json"),
            format!(
                r#"{{"ok":true,"phase":"ok","message":"","event_count":1,"checked_at":"{now}","calendars":[]}}"#
            ),
        )
        .unwrap();
        std::fs::write(
            root.join("events_cache.json"),
            format!(
                r#"{{"fetched_at":"{now}","events":[{{"start_hhmm":"1900","end_hhmm":"1930","all_day":false,"title":"Ensaio","location":"","duration_min":30,"color":"1A73E8","day_offset":0}}]}}"#
            ),
        )
        .unwrap();
        let mut cap = CalendarCapability::new(CalendarPaths::isolated(root));
        let line = &cap.handle_line("CALENDAR?")[0];
        assert!(line.starts_with("CALENDAR\tv1\t1\t1\t"), "{line}");
        assert!(line.contains("Ensaio"), "{line}");
    }

    #[test]
    fn calendar_query_reports_a_missing_mac_calendar() {
        let temp = tempfile::tempdir().unwrap();
        let mut cap = CalendarCapability::new(CalendarPaths::isolated(temp.path().join("calendar")));
        assert_eq!(
            cap.handle_line("CALENDAR?"),
            vec![
                "CALENDAR\tv1\t1\t0",
                "CALENDAR_CATALOG\tv1\t0\tsem agenda no Mac"
            ]
        );
        assert_eq!(
            cap.handle_line("CALENDAR_AUTH?")[0],
            "CALENDAR_AUTH\tv1\t0\tsem agenda no Mac"
        );
    }
}

//! CALENDAR capability — Google OAuth data stays on the Mac; only snapshots
//! and calendar catalog metadata are sent to the panel over USB.

use std::fs;
use std::path::PathBuf;

const EVENT_LIMIT: usize = 12;
const TITLE_LIMIT: usize = 47;
const LOCATION_LIMIT: usize = 31;
const STATUS_LIMIT: usize = 40;

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
    for event in events
        .iter()
        .filter(|event| event.day_offset <= 6)
        .take(EVENT_LIMIT)
    {
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
}

pub struct CalendarCapability {
    paths: CalendarPaths,
    selected: Vec<String>,
    last_pushed: Option<String>,
    auth_status: String,
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

    pub fn build_events_line(&self) -> String {
        // OAuth refresh / Google API fetching is deliberately not ported yet.
        // An existing local token still reports as linked without exposing it.
        encode_calendar_line(self.linked(), &[])
    }

    pub fn build_catalog_lines(&self) -> Vec<String> {
        let status = if self.has_account() {
            "catálogo indisponível"
        } else if self.paths.client_json().is_file() {
            "sem conta"
        } else {
            "configure client.json"
        };
        vec![format!(
            "CALENDAR_CATALOG\tv1\t0\t{}",
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
            self.auth_status = "OAuth ainda não implementado".to_string();
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

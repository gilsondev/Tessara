//! Read Google calendars that the Mac Calendar app already syncs.
//! The database stays on this Mac. No Google Cloud client is involved.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

use chrono::{DateTime, Local, TimeZone, Timelike};
use serde::{Deserialize, Serialize};

use crate::{encode_calendar_line, CalendarEvent, CalendarPaths, CalendarRow, EVENT_LIMIT};

const APPLE_EPOCH_OFFSET: f64 = 978_307_200.0;
const WINDOW_DAYS: i64 = 8;
/// A check older than this means the refresh loop stopped.
const STALE_AFTER_S: i64 = 180;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LocalProbe {
    Ready,
    Missing,
    Blocked,
    Empty,
    Error,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct LocalSnapshot {
    pub ok: bool,
    pub phase: String,
    pub message: String,
    pub event_count: usize,
    pub checked_at: String,
    #[serde(default)]
    pub calendars: Vec<CalendarRow>,
}

pub fn status_short(phase: &str) -> &'static str {
    match phase {
        "ok" => "agenda do Mac",
        "blocked" => "sem acesso à agenda",
        "empty" => "sem calendário Google",
        "stale" => "checagem atrasada",
        "error" => "checagem falhou",
        _ => "sem agenda no Mac",
    }
}

/// Read the last written check. If it is old or missing, read the Calendar database again.
pub fn current_snapshot(paths: &CalendarPaths) -> LocalSnapshot {
    if let Some(snapshot) = read_snapshot(paths) {
        if let Some(age) = snapshot_age_secs(&snapshot.checked_at) {
            if age < STALE_AFTER_S {
                return snapshot;
            }
        }
    }
    refresh_local(paths)
}

pub fn refresh_local(paths: &CalendarPaths) -> LocalSnapshot {
    let selection = selection_from_disk(paths);
    let snapshot = collect(paths, selection.as_deref());
    let _ = write_snapshot(paths, &snapshot);
    snapshot
}

pub fn probe(paths: &CalendarPaths) -> LocalProbe {
    match open_calendar(paths) {
        Open::Missing => LocalProbe::Missing,
        Open::Blocked => LocalProbe::Blocked,
        Open::Failed(_) => LocalProbe::Error,
        Open::Rows(rows) => {
            let emails = google_emails(&paths.accounts_db);
            if google_calendars(&rows, &emails).is_empty() {
                LocalProbe::Empty
            } else {
                LocalProbe::Ready
            }
        }
    }
}

fn selection_from_disk(paths: &CalendarPaths) -> Option<Vec<String>> {
    let raw = fs::read_to_string(paths.selection_json()).ok()?;
    serde_json::from_str(&raw).ok()
}

fn collect(paths: &CalendarPaths, selection: Option<&[String]>) -> LocalSnapshot {
    let checked = Local::now();
    let checked_label = checked.format("%H:%M").to_string();
    let checked_at = checked.to_rfc3339();
    let rows = match open_calendar(paths) {
        Open::Missing => {
            return snapshot(
                false,
                "missing",
                "O app Calendário ainda não tem agenda neste Mac.",
                0,
                checked_at,
                Vec::new(),
            );
        }
        Open::Blocked => {
            return snapshot(
                false,
                "blocked",
                "Não consegui ler a agenda do Mac. Ligue Tessera Agent em Acesso Total ao Disco.",
                0,
                checked_at,
                Vec::new(),
            );
        }
        Open::Failed(err) => {
            return snapshot(
                false,
                "error",
                &format!("A checagem da agenda falhou. {err}"),
                0,
                checked_at,
                Vec::new(),
            );
        }
        Open::Rows(rows) => rows,
    };
    let emails = google_emails(&paths.accounts_db);
    let mut calendars = google_calendars(&rows, &emails);
    if calendars.is_empty() {
        return snapshot(
            false,
            "empty",
            "A agenda do Mac abre, mas nenhum calendário do Google está sincronizado.",
            0,
            checked_at,
            Vec::new(),
        );
    }
    drop_empty_duplicates(&mut calendars);
    let sync_apple = calendars
        .iter()
        .map(|c| c.sync_apple)
        .fold(0.0_f64, f64::max);
    let sync_label = apple_to_local(sync_apple)
        .map(|dt| dt.format("%H:%M").to_string())
        .unwrap_or_default();
    let chosen: Vec<&LocalCalendar> = match selection {
        None => calendars.iter().collect(),
        Some(ids) => calendars.iter().filter(|c| ids.iter().any(|id| id == &c.id)).collect(),
    };
    let ids: Vec<i64> = chosen.iter().filter_map(|c| c.id.parse().ok()).collect();
    let events = if ids.is_empty() {
        Vec::new()
    } else {
        load_events(&paths.calendar_db, &ids, checked).unwrap_or_default()
    };
    let stamp = format!(
        "{:.0}:{}:{}",
        sync_apple,
        calendars.iter().map(|c| c.items).sum::<i64>(),
        calendars.iter().map(|c| c.id.parse::<i64>().unwrap_or(0)).max().unwrap_or(0)
    );
    let changed = stamp_changed(paths, &stamp);
    if let Err(err) = write_events(paths, &checked, &events) {
        return snapshot(
            false,
            "error",
            &format!("A checagem da agenda falhou. {err}"),
            0,
            checked_at,
            Vec::new(),
        );
    }
    let rows: Vec<CalendarRow> = calendars
        .iter()
        .map(|c| CalendarRow {
            selected: match selection {
                None => true,
                Some(ids) => ids.iter().any(|id| id == &c.id),
            },
            id: c.id.clone(),
            account: c.account.clone(),
            name: c.title.clone(),
            color: c.color.clone(),
        })
        .collect();
    let n = rows.len();
    let mut message = format!(
        "Saudável. {} neste Mac. Checagem às {checked_label}.",
        calendarios(n)
    );
    if !sync_label.is_empty() {
        message.push_str(&format!(" O Calendário sincronizou às {sync_label}."));
    }
    if changed {
        message.push_str(" Houve atualização desde a checagem anterior.");
    }
    if selection.is_some_and(|ids| ids.is_empty()) {
        message.push_str(" Nenhum calendário marcado para o painel.");
    }
    snapshot(true, "ok", &message, events.len().min(EVENT_LIMIT), checked_at, rows)
}

fn calendarios(n: usize) -> String {
    if n == 1 {
        "1 calendário do Google".to_string()
    } else {
        format!("{n} calendários do Google")
    }
}

fn snapshot(
    ok: bool,
    phase: &str,
    message: &str,
    event_count: usize,
    checked_at: String,
    calendars: Vec<CalendarRow>,
) -> LocalSnapshot {
    LocalSnapshot {
        ok,
        phase: phase.to_string(),
        message: message.to_string(),
        event_count,
        checked_at,
        calendars,
    }
}

fn write_events(
    paths: &CalendarPaths,
    now: &DateTime<Local>,
    events: &[CalendarEvent],
) -> Result<(), String> {
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    let body = serde_json::json!({
        "fetched_at": now.to_rfc3339(),
        "events": events.iter().take(EVENT_LIMIT).map(|e| serde_json::json!({
            "start_hhmm": e.start_hhmm,
            "end_hhmm": e.end_hhmm,
            "all_day": e.all_day,
            "title": e.title,
            "location": e.location,
            "duration_min": e.duration_min,
            "color": e.color,
            "day_offset": e.day_offset,
        })).collect::<Vec<_>>(),
    });
    let raw = serde_json::to_string_pretty(&body).map_err(|e| e.to_string())?;
    fs::write(paths.events_cache_json(), raw).map_err(|e| e.to_string())?;
    // Touch the encoder so a bad event fails in tests the same way the USB line would.
    let _ = encode_calendar_line(true, events);
    Ok(())
}

fn stamp_changed(paths: &CalendarPaths, stamp: &str) -> bool {
    let path = paths.root.join("local_stamp");
    let prev = fs::read_to_string(&path).unwrap_or_default();
    let changed = !prev.is_empty() && prev.trim() != stamp;
    let _ = fs::create_dir_all(&paths.root);
    let _ = fs::write(path, stamp);
    changed
}

fn health_path(paths: &CalendarPaths) -> PathBuf {
    paths.root.join("health.json")
}

fn write_snapshot(paths: &CalendarPaths, snapshot: &LocalSnapshot) -> Result<(), String> {
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    let raw = serde_json::to_string_pretty(snapshot).map_err(|e| e.to_string())?;
    fs::write(health_path(paths), raw).map_err(|e| e.to_string())
}

fn read_snapshot(paths: &CalendarPaths) -> Option<LocalSnapshot> {
    let raw = fs::read_to_string(health_path(paths)).ok()?;
    serde_json::from_str(&raw).ok()
}

fn snapshot_age_secs(checked_at: &str) -> Option<i64> {
    let written = DateTime::parse_from_rfc3339(checked_at).ok()?;
    let now = SystemTime::now().duration_since(UNIX_EPOCH).ok()?.as_secs() as i64;
    Some(now.saturating_sub(written.timestamp()))
}

struct LocalCalendar {
    id: String,
    title: String,
    color: String,
    account: String,
    sync_apple: f64,
    items: i64,
}

enum Open {
    Missing,
    Blocked,
    Failed(String),
    Rows(Vec<Vec<String>>),
}

fn open_calendar(paths: &CalendarPaths) -> Open {
    if !paths.calendar_db.is_file() {
        return Open::Missing;
    }
    match sqlite_rows(
        &paths.calendar_db,
        "SELECT c.ROWID, REPLACE(REPLACE(IFNULL(c.title,''), char(10), ' '), char(9), ' '), \
         IFNULL(c.color,''), IFNULL(s.name,''), IFNULL(c.self_identity_email,''), \
         IFNULL(c.owner_identity_email,''), IFNULL(s.disabled,0), IFNULL(s.last_sync_end,0), \
         (SELECT COUNT(*) FROM CalendarItem i WHERE i.calendar_id = c.ROWID) \
         FROM Calendar c JOIN Store s ON s.ROWID = c.store_id",
    ) {
        Ok(rows) => Open::Rows(rows),
        Err(err) => {
            let lower = err.to_ascii_lowercase();
            if lower.contains("authorization")
                || lower.contains("operation not permitted")
                || lower.contains("unable to open")
            {
                Open::Blocked
            } else {
                Open::Failed(err)
            }
        }
    }
}

fn google_emails(accounts_db: &Path) -> Vec<String> {
    if !accounts_db.is_file() {
        return Vec::new();
    }
    sqlite_rows(
        accounts_db,
        "SELECT IFNULL(a.ZUSERNAME,'') FROM ZACCOUNT a \
         JOIN ZACCOUNTTYPE t ON a.ZACCOUNTTYPE = t.Z_PK \
         WHERE t.ZIDENTIFIER = 'com.apple.account.Google'",
    )
    .unwrap_or_default()
    .into_iter()
    .filter_map(|row| row.into_iter().next())
    .map(|email| email.trim().to_ascii_lowercase())
    .filter(|email| email.contains('@'))
    .collect()
}

fn google_calendars(rows: &[Vec<String>], emails: &[String]) -> Vec<LocalCalendar> {
    let mut out = Vec::new();
    for row in rows {
        if row.len() < 9 || row[6] == "1" {
            continue;
        }
        let self_email = row[4].trim().to_ascii_lowercase();
        let owner_email = row[5].trim().to_ascii_lowercase();
        let google = emails.iter().any(|email| email == &self_email || email == &owner_email)
            || self_email.contains("calendar.google.com")
            || owner_email.contains("calendar.google.com");
        if !google {
            continue;
        }
        let title = row[1].trim();
        if title.is_empty() {
            continue;
        }
        let account = if self_email.contains('@') {
            row[4].trim().to_string()
        } else if owner_email.contains('@') {
            row[5].trim().to_string()
        } else {
            row[3].trim().to_string()
        };
        out.push(LocalCalendar {
            id: row[0].clone(),
            title: title.to_string(),
            color: normalize_color(&row[2]),
            account,
            sync_apple: row[7].parse().unwrap_or(0.0),
            items: row[8].parse().unwrap_or(0),
        });
    }
    out
}

fn drop_empty_duplicates(calendars: &mut Vec<LocalCalendar>) {
    let titles_with_items: Vec<String> = calendars
        .iter()
        .filter(|c| c.items > 0)
        .map(|c| c.title.to_ascii_lowercase())
        .collect();
    calendars.retain(|c| c.items > 0 || !titles_with_items.iter().any(|t| t == &c.title.to_ascii_lowercase()));
}

fn load_events(db: &Path, ids: &[i64], now: DateTime<Local>) -> Result<Vec<CalendarEvent>, String> {
    let list = ids
        .iter()
        .map(|id| id.to_string())
        .collect::<Vec<_>>()
        .join(",");
    let today = now.date_naive();
    let start_naive = today
        .and_hms_opt(0, 0, 0)
        .ok_or_else(|| "data local inválida".to_string())?;
    let start = Local
        .from_local_datetime(&start_naive)
        .single()
        .unwrap_or(now);
    // All-day rows are stored at 00:00 UTC of that date, which is still the previous evening here.
    let utc_midnight = today
        .and_hms_opt(0, 0, 0)
        .ok_or_else(|| "data local inválida".to_string())?
        .and_utc();
    let end = start + chrono::Duration::days(WINDOW_DAYS);
    let start_apple = (start.timestamp().min(utc_midnight.timestamp()) as f64) - APPLE_EPOCH_OFFSET;
    let end_apple = end.timestamp() as f64 - APPLE_EPOCH_OFFSET;
    let item_sql = format!(
        "SELECT REPLACE(REPLACE(IFNULL(i.summary,''), char(10), ' '), char(9), ' '), \
         IFNULL(i.all_day,0), i.start_date, IFNULL(i.end_date,0), i.calendar_id, IFNULL(c.color,'') \
         FROM CalendarItem i JOIN Calendar c ON c.ROWID = i.calendar_id \
         WHERE i.calendar_id IN ({list}) AND i.start_date >= {start_apple} AND i.start_date < {end_apple}"
    );
    let mut events = rows_to_events(&sqlite_rows(db, &item_sql)?, now.date_naive());
    // Most rows leave occurrence_start_date empty and store the instance in occurrence_date.
    // Recurring events (the day's repeated commitments) live only in this cache.
    let occ_sql = format!(
        "SELECT REPLACE(REPLACE(IFNULL(i.summary,''), char(10), ' '), char(9), ' '), \
         IFNULL(i.all_day,0), \
         CASE WHEN IFNULL(o.occurrence_start_date,0) > 0 THEN o.occurrence_start_date \
              ELSE IFNULL(o.occurrence_date,0) END, \
         IFNULL(o.occurrence_end_date,0), \
         o.calendar_id, IFNULL(c.color,'') \
         FROM OccurrenceCache o \
         JOIN CalendarItem i ON i.ROWID = o.event_id \
         JOIN Calendar c ON c.ROWID = o.calendar_id \
         WHERE o.calendar_id IN ({list}) \
         AND CASE WHEN IFNULL(o.occurrence_start_date,0) > 0 THEN o.occurrence_start_date \
             ELSE IFNULL(o.occurrence_date,0) END >= {start_apple} \
         AND CASE WHEN IFNULL(o.occurrence_start_date,0) > 0 THEN o.occurrence_start_date \
             ELSE IFNULL(o.occurrence_date,0) END < {end_apple}"
    );
    if let Ok(rows) = sqlite_rows(db, &occ_sql) {
        events.extend(rows_to_events(&rows, now.date_naive()));
    }
    events.sort_by(|a, b| {
        (a.day_offset, !a.all_day, a.start_hhmm.as_str(), a.title.as_str()).cmp(&(
            b.day_offset,
            !b.all_day,
            b.start_hhmm.as_str(),
            b.title.as_str(),
        ))
    });
    events.dedup_by(|a, b| {
        a.title == b.title && a.day_offset == b.day_offset && a.start_hhmm == b.start_hhmm
    });
    events.truncate(EVENT_LIMIT);
    Ok(events)
}

fn rows_to_events(rows: &[Vec<String>], today: chrono::NaiveDate) -> Vec<CalendarEvent> {
    rows.iter().filter_map(|row| row_to_event(row, today)).collect()
}

fn row_to_event(row: &[String], today: chrono::NaiveDate) -> Option<CalendarEvent> {
    if row.len() < 6 {
        return None;
    }
    let title = if row[0].trim().is_empty() {
        "(sem título)".to_string()
    } else {
        row[0].trim().to_string()
    };
    let all_day = row[1] == "1";
    let start_apple: f64 = row[2].parse().ok()?;
    let start = apple_to_local(start_apple)?;
    let end = apple_to_local(row[3].parse().unwrap_or(0.0)).unwrap_or(start + chrono::Duration::minutes(30));
    // All-day values are floating dates: midnight UTC of that calendar day, which is the
    // previous evening in Brasília. The day number is the UTC date, not the local clock.
    let event_day = if all_day {
        apple_utc_date(start_apple)?
    } else {
        start.date_naive()
    };
    let offset_days = (event_day - today).num_days();
    if !(0..=14).contains(&offset_days) {
        return None;
    }
    let color = normalize_color(&row[5]);
    if all_day {
        return Some(CalendarEvent {
            start_hhmm: "0000".into(),
            end_hhmm: "0000".into(),
            all_day: true,
            title,
            location: String::new(),
            duration_min: 1440,
            color,
            day_offset: offset_days as u8,
        });
    }
    let duration_min = (end - start).num_minutes().max(1) as u32;
    Some(CalendarEvent {
        start_hhmm: format!("{:02}{:02}", start.hour(), start.minute()),
        end_hhmm: format!("{:02}{:02}", end.hour(), end.minute()),
        all_day: false,
        title,
        location: String::new(),
        duration_min,
        color,
        day_offset: offset_days as u8,
    })
}

fn apple_to_local(apple: f64) -> Option<DateTime<Local>> {
    if apple <= 0.0 {
        return None;
    }
    let unix = (apple + APPLE_EPOCH_OFFSET).round() as i64;
    DateTime::from_timestamp(unix, 0).map(|dt| dt.with_timezone(&Local))
}

fn apple_utc_date(apple: f64) -> Option<chrono::NaiveDate> {
    if apple <= 0.0 {
        return None;
    }
    let unix = (apple + APPLE_EPOCH_OFFSET).round() as i64;
    DateTime::from_timestamp(unix, 0).map(|dt| dt.date_naive())
}

fn normalize_color(raw: &str) -> String {
    let hex: String = raw.trim().trim_start_matches('#').chars().take(6).collect();
    if hex.len() == 6 && hex.chars().all(|c| c.is_ascii_hexdigit()) {
        hex.to_ascii_uppercase()
    } else {
        "1A73E8".to_string()
    }
}

fn sqlite_rows(db: &Path, sql: &str) -> Result<Vec<Vec<String>>, String> {
    let output = Command::new("/usr/bin/sqlite3")
        .arg("-separator")
        .arg("\u{1f}")
        .arg(sqlite_uri(db))
        .arg(sql)
        .output()
        .map_err(|e| format!("sqlite3: {e}"))?;
    if !output.status.success() {
        let err = String::from_utf8_lossy(&output.stderr);
        let err = err.trim();
        if err.is_empty() {
            return Err(format!("sqlite3 saiu com {}", output.status));
        }
        return Err(err.to_string());
    }
    let text = String::from_utf8_lossy(&output.stdout);
    Ok(text
        .lines()
        .filter(|line| !line.is_empty())
        .map(|line| line.split('\u{1f}').map(str::to_string).collect())
        .collect())
}

fn sqlite_uri(path: &Path) -> String {
    let mut enc = String::from("file:");
    for b in path.to_string_lossy().bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'/' | b'.' | b'-' | b'_' => {
                enc.push(b as char);
            }
            _ => enc.push_str(&format!("%{b:02X}")),
        }
    }
    enc.push_str("?mode=ro");
    enc
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::Timelike;
    use std::process::Command;

    fn exec(path: &std::path::Path, sql: &str) {
        let status = Command::new("/usr/bin/sqlite3")
            .arg(path)
            .arg(sql)
            .status()
            .unwrap();
        assert!(status.success());
    }

    fn fixture(root: &std::path::Path) -> CalendarPaths {
        let calendar_db = root.join("Calendar.sqlitedb");
        let accounts_db = root.join("Accounts4.sqlite");
        let now_apple = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as f64
            - APPLE_EPOCH_OFFSET;
        let soon = now_apple + 3600.0;
        let soon_end = soon + 1800.0;
        exec(
            &accounts_db,
            "CREATE TABLE ZACCOUNTTYPE (Z_PK INTEGER PRIMARY KEY, ZIDENTIFIER TEXT);
             CREATE TABLE ZACCOUNT (Z_PK INTEGER PRIMARY KEY, ZACCOUNTTYPE INTEGER, ZUSERNAME TEXT);
             INSERT INTO ZACCOUNTTYPE VALUES (1, 'com.apple.account.Google');
             INSERT INTO ZACCOUNT VALUES (1, 1, 'me@example.com');",
        );
        exec(
            &calendar_db,
            &format!(
                "CREATE TABLE Store (ROWID INTEGER PRIMARY KEY, name TEXT, disabled INTEGER, last_sync_end REAL);
                 CREATE TABLE Calendar (ROWID INTEGER PRIMARY KEY, store_id INTEGER, title TEXT, color TEXT, self_identity_email TEXT, owner_identity_email TEXT);
                 CREATE TABLE CalendarItem (ROWID INTEGER PRIMARY KEY, summary TEXT, start_date REAL, end_date REAL, all_day INTEGER, calendar_id INTEGER);
                 CREATE TABLE OccurrenceCache (event_id INTEGER, calendar_id INTEGER, occurrence_start_date REAL, occurrence_end_date REAL, occurrence_date REAL);
                 INSERT INTO Store VALUES (1, 'Pessoa', 0, {now_apple});
                 INSERT INTO Store VALUES (2, 'iCloud', 0, {now_apple});
                 INSERT INTO Store VALUES (3, 'F1', 0, {now_apple});
                 INSERT INTO Calendar VALUES (10, 1, 'Pessoal', '#1A73E8', 'me@example.com', 'me@example.com');
                 INSERT INTO Calendar VALUES (11, 1, 'Pessoal', '#1A73E8', 'me@example.com', 'me@example.com');
                 INSERT INTO Calendar VALUES (20, 2, 'Trabalho', '#FF0000', 'a@icloud.com', 'a@icloud.com');
                 INSERT INTO Calendar VALUES (30, 3, 'Formula', '#00AA00', 'x@group.calendar.google.com', '');
                 INSERT INTO CalendarItem VALUES (1, 'Reuniao', {soon}, {soon_end}, 0, 10);
                 INSERT INTO CalendarItem VALUES (2, 'Nao Google', {soon}, {soon_end}, 0, 20);
                 INSERT INTO CalendarItem VALUES (3, 'Corrida', {soon}, {soon_end}, 0, 30);"
            ),
        );
        CalendarPaths {
            root: root.join("tessera"),
            calendar_db,
            accounts_db,
        }
    }

    #[test]
    fn reads_google_calendars_and_skips_icloud() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        let snap = refresh_local(&paths);
        assert!(snap.ok, "{}", snap.message);
        assert_eq!(snap.phase, "ok");
        let names: Vec<_> = snap.calendars.iter().map(|c| c.name.as_str()).collect();
        assert!(names.contains(&"Pessoal"), "{names:?}");
        assert!(names.contains(&"Formula"), "{names:?}");
        assert!(!names.contains(&"Trabalho"), "{names:?}");
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(raw.contains("Corrida"), "{raw}");
        assert!(!raw.contains("Nao Google"), "{raw}");
        assert!(snap.message.starts_with("Saudável"));
    }

    #[test]
    fn today_keeps_finished_occurrences_and_the_floating_all_day() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        let now = chrono::Local::now();
        let today = now.date_naive();
        let holiday_apple = today
            .and_hms_opt(0, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp() as f64
            - APPLE_EPOCH_OFFSET;
        // Already finished, but still today — including just after midnight.
        let past = if now.time().hour() >= 2 {
            now - chrono::Duration::hours(2)
        } else {
            chrono::Local
                .from_local_datetime(&today.and_hms_opt(0, 10, 0).unwrap())
                .single()
                .unwrap()
        };
        let past_end = if now.time().hour() >= 2 {
            now - chrono::Duration::hours(1)
        } else {
            chrono::Local
                .from_local_datetime(&today.and_hms_opt(0, 20, 0).unwrap())
                .single()
                .unwrap()
        };
        let past_apple = past.timestamp() as f64 - APPLE_EPOCH_OFFSET;
        let past_end_apple = past_end.timestamp() as f64 - APPLE_EPOCH_OFFSET;
        exec(
            &paths.calendar_db,
            &format!(
                "INSERT INTO CalendarItem VALUES (4, 'Devocional', 1000, 2000, 0, 10);
                 INSERT INTO CalendarItem VALUES (5, 'Feriado', {holiday_apple}, {holiday_apple} + 86399, 1, 10);
                 INSERT INTO OccurrenceCache (event_id, calendar_id, occurrence_start_date, occurrence_end_date, occurrence_date)
                 VALUES (4, 10, 0, {past_end_apple}, {past_apple});"
            ),
        );
        let snap = refresh_local(&paths);
        assert!(snap.ok, "{}", snap.message);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Devocional"), "{raw}");
        assert!(raw.contains("Feriado"), "{raw}");
        let value: serde_json::Value = serde_json::from_str(&raw).unwrap();
        let holiday = value["events"]
            .as_array()
            .unwrap()
            .iter()
            .find(|e| e["title"] == "Feriado")
            .expect("feriado");
        assert_eq!(holiday["day_offset"], 0);
        assert_eq!(holiday["all_day"], true);
    }

    #[test]
    fn second_check_reports_an_update_when_sync_moves() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        let first = refresh_local(&paths);
        assert!(!first.message.contains("atualização"));
        exec(
            &paths.calendar_db,
            "UPDATE Store SET last_sync_end = last_sync_end + 10 WHERE ROWID = 1;",
        );
        let second = refresh_local(&paths);
        assert!(second.message.contains("atualização"), "{}", second.message);
    }

    #[test]
    fn missing_database_is_not_healthy() {
        let temp = tempfile::tempdir().unwrap();
        let paths = CalendarPaths::isolated(temp.path().join("calendar"));
        let snap = refresh_local(&paths);
        assert!(!snap.ok);
        assert_eq!(snap.phase, "missing");
        assert_eq!(probe(&paths), LocalProbe::Missing);
    }

    #[test]
    fn readable_database_without_google_is_empty() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        exec(&paths.accounts_db, "DELETE FROM ZACCOUNT;");
        exec(
            &paths.calendar_db,
            "DELETE FROM Calendar WHERE self_identity_email LIKE '%google.com' OR self_identity_email = 'me@example.com';",
        );
        let snap = refresh_local(&paths);
        assert!(!snap.ok);
        assert_eq!(snap.phase, "empty");
    }

    #[test]
    fn an_old_check_is_read_again() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        let mut snap = refresh_local(&paths);
        snap.checked_at = "2000-01-01T00:00:00+00:00".into();
        snap.phase = "ok".into();
        write_snapshot(&paths, &snap).unwrap();
        let current = current_snapshot(&paths);
        assert!(current.ok, "{}", current.message);
        assert_ne!(current.checked_at, "2000-01-01T00:00:00+00:00");
    }
}

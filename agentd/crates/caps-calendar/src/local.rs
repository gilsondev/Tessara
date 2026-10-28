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
    refresh_local_at(paths, Local::now())
}

fn refresh_local_at(paths: &CalendarPaths, now: DateTime<Local>) -> LocalSnapshot {
    let selection = selection_from_disk(paths);
    let snapshot = collect(paths, selection, now);
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

enum StoredSelection {
    Missing,
    /// Old file: ROWIDs of the calendars that should appear.
    Include(Vec<String>),
    /// Current file: stable keys of the calendars that stay off the panel.
    /// `v1_applied` means the old include-list was already recovered once.
    /// `rowids_seen` are the Calendar ROWIDs present when that migration ran.
    Exclude {
        keys: Vec<String>,
        v1_applied: bool,
        rowids_seen: Vec<String>,
        migrated_at: i64,
        wait_n: u32,
    },
}

fn selection_from_disk(paths: &CalendarPaths) -> StoredSelection {
    let Ok(raw) = fs::read_to_string(paths.selection_json()) else {
        return StoredSelection::Missing;
    };
    let Ok(value) = serde_json::from_str::<serde_json::Value>(&raw) else {
        return StoredSelection::Missing;
    };
    if let Some(rows) = value.as_array() {
        return StoredSelection::Include(
            rows.iter().filter_map(|row| row.as_str().map(str::to_string)).collect(),
        );
    }
    let Some(excluded) = value.get("excluded").and_then(|row| row.as_array()) else {
        return StoredSelection::Missing;
    };
    StoredSelection::Exclude {
        keys: excluded.iter().filter_map(|row| row.as_str().map(str::to_string)).collect(),
        v1_applied: value.get("v1_applied").and_then(|flag| flag.as_bool()).unwrap_or(false),
        rowids_seen: value
            .get("v1_rowids_seen")
            .and_then(|row| row.as_array())
            .map(|rows| rows.iter().filter_map(|row| row.as_str().map(str::to_string)).collect())
            .unwrap_or_default(),
        migrated_at: value.get("v1_migrated_at").and_then(|stamp| stamp.as_i64()).unwrap_or(0),
        wait_n: value.get("v1_wait_n").and_then(|n| n.as_u64()).unwrap_or(0) as u32,
    }
}

fn write_excluded(
    paths: &CalendarPaths,
    excluded: &[String],
    v1_applied: bool,
    rowids_seen: &[String],
    migrated_at: i64,
    wait_n: u32,
) -> Result<(), String> {
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    let mut body = serde_json::json!({
        "version": 2,
        "excluded": excluded,
        "v1_applied": v1_applied,
    });
    if !rowids_seen.is_empty() {
        body["v1_rowids_seen"] = serde_json::json!(rowids_seen);
    }
    if migrated_at > 0 {
        body["v1_migrated_at"] = serde_json::json!(migrated_at);
        body["v1_wait_n"] = serde_json::json!(wait_n);
    }
    let raw = serde_json::to_string_pretty(&body).map_err(|e| e.to_string())?;
    tessera_proc::write_atomic(&paths.selection_json(), raw).map_err(|e| e.to_string())
}

fn backup_v1(paths: &CalendarPaths) {
    let dest = paths.root.join("selection.v1.json");
    if dest.is_file() {
        return;
    }
    let Ok(raw) = fs::read(paths.selection_json()) else {
        return;
    };
    let _ = fs::create_dir_all(&paths.root);
    let _ = tessera_proc::write_atomic(&dest, raw);
}

fn read_v1_include(paths: &CalendarPaths) -> Option<Vec<String>> {
    let raw = fs::read_to_string(paths.root.join("selection.v1.json")).ok()?;
    let value: serde_json::Value = serde_json::from_str(&raw).ok()?;
    let rows = value.as_array()?;
    Some(rows.iter().filter_map(|row| row.as_str().map(str::to_string)).collect())
}

pub fn save_selection_local(paths: &CalendarPaths, selected_rowids: &[String]) -> Result<(), String> {
    let rows = match open_calendar(paths) {
        Open::Rows(rows) => rows,
        Open::Missing => return Err("sem agenda no Mac".to_string()),
        Open::Blocked => return Err("sem acesso à agenda".to_string()),
        Open::Failed(err) => return Err(err),
    };
    let emails = google_emails(&paths.accounts_db);
    let mut calendars = google_calendars(&rows, &emails);
    drop_empty_duplicates(&mut calendars);
    if calendars.is_empty() {
        return Err("nenhuma agenda do Google para gravar a seleção".to_string());
    }
    let excluded: Vec<String> = calendars
        .iter()
        .filter(|calendar| !selected_rowids.iter().any(|id| id == &calendar.id))
        .map(|calendar| calendar.key.clone())
        .collect();
    write_excluded(paths, &excluded, true, &[], 0, 0)
}

fn stable_key(uuid: &str, external_id: &str, account: &str, rowid: &str) -> String {
    let ident = if !uuid.trim().is_empty() {
        uuid.trim().to_string()
    } else if !external_id.trim().is_empty() {
        format!("ext:{}", external_id.trim())
    } else {
        format!("row:{rowid}")
    };
    format!("{ident}|{}", account.trim().to_ascii_lowercase())
}

fn collect(paths: &CalendarPaths, selection: StoredSelection, now: DateTime<Local>) -> LocalSnapshot {
    let checked = now;
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
        if matches!(selection, StoredSelection::Include(_)) {
            backup_v1(paths);
        }
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
    let (chosen, stale_selection) = choose_calendars(paths, &calendars, selection);
    let ids: Vec<i64> = chosen.iter().filter_map(|c| c.id.parse().ok()).collect();
    let events = if ids.is_empty() {
        Vec::new()
    } else {
        match load_events(&paths.calendar_db, &ids, checked) {
            Ok(events) => events,
            Err(err) => {
                return snapshot(
                    false,
                    "error",
                    &format!("A checagem da agenda falhou. {err}"),
                    0,
                    checked_at,
                    Vec::new(),
                );
            }
        }
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
            selected: chosen.iter().any(|picked| picked.id == c.id),
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
    if chosen.is_empty() {
        message.push_str(" Nenhum calendário marcado para o painel.");
    }
    if stale_selection {
        message.push_str(" A seleção salva não bate com nenhuma agenda. Mostrando todas.");
        eprintln!("agenda: seleção salva não bate com nenhuma agenda; mostrando todas");
    }
    snapshot(true, "ok", &message, events.len().min(EVENT_LIMIT), checked_at, rows)
}

fn chosen_from_include<'a>(calendars: &'a [LocalCalendar], ids: &[String]) -> Vec<&'a LocalCalendar> {
    calendars.iter().filter(|calendar| ids.iter().any(|id| id == &calendar.id)).collect()
}

fn excluded_keys(calendars: &[LocalCalendar], ids: &[String]) -> Vec<String> {
    calendars
        .iter()
        .filter(|calendar| !ids.iter().any(|id| id == &calendar.id))
        .map(|calendar| calendar.key.clone())
        .collect()
}

struct DbCalendar {
    id: String,
    title: String,
    self_email: String,
    owner_email: String,
    disabled: bool,
}

fn calendar_table(paths: &CalendarPaths) -> Vec<DbCalendar> {
    sqlite_rows(
        &paths.calendar_db,
        "SELECT c.ROWID, IFNULL(c.title,''), IFNULL(c.self_identity_email,''), \
         IFNULL(c.owner_identity_email,''), IFNULL(s.disabled,0) \
         FROM Calendar c JOIN Store s ON s.ROWID = c.store_id",
    )
    .unwrap_or_default()
    .into_iter()
    .filter_map(|row| {
        if row.len() < 5 {
            return None;
        }
        Some(DbCalendar {
            id: row[0].clone(),
            title: row[1].clone(),
            self_email: row[2].clone(),
            owner_email: row[3].clone(),
            disabled: row[4] == "1",
        })
    })
    .collect()
}

fn is_google_account(self_email: &str, owner_email: &str, emails: &[String]) -> bool {
    let self_email = self_email.trim().to_ascii_lowercase();
    let owner_email = owner_email.trim().to_ascii_lowercase();
    emails.iter().any(|email| email == &self_email || email == &owner_email)
        || self_email.contains("calendar.google.com")
        || owner_email.contains("calendar.google.com")
}

/// A row from the v1 snapshot that is still not a Google calendar we can judge.
/// An iCloud calendar and an empty duplicate of a calendar already on screen are settled.
fn migration_pending(
    rows: &[DbCalendar],
    calendars: &[LocalCalendar],
    ids: &[String],
    emails: &[String],
    seen: &[String],
) -> usize {
    rows.iter().filter(|row| {
        if row.disabled || !seen.iter().any(|id| id == &row.id) {
            return false;
        }
        if ids.iter().any(|id| id == &row.id) || calendars.iter().any(|calendar| calendar.id == row.id) {
            return false;
        }
        let title = row.title.trim().to_ascii_lowercase();
        if !title.is_empty()
            && calendars.iter().any(|calendar| calendar.title.to_ascii_lowercase() == title)
        {
            return false;
        }
        let self_email = row.self_email.trim().to_ascii_lowercase();
        let owner_email = row.owner_email.trim().to_ascii_lowercase();
        let has_email = self_email.contains('@') || owner_email.contains('@');
        if title.is_empty() && !has_email {
            return false;
        }
        let google = is_google_account(&self_email, &owner_email, emails);
        if !google && !title.is_empty() && has_email {
            return false;
        }
        true
    }).count()
}

fn unix_now() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs() as i64).unwrap_or(0)
}

const MIGRATION_REFRESH_LIMIT: u32 = 50;
const MIGRATION_WAIT_S: i64 = 24 * 60 * 60;

fn migration_expired(migrated_at: i64, wait_n: u32) -> bool {
    if wait_n >= MIGRATION_REFRESH_LIMIT {
        return true;
    }
    migrated_at > 0 && unix_now().saturating_sub(migrated_at) >= MIGRATION_WAIT_S
}

#[cfg(test)]
thread_local! {
    static MIGRATION_PROBES: std::cell::Cell<usize> = const { std::cell::Cell::new(0) };
}

fn note_migration_probe() {
    #[cfg(test)]
    MIGRATION_PROBES.with(|count| count.set(count.get() + 1));
}

#[cfg(test)]
fn migration_probes() -> usize {
    MIGRATION_PROBES.with(|count| count.get())
}

fn keep_visible<'a>(calendars: &'a [LocalCalendar], excluded: &[String]) -> Vec<&'a LocalCalendar> {
    calendars
        .iter()
        .filter(|calendar| !excluded.iter().any(|key| key == &calendar.key))
        .collect()
}

fn choose_calendars<'a>(
    paths: &CalendarPaths,
    calendars: &'a [LocalCalendar],
    selection: StoredSelection,
) -> (Vec<&'a LocalCalendar>, bool) {
    match selection {
        StoredSelection::Missing => (calendars.iter().collect(), false),
        StoredSelection::Exclude {
            keys,
            v1_applied,
            rowids_seen,
            migrated_at,
            wait_n,
        } if !v1_applied => {
            let Some(ids) = read_v1_include(paths) else {
                return (keep_visible(calendars, &keys), false);
            };
            if ids.is_empty() {
                return (keep_visible(calendars, &keys), false);
            }
            note_migration_probe();
            let mut excluded = keys.clone();
            // The previous migration saw no calendar to exclude. Rebuild from the backup
            // once the list is non-empty again.
            if keys.is_empty() {
                let matched = chosen_from_include(calendars, &ids);
                let rebuilt = excluded_keys(calendars, &ids);
                if !matched.is_empty() && !rebuilt.is_empty() {
                    excluded = rebuilt;
                }
            }
            // A calendar that already existed at migration time, and was not in the
            // include list, stays off the panel when it shows up later.
            for calendar in calendars {
                let seen = rowids_seen.iter().any(|id| id == &calendar.id);
                let selected = ids.iter().any(|id| id == &calendar.id);
                let already = excluded.iter().any(|key| key == &calendar.key);
                if seen && !selected && !already {
                    excluded.push(calendar.key.clone());
                }
            }
            let table = calendar_table(paths);
            let emails = google_emails(&paths.accounts_db);
            let pending = migration_pending(&table, calendars, &ids, &emails, &rowids_seen);
            let started = if migrated_at > 0 { migrated_at } else { unix_now() };
            let next_n = wait_n.saturating_add(1);
            let expired = migration_expired(started, next_n);
            if expired && pending > 0 {
                eprintln!("agenda: migração v1 encerrada sem achar {pending} agendas");
            }
            let applied = pending == 0 || expired;
            if excluded != keys || applied != v1_applied || next_n != wait_n || started != migrated_at {
                let _ = write_excluded(paths, &excluded, applied, &rowids_seen, started, next_n);
            }
            (keep_visible(calendars, &excluded), false)
        }
        StoredSelection::Exclude { keys, .. } => (keep_visible(calendars, &keys), false),
        StoredSelection::Include(ids) if ids.is_empty() => {
            backup_v1(paths);
            let _ = write_excluded(paths, &excluded_keys(calendars, &ids), true, &[], 0, 0);
            (Vec::new(), false)
        }
        StoredSelection::Include(ids) => {
            backup_v1(paths);
            let matched = chosen_from_include(calendars, &ids);
            if matched.is_empty() {
                (calendars.iter().collect(), true)
            } else {
                let table = calendar_table(paths);
                let seen: Vec<String> = table.iter().map(|row| row.id.clone()).collect();
                let emails = google_emails(&paths.accounts_db);
                let pending = migration_pending(&table, calendars, &ids, &emails, &seen);
                let applied = pending == 0;
                let (migrated_at, wait_n) = if applied { (0, 0) } else { (unix_now(), 1) };
                let _ = write_excluded(paths, &excluded_keys(calendars, &ids), applied, &seen, migrated_at, wait_n);
                (matched, false)
            }
        }
    }
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

pub(crate) fn read_snapshot(paths: &CalendarPaths) -> Option<LocalSnapshot> {
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
    key: String,
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
         (SELECT COUNT(*) FROM CalendarItem i WHERE i.calendar_id = c.ROWID), \
         IFNULL(c.UUID,''), IFNULL(c.external_id,'') \
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
        if row.len() < 11 || row[6] == "1" {
            continue;
        }
        let self_email = row[4].trim().to_ascii_lowercase();
        let owner_email = row[5].trim().to_ascii_lowercase();
        if !is_google_account(&self_email, &owner_email, emails) {
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
            key: stable_key(&row[9], &row[10], &account, &row[0]),
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
         WHERE i.calendar_id IN ({list}) AND i.start_date < {end_apple} \
         AND (IFNULL(i.end_date,0) = 0 OR i.end_date > {start_apple})"
    );
    let mut events = rows_to_events(&sqlite_rows(db, &item_sql)?, now);
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
             ELSE IFNULL(o.occurrence_date,0) END < {end_apple} \
         AND (IFNULL(o.occurrence_end_date,0) = 0 OR IFNULL(o.occurrence_end_date,0) > {start_apple})"
    );
    let rows = sqlite_rows(db, &occ_sql)?;
    events.extend(rows_to_events(&rows, now));
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

fn rows_to_events(rows: &[Vec<String>], now: DateTime<Local>) -> Vec<CalendarEvent> {
    let today = now.date_naive();
    rows.iter().filter_map(|row| row_to_event(row, today, now)).collect()
}

fn row_to_event(row: &[String], today: chrono::NaiveDate, now: DateTime<Local>) -> Option<CalendarEvent> {
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
    let mut offset_days = (event_day - today).num_days();
    let today_start = Local
        .from_local_datetime(&today.and_hms_opt(0, 0, 0)?)
        .single()
        .unwrap_or(now);
    // A span that began yesterday and is still open belongs on today.
    if offset_days < 0 && end > today_start {
        offset_days = 0;
    }
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
    // Today keeps what is still ahead. A timed event that already ended stays off the panel.
    if end <= now {
        return None;
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
    let output = tessera_proc::command_output_timeout(
        Command::new("/usr/bin/sqlite3")
            .arg("-separator")
            .arg("\u{1f}")
            .arg(sqlite_uri(db))
            .arg(sql),
        std::time::Duration::from_secs(15),
    )
    .ok_or_else(|| "sqlite3 excedeu o tempo".to_string())?;
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
                 CREATE TABLE Calendar (ROWID INTEGER PRIMARY KEY, store_id INTEGER, title TEXT, color TEXT, self_identity_email TEXT, owner_identity_email TEXT, UUID TEXT, external_id TEXT);
                 CREATE TABLE CalendarItem (ROWID INTEGER PRIMARY KEY, summary TEXT, start_date REAL, end_date REAL, all_day INTEGER, calendar_id INTEGER);
                 CREATE TABLE OccurrenceCache (event_id INTEGER, calendar_id INTEGER, occurrence_start_date REAL, occurrence_end_date REAL, occurrence_date REAL);
                 INSERT INTO Store VALUES (1, 'Pessoa', 0, {now_apple});
                 INSERT INTO Store VALUES (2, 'iCloud', 0, {now_apple});
                 INSERT INTO Store VALUES (3, 'F1', 0, {now_apple});
                 INSERT INTO Calendar VALUES (10, 1, 'Pessoal', '#1A73E8', 'me@example.com', 'me@example.com', 'cal-pessoal', 'ext-pessoal');
                 INSERT INTO Calendar VALUES (11, 1, 'Pessoal', '#1A73E8', 'me@example.com', 'me@example.com', 'cal-pessoal-vazio', 'ext-vazio');
                 INSERT INTO Calendar VALUES (20, 2, 'Trabalho', '#FF0000', 'a@icloud.com', 'a@icloud.com', 'cal-icloud', 'ext-icloud');
                 INSERT INTO Calendar VALUES (30, 3, 'Formula', '#00AA00', 'x@group.calendar.google.com', '', 'cal-formula', 'ext-formula');
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
    fn a_query_error_keeps_the_previous_event_cache() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        let first = refresh_local(&paths);
        assert!(first.ok, "{}", first.message);
        let before = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(before.contains("Reuniao"), "{before}");
        exec(&paths.calendar_db, "DROP TABLE OccurrenceCache;");
        let snap = refresh_local(&paths);
        assert!(!snap.ok, "{}", snap.message);
        assert_eq!(snap.phase, "error");
        assert_eq!(fs::read_to_string(paths.events_cache_json()).unwrap(), before);
    }

    #[test]
    fn a_rowid_change_keeps_the_saved_calendars() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        fs::create_dir_all(&paths.root).unwrap();
        fs::write(paths.selection_json(), r#"["10"]"#).unwrap();
        let first = refresh_local(&paths);
        assert!(first.ok, "{}", first.message);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(!raw.contains("Corrida"), "{raw}");
        let stored = fs::read_to_string(paths.selection_json()).unwrap();
        assert!(stored.contains("cal-formula"), "{stored}");
        assert!(!stored.contains("cal-pessoal\""), "{stored}");
        exec(
            &paths.calendar_db,
            "UPDATE Calendar SET ROWID = 99 WHERE ROWID = 10;
             UPDATE CalendarItem SET calendar_id = 99 WHERE calendar_id = 10;",
        );
        let second = refresh_local(&paths);
        assert!(second.ok, "{}", second.message);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(!raw.contains("Corrida"), "{raw}");
    }

    #[test]
    fn a_selection_that_matches_nothing_shows_every_calendar() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        fs::create_dir_all(&paths.root).unwrap();
        fs::write(paths.selection_json(), r#"["999"]"#).unwrap();
        let snap = refresh_local(&paths);
        assert!(snap.ok, "{}", snap.message);
        assert!(snap.message.contains("não bate"), "{}", snap.message);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(raw.contains("Corrida"), "{raw}");
        let stored = fs::read_to_string(paths.selection_json()).unwrap();
        assert_eq!(stored, r#"["999"]"#);
        assert!(paths.root.join("selection.v1.json").is_file());
    }

    #[test]
    fn saving_a_rowid_writes_the_excluded_keys() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        exec(
            &paths.calendar_db,
            "INSERT INTO Calendar VALUES (12, 1, 'Casa', '#112233', 'me@example.com', 'me@example.com', 'cal-casa', 'ext-casa');
             INSERT INTO CalendarItem (ROWID, summary, start_date, end_date, all_day, calendar_id)
             SELECT 9, 'Almoco', start_date, end_date, 0, 12 FROM CalendarItem WHERE ROWID = 1;",
        );
        save_selection_local(&paths, &["10".to_string()]).unwrap();
        let stored: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(stored["version"], 2);
        let excluded = stored["excluded"].as_array().unwrap();
        assert_eq!(excluded.len(), 2, "{stored}");
        let text = stored.to_string();
        assert!(text.contains("cal-formula"), "{text}");
        assert!(text.contains("cal-casa"), "{text}");
        let file = std::fs::OpenOptions::new().write(true).open(paths.selection_json()).unwrap();
        file.set_modified(std::time::SystemTime::now() - std::time::Duration::from_secs(120))
            .unwrap();
        drop(file);
        let before = fs::metadata(paths.selection_json()).unwrap().modified().unwrap();
        let _ = refresh_local(&paths);
        let after = fs::metadata(paths.selection_json()).unwrap().modified().unwrap();
        assert_eq!(before, after);
    }

    #[test]
    fn choosing_every_calendar_is_not_replaced_by_the_old_backup() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        fs::create_dir_all(&paths.root).unwrap();
        fs::write(paths.selection_json(), r#"["10"]"#).unwrap();
        let migrated = refresh_local(&paths);
        assert!(migrated.ok, "{}", migrated.message);
        let stored: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(stored["v1_applied"], true);
        assert!(stored["excluded"].as_array().unwrap().len() >= 1, "{stored}");
        save_selection_local(&paths, &["10".to_string(), "30".to_string()]).unwrap();
        let chosen: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(chosen["excluded"].as_array().unwrap().len(), 0, "{chosen}");
        assert_eq!(chosen["v1_applied"], true);
        let snap = refresh_local(&paths);
        assert!(snap.ok, "{}", snap.message);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(raw.contains("Corrida"), "{raw}");
        let after: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(after["excluded"].as_array().unwrap().len(), 0, "{after}");
    }

    #[test]
    fn an_empty_calendar_list_keeps_the_old_selection() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        fs::create_dir_all(&paths.root).unwrap();
        let raw = r#"["10","12"]"#;
        fs::write(paths.selection_json(), raw).unwrap();
        let db = fs::read(&paths.calendar_db).unwrap();
        exec(&paths.calendar_db, "DELETE FROM Calendar;");
        let snap = refresh_local(&paths);
        assert!(!snap.ok, "{}", snap.message);
        assert_eq!(fs::read_to_string(paths.selection_json()).unwrap(), raw);
        assert_eq!(
            fs::read_to_string(paths.root.join("selection.v1.json")).unwrap(),
            raw
        );
        fs::write(&paths.calendar_db, db).unwrap();
        let snap = refresh_local(&paths);
        assert!(snap.ok, "{}", snap.message);
        let stored: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(stored["version"], 2);
        let text = stored.to_string();
        assert!(text.contains("cal-formula"), "{text}");
        assert!(!text.contains("cal-pessoal|"), "{text}");
        assert!(paths.root.join("selection.v1.json").is_file());
    }

    #[test]
    fn a_partial_list_keeps_an_unchecked_calendar_hidden_when_it_returns() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        fs::create_dir_all(&paths.root).unwrap();
        fs::write(paths.selection_json(), r#"["10"]"#).unwrap();
        exec(
            &paths.calendar_db,
            "INSERT INTO Calendar VALUES (12, 1, 'Casa', '#112233', 'me@example.com', 'me@example.com', 'cal-casa', 'ext-casa');
             INSERT INTO CalendarItem (ROWID, summary, start_date, end_date, all_day, calendar_id)
             SELECT 9, 'Almoco', start_date, end_date, 0, 12 FROM CalendarItem WHERE ROWID = 1;
             UPDATE Calendar SET title = '' WHERE ROWID = 30;",
        );
        let first = refresh_local(&paths);
        assert!(first.ok, "{}", first.message);
        let stored: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(stored["v1_applied"], false, "{stored}");
        let seen = stored["v1_rowids_seen"].as_array().unwrap();
        assert!(seen.iter().any(|id| id == "30"), "{stored}");
        let text = stored.to_string();
        assert!(text.contains("cal-casa"), "{text}");
        assert!(!text.contains("cal-formula"), "{text}");
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(!raw.contains("Almoco"), "{raw}");
        assert!(!raw.contains("Corrida"), "{raw}");

        exec(&paths.calendar_db, "UPDATE Calendar SET title = 'Formula' WHERE ROWID = 30;");
        let second = refresh_local(&paths);
        assert!(second.ok, "{}", second.message);
        let stored: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(stored["v1_applied"], true, "{stored}");
        let text = stored.to_string();
        assert!(text.contains("cal-casa"), "{text}");
        assert!(text.contains("cal-formula"), "{text}");
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(!raw.contains("Almoco"), "{raw}");
        assert!(!raw.contains("Corrida"), "{raw}");

        exec(
            &paths.calendar_db,
            "INSERT INTO Calendar VALUES (40, 1, 'Nova', '#445566', 'me@example.com', 'me@example.com', 'cal-nova', 'ext-nova');
             INSERT INTO CalendarItem (ROWID, summary, start_date, end_date, all_day, calendar_id)
             SELECT 8, 'Passeio', start_date, end_date, 0, 40 FROM CalendarItem WHERE ROWID = 1;",
        );
        let third = refresh_local(&paths);
        assert!(third.ok, "{}", third.message);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(raw.contains("Passeio"), "{raw}");
        assert!(!raw.contains("Corrida"), "{raw}");
        let after: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert!(!after.to_string().contains("cal-nova"), "{after}");
    }

    #[test]
    fn a_calendar_that_never_joins_google_closes_the_migration() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        fs::create_dir_all(&paths.root).unwrap();
        fs::write(paths.selection_json(), r#"["10"]"#).unwrap();
        exec(
            &paths.calendar_db,
            "INSERT INTO Calendar VALUES (50, 2, 'Feriados', '#888888', '', '', 'cal-feriados', '');",
        );
        let first = refresh_local(&paths);
        assert!(first.ok, "{}", first.message);
        let stored: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(stored["v1_applied"], false, "{stored}");
        assert!(stored["v1_migrated_at"].as_i64().unwrap_or(0) > 0, "{stored}");
        let mut aged = stored;
        aged["v1_migrated_at"] = serde_json::json!(super::unix_now() - super::MIGRATION_WAIT_S - 60);
        fs::write(paths.selection_json(), serde_json::to_string_pretty(&aged).unwrap()).unwrap();
        let closed = refresh_local(&paths);
        assert!(closed.ok, "{}", closed.message);
        let stored: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(stored["v1_applied"], true, "{stored}");
        let probes = super::migration_probes();
        let _ = refresh_local(&paths);
        assert_eq!(super::migration_probes(), probes);

        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        fs::create_dir_all(&paths.root).unwrap();
        fs::write(paths.selection_json(), r#"["10"]"#).unwrap();
        exec(
            &paths.calendar_db,
            "INSERT INTO Calendar VALUES (50, 2, 'Feriados', '#888888', '', '', 'cal-feriados', '');",
        );
        for _ in 0..50 {
            let snap = refresh_local(&paths);
            assert!(snap.ok, "{}", snap.message);
        }
        let stored: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(paths.selection_json()).unwrap()).unwrap();
        assert_eq!(stored["v1_applied"], true, "{stored}");
        assert!(stored["v1_wait_n"].as_u64().unwrap_or(0) >= 50, "{stored}");
        let probes = super::migration_probes();
        let before = fs::metadata(paths.selection_json()).unwrap().modified().unwrap();
        let _ = refresh_local(&paths);
        let after = fs::metadata(paths.selection_json()).unwrap().modified().unwrap();
        assert_eq!(super::migration_probes(), probes);
        assert_eq!(before, after);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(raw.contains("Reuniao"), "{raw}");
        assert!(!raw.contains("Corrida"), "{raw}");
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
    fn today_drops_finished_timed_events_and_keeps_the_all_day() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        let today = chrono::Local::now().date_naive();
        let now = chrono::Local
            .from_local_datetime(&today.and_hms_opt(18, 0, 0).unwrap())
            .single()
            .unwrap();
        let holiday_apple = today
            .and_hms_opt(0, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp() as f64
            - APPLE_EPOCH_OFFSET;
        let past = now - chrono::Duration::hours(2);
        let past_end = now - chrono::Duration::hours(1);
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
        let snap = refresh_local_at(&paths, now);
        assert!(snap.ok, "{}", snap.message);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        assert!(!raw.contains("Devocional"), "{raw}");
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
    fn a_span_that_started_yesterday_stays_on_today() {
        let temp = tempfile::tempdir().unwrap();
        let paths = fixture(temp.path());
        let today = chrono::Local::now().date_naive();
        let now = chrono::Local
            .from_local_datetime(&today.and_hms_opt(18, 0, 0).unwrap())
            .single()
            .unwrap();
        let start_apple = (today - chrono::Duration::days(1))
            .and_hms_opt(0, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp() as f64
            - APPLE_EPOCH_OFFSET;
        let end_apple = (today + chrono::Duration::days(2))
            .and_hms_opt(0, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp() as f64
            - APPLE_EPOCH_OFFSET;
        let shift_start = chrono::Local
            .from_local_datetime(&(today - chrono::Duration::days(1)).and_hms_opt(22, 0, 0).unwrap())
            .single()
            .unwrap();
        let shift_end = chrono::Local
            .from_local_datetime(&today.and_hms_opt(23, 0, 0).unwrap())
            .single()
            .unwrap();
        let shift_start_apple = shift_start.timestamp() as f64 - APPLE_EPOCH_OFFSET;
        let shift_end_apple = shift_end.timestamp() as f64 - APPLE_EPOCH_OFFSET;
        exec(
            &paths.calendar_db,
            &format!(
                "INSERT INTO CalendarItem VALUES (6, 'Ferias', {start_apple}, {end_apple}, 1, 10);
                 INSERT INTO CalendarItem VALUES (7, 'Plantao', {shift_start_apple}, {shift_end_apple}, 0, 10);"
            ),
        );
        let snap = refresh_local_at(&paths, now);
        assert!(snap.ok, "{}", snap.message);
        let raw = fs::read_to_string(paths.events_cache_json()).unwrap();
        let value: serde_json::Value = serde_json::from_str(&raw).unwrap();
        let events = value["events"].as_array().unwrap();
        let vacation = events.iter().find(|e| e["title"] == "Ferias").expect("ferias");
        assert_eq!(vacation["day_offset"], 0);
        assert_eq!(vacation["all_day"], true);
        let shift = events.iter().find(|e| e["title"] == "Plantao").expect("plantao");
        assert_eq!(shift["day_offset"], 0);
        assert_eq!(shift["start_hhmm"], "2200");
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

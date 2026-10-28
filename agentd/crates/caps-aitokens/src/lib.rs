//! AITOKENS capability — credential discovery + live usage (Cursor/Codex) and
//! Claude statusLine cache → USB wire line.

mod claude_live;
mod codex_live;
mod cursor_live;

use std::collections::HashMap;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use rusqlite::{Connection, OpenFlags};
use serde::Deserialize;

pub const AITOKENS_PUSH_INTERVAL_S: u64 = 30;
/// How often to re-hit Cursor/Codex APIs (USB heartbeat is 30s).
const USAGE_CACHE_TTL_S: u64 = 60;
/// Keep last successful per-source numbers this long when live polls fail (spec: ~6h).
pub const AITOKENS_LAST_GOOD_TTL_S: u64 = 6 * 60 * 60;

#[derive(Debug, Clone)]
struct LastGoodEntry {
    source: AiTokenSource,
    ok_at: Instant,
}

/// When a fresh poll is unavailable, reuse last-good values until `ttl` since last success.
pub(crate) fn merge_last_good(
    store: &mut HashMap<&'static str, LastGoodEntry>,
    fresh: &[AiTokenSource],
    now: Instant,
    ttl: Duration,
) -> Vec<AiTokenSource> {
    let mut out = Vec::with_capacity(fresh.len());
    for src in fresh {
        if src.available {
            store.insert(
                src.id,
                LastGoodEntry {
                    source: src.clone(),
                    ok_at: now,
                },
            );
            out.push(src.clone());
            continue;
        }
        let keep = store.get(src.id).and_then(|entry| {
            now.checked_duration_since(entry.ok_at)
                .filter(|age| *age < ttl)
                .map(|_| entry.source.clone())
        });
        if let Some(cached) = keep {
            out.push(cached);
            continue;
        }
        store.remove(src.id);
        out.push(src.clone());
    }
    out
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SourceKind {
    None,
    Tokens,
    Pct,
}

impl SourceKind {
    fn as_str(self) -> &'static str {
        match self {
            Self::None => "none",
            Self::Tokens => "tokens",
            Self::Pct => "pct",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AiTokenSource {
    pub id: &'static str,
    pub available: bool,
    pub kind: SourceKind,
    pub value: i32,
    pub display: String,
    pub reset: String,
}

impl AiTokenSource {
    pub fn available(
        id: &'static str,
        kind: SourceKind,
        value: i32,
        display: impl Into<String>,
        reset: impl Into<String>,
    ) -> Self {
        Self {
            id,
            available: true,
            kind,
            value,
            display: display.into(),
            reset: reset.into(),
        }
    }

    pub fn unavailable(id: &'static str) -> Self {
        let display = match id {
            "claude" => "abra Code",
            "cursor" => "acesso",
            "codex" => "login",
            _ => "—",
        };
        Self {
            id,
            available: false,
            kind: SourceKind::None,
            value: 0,
            display: display.to_string(),
            reset: String::new(),
        }
    }
}

fn wire_field(field: &str) -> String {
    field.replace('\t', " ")
}

/// Matches legacy Python `agent.capabilities.aitokens.build_line()` exactly.
pub fn encode_aitokens_line(sources: &[AiTokenSource]) -> String {
    let ok = sources.iter().any(|source| source.available);
    let mut fields = vec![
        "AITOKENS".to_string(),
        "v1".to_string(),
        if ok { "1" } else { "0" }.to_string(),
    ];
    for source in sources {
        fields.extend([
            source.id.to_string(),
            if source.available { "1" } else { "0" }.to_string(),
            source.kind.as_str().to_string(),
            source.value.to_string(),
            wire_field(&source.display),
            wire_field(&source.reset),
        ]);
    }
    fields.join("\t")
}

pub fn aitokens_line_changed(last_pushed: Option<&str>, encoded: &str) -> bool {
    last_pushed != Some(encoded)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CursorSource {
    StateDb,
    Keychain,
    AuthJson,
    TokenEnv,
    ApiKeyEnv,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CursorSession {
    pub access: Option<String>,
    pub refresh: Option<String>,
    pub source: CursorSource,
}

#[derive(Debug, Default)]
pub struct CursorCredentials {
    pub token: Option<String>,
    pub api_key: Option<String>,
}

pub struct CursorResolver {
    state_db: PathBuf,
    auth_json_paths: Vec<PathBuf>,
    read_keychain: bool,
}

impl CursorResolver {
    pub fn default_home() -> Self {
        let home = home_dir();
        Self {
            state_db: home
                .join("Library/Application Support/Cursor/User/globalStorage/state.vscdb"),
            auth_json_paths: vec![
                home.join(".cursor/auth.json"),
                home.join(".config/cursor/auth.json"),
            ],
            read_keychain: cfg!(target_os = "macos"),
        }
    }

    /// Test-friendly resolver: only files supplied by the caller are read.
    pub fn with_paths(state_db: PathBuf, auth_json_paths: Vec<PathBuf>) -> Self {
        Self {
            state_db,
            auth_json_paths,
            read_keychain: false,
        }
    }

    /// Cursor candidates in legacy order (sqlite → keychain → auth.json → env).
    /// Callers should try each until usage succeeds — a free/stale sqlite ghost
    /// must not block a working Keychain session.
    pub fn resolve_all(&self, env_credentials: CursorCredentials) -> Vec<CursorSession> {
        let mut sessions = Vec::new();
        if let Some(session) = self.read_state_db() {
            sessions.push(session);
        }
        if self.read_keychain {
            if let Some(session) = self.read_keychain() {
                sessions.push(session);
            }
        }
        if let Some(session) = self.read_auth_json() {
            sessions.push(session);
        }
        if env_credentials
            .api_key
            .as_deref()
            .is_some_and(|key| key.starts_with("crsr_"))
        {
            sessions.push(CursorSession {
                access: env_credentials.api_key.clone(),
                refresh: None,
                source: CursorSource::ApiKeyEnv,
            });
        }
        if let Some(access) = env_credentials
            .token
            .filter(|token| !token.trim().is_empty())
        {
            sessions.push(CursorSession {
                access: Some(access),
                refresh: None,
                source: CursorSource::TokenEnv,
            });
        }
        sessions
    }

    /// Cursor order: state.vscdb → Keychain → auth.json → environment.
    pub fn resolve(&self, env_credentials: CursorCredentials) -> Option<CursorSession> {
        self.resolve_all(env_credentials).into_iter().next()
    }

    fn read_state_db(&self) -> Option<CursorSession> {
        if !self.state_db.is_file() {
            return None;
        }
        // URI + READONLY matches the Python collector and plays nicer with WAL.
        let uri = format!("file:{}?mode=ro", self.state_db.display());
        let db = Connection::open_with_flags(
            &uri,
            OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_URI,
        )
        .ok()?;
        let access = db_value(&db, "cursorAuth/accessToken");
        let refresh = db_value(&db, "cursorAuth/refreshToken");
        (access.is_some() || refresh.is_some()).then_some(CursorSession {
            access,
            refresh,
            source: CursorSource::StateDb,
        })
    }

    fn read_keychain(&self) -> Option<CursorSession> {
        let access = keychain_value("cursor-access-token");
        let refresh = keychain_value("cursor-refresh-token");
        (access.is_some() || refresh.is_some()).then_some(CursorSession {
            access,
            refresh,
            source: CursorSource::Keychain,
        })
    }

    fn read_auth_json(&self) -> Option<CursorSession> {
        for path in &self.auth_json_paths {
            let Ok(raw) = fs::read_to_string(path) else {
                continue;
            };
            let Ok(auth) = serde_json::from_str::<CursorAuthFile>(&raw) else {
                continue;
            };
            let access = auth.access_token.or(auth.access_token_snake);
            let refresh = auth.refresh_token.or(auth.refresh_token_snake);
            if access.is_some() || refresh.is_some() {
                return Some(CursorSession {
                    access,
                    refresh,
                    source: CursorSource::AuthJson,
                });
            }
        }
        None
    }
}

fn db_value(db: &Connection, key: &str) -> Option<String> {
    db.query_row("SELECT value FROM ItemTable WHERE key = ?1", [key], |row| {
        row.get(0)
    })
    .ok()
}

fn keychain_value(service: &str) -> Option<String> {
    let output = tessera_proc::command_output_timeout(
        Command::new("security").args([
            "find-generic-password",
            "-s",
            service,
            "-a",
            "cursor-user",
            "-w",
        ]),
        Duration::from_secs(20),
    )?;
    output
        .status
        .success()
        .then(|| String::from_utf8_lossy(&output.stdout).trim().to_string())
}

#[derive(Deserialize)]
struct CursorAuthFile {
    #[serde(rename = "accessToken")]
    access_token: Option<String>,
    #[serde(rename = "access_token")]
    access_token_snake: Option<String>,
    #[serde(rename = "refreshToken")]
    refresh_token: Option<String>,
    #[serde(rename = "refresh_token")]
    refresh_token_snake: Option<String>,
}

#[derive(Deserialize)]
struct ClaudeStatus {
    generated_at: Option<f64>,
    percent_used: Option<f64>,
    reset_label: Option<String>,
}

const CLAUDE_STATUSLINE_MAX_AGE_S: f64 = 120.0;
const CLAUDE_DESKTOP_MAX_AGE_S: f64 = 24.0 * 3600.0;
const CLAUDE_LAST_KNOWN_MAX_AGE_S: f64 = 7.0 * 24.0 * 3600.0;

struct ClaudeReading {
    observed_at: f64,
    used: f64,
    reset: String,
}

/// StatusLine, the account usage cache, Claude Desktop storage, then the last
/// reading kept for a week. A week-old number is marked old. The panel shows
/// percent remaining.
pub fn read_claude(home: &Path) -> AiTokenSource {
    let tessera = home.join(".tessera");
    let now = now_s();
    let mut fresh: Option<ClaudeReading> = None;
    let mut remembered: Option<ClaudeReading> = None;
    let consider = |into: &mut Option<ClaudeReading>, reading: ClaudeReading| {
        let replace = into
            .as_ref()
            .map(|current| reading.observed_at > current.observed_at)
            .unwrap_or(true);
        if replace {
            *into = Some(reading);
        }
    };
    if let Some(reading) = read_claude_desktop(home, now) {
        consider(&mut fresh, reading);
    }
    if let Some(reading) = read_claude_json(
        &tessera.join("claude_status.json"),
        now,
        CLAUDE_STATUSLINE_MAX_AGE_S,
    ) {
        consider(&mut fresh, reading);
    }
    if let Some(reading) = read_claude_json(
        &tessera.join("claude_usage_cache.json"),
        now,
        180.0,
    ) {
        consider(&mut fresh, reading);
    }
    if let Some(best) = fresh {
        return claude_source(best, true);
    }
    for path in [
        tessera.join("claude_usage_cache.json"),
        tessera.join("claude_status.json"),
    ] {
        if let Some(reading) = read_claude_json(&path, now, CLAUDE_LAST_KNOWN_MAX_AGE_S) {
            consider(&mut remembered, reading);
        }
    }
    remembered
        .map(|reading| claude_source(reading, false))
        .unwrap_or_else(|| AiTokenSource::unavailable("claude"))
}

fn claude_source(reading: ClaudeReading, live: bool) -> AiTokenSource {
    let remaining = (100.0 - reading.used).round().clamp(0.0, 100.0) as i32;
    let display = if live {
        format!("{remaining}%")
    } else {
        format!("{remaining}% antigo")
    };
    AiTokenSource::available(
        "claude",
        SourceKind::Pct,
        remaining,
        display,
        reading.reset.chars().take(15).collect::<String>(),
    )
}

fn read_claude_json(path: &Path, now: f64, max_age: f64) -> Option<ClaudeReading> {
    let raw = fs::read_to_string(path).ok()?;
    let value: ClaudeStatus = serde_json::from_str(&raw).ok()?;
    let used = clamp_used_percent(value.percent_used?)?;
    let observed = value.generated_at.unwrap_or(now);
    let age = now - observed;
    if age > max_age || age < -60.0 {
        return None;
    }
    Some(ClaudeReading {
        observed_at: observed,
        used,
        reset: value.reset_label.unwrap_or_default(),
    })
}

fn read_claude_desktop(home: &Path, now: f64) -> Option<ClaudeReading> {
    let dir = home.join("Library/Application Support/Claude/Local Storage/leveldb");
    let entries = fs::read_dir(&dir).ok()?;
    let mut best: Option<ClaudeReading> = None;
    for entry in entries.flatten() {
        let path = entry.path();
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if matches!(name.as_ref(), "CURRENT" | "LOCK" | "LOG" | "LOG.old")
            || name.starts_with("MANIFEST")
        {
            continue;
        }
        if path.extension().and_then(|ext| ext.to_str()) != Some("log")
            && path.extension().and_then(|ext| ext.to_str()) != Some("ldb")
            && !name.starts_with(|c: char| c.is_ascii_digit())
        {
            continue;
        }
        let Ok(blob) = fs::read(&path) else {
            continue;
        };
        let mtime = entry
            .metadata()
            .ok()
            .and_then(|meta| meta.modified().ok())
            .and_then(|time| time.duration_since(UNIX_EPOCH).ok())
            .map(|d| d.as_secs_f64())
            .unwrap_or(now);
        if let Some(reading) = claude_from_desktop_blob(&blob, mtime, now) {
            if best
                .as_ref()
                .is_none_or(|prev| reading.observed_at >= prev.observed_at)
            {
                best = Some(reading);
            }
        }
    }
    best
}

fn claude_from_desktop_blob(blob: &[u8], mtime: f64, now: f64) -> Option<ClaudeReading> {
    let key = b"\"utilization\"";
    let mut from = 0;
    let mut best: Option<ClaudeReading> = None;
    while let Some(rel) = blob[from..]
        .windows(key.len())
        .position(|window| window == key)
    {
        let at = from + rel;
        let used = norm_used_percent(number_after_key(&blob[at + key.len()..])?)?;
        let window_end = (at + 240).min(blob.len());
        let window_start = at.saturating_sub(240);
        let nearby = &blob[window_start..window_end];
        let resets = number_after_key_in(nearby, b"\"resetsAt\"");
        let mut observed = number_after_key_in(nearby, b"\"observedAt\"").unwrap_or(mtime);
        if observed > 1.0e12 {
            observed /= 1000.0;
        }
        let age = now - observed;
        if (-300.0..=CLAUDE_DESKTOP_MAX_AGE_S).contains(&age)
            && best
                .as_ref()
                .is_none_or(|prev| observed >= prev.observed_at)
        {
            best = Some(ClaudeReading {
                observed_at: observed,
                used,
                reset: reset_label(resets),
            });
        }
        from = at + key.len();
    }
    best
}

fn number_after_key_in(blob: &[u8], key: &[u8]) -> Option<f64> {
    let rel = blob.windows(key.len()).position(|window| window == key)?;
    number_after_key(&blob[rel + key.len()..])
}

fn number_after_key(bytes: &[u8]) -> Option<f64> {
    let mut i = 0;
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    if i < bytes.len() && bytes[i] == b':' {
        i += 1;
    }
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    let start = i;
    if i < bytes.len() && (bytes[i] == b'+' || bytes[i] == b'-') {
        i += 1;
    }
    let mut saw = false;
    while i < bytes.len() && (bytes[i].is_ascii_digit() || bytes[i] == b'.') {
        saw = true;
        i += 1;
    }
    if !saw {
        return None;
    }
    std::str::from_utf8(&bytes[start..i])
        .ok()?
        .parse::<f64>()
        .ok()
}

fn clamp_used_percent(raw: f64) -> Option<f64> {
    if !raw.is_finite() || raw < 0.0 {
        return None;
    }
    Some(raw.clamp(0.0, 100.0))
}

fn norm_used_percent(raw: f64) -> Option<f64> {
    if !raw.is_finite() || raw < 0.0 {
        return None;
    }
    let used = if raw <= 1.5 { raw * 100.0 } else { raw };
    Some(used.clamp(0.0, 100.0))
}

fn reset_label(resets: Option<f64>) -> String {
    let Some(mut ts) = resets else {
        return String::new();
    };
    if ts > 1.0e12 {
        ts /= 1000.0;
    }
    chrono::DateTime::from_timestamp(ts as i64, 0)
        .map(|dt| dt.with_timezone(&chrono::Local).format("%H:%M").to_string())
        .unwrap_or_default()
}

/// Codex auth → ChatGPT WHAM usage (same shape as the legacy Python collector).
pub fn read_codex_auth(home: &Path) -> AiTokenSource {
    codex_live::read_codex(home)
}

/// Resolves Cursor session candidates and fetches live plan usage percentages.
pub fn read_cursor(resolver: &CursorResolver, env_credentials: CursorCredentials) -> AiTokenSource {
    let sessions = resolver.resolve_all(env_credentials);
    if sessions.is_empty() {
        return AiTokenSource::unavailable("cursor");
    }
    let mut last = AiTokenSource::unavailable("cursor");
    for mut session in sessions {
        if session.source == CursorSource::ApiKeyEnv {
            if let Some(key) = session.access.clone().filter(|k| k.starts_with("crsr_")) {
                match cursor_live::exchange_api_key(&key) {
                    Some(exchanged) => session = exchanged,
                    None => {
                        last.display = "CURSOR_API_KEY: exchange falhou".into();
                        continue;
                    }
                }
            }
        }
        let result = cursor_live::fetch_usage(session);
        if result.available {
            return result;
        }
        last = result;
    }
    last
}

/// Codex, Claude, Cursor, in that order, and only the providers the filter leaves on.
fn collect_enabled(
    filter: AiTokensFilter,
    codex: impl FnOnce() -> AiTokenSource,
    claude: impl FnOnce() -> AiTokenSource,
    cursor: impl FnOnce() -> AiTokenSource,
) -> Vec<AiTokenSource> {
    let mut sources = Vec::with_capacity(3);
    if filter.codex {
        sources.push(codex());
    }
    if filter.claude {
        sources.push(claude());
    }
    if filter.cursor {
        sources.push(cursor());
    }
    sources
}

fn refresh_claude_if_enabled(filter: AiTokensFilter, refresh: impl FnOnce()) {
    if filter.claude {
        refresh();
    }
}

/// Builds the fixed legacy order: Codex, Claude, Cursor.
fn collect_sources_with_cursor(
    home: &Path,
    resolver: &CursorResolver,
    cursor_env: CursorCredentials,
    filter: AiTokensFilter,
) -> Vec<AiTokenSource> {
    collect_enabled(
        filter,
        || read_codex_auth(home),
        || read_claude(home),
        || read_cursor(resolver, cursor_env),
    )
}

/// Which providers to include in the USB line (agent collect toggles).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AiTokensFilter {
    pub cursor: bool,
    pub claude: bool,
    pub codex: bool,
}

impl Default for AiTokensFilter {
    fn default() -> Self {
        Self {
            cursor: true,
            claude: true,
            codex: true,
        }
    }
}

pub fn collect_sources() -> Vec<AiTokenSource> {
    collect_sources_filtered(AiTokensFilter::default())
}

pub fn collect_sources_filtered(filter: AiTokensFilter) -> Vec<AiTokenSource> {
    let home = home_dir();
    let cursor_env = CursorCredentials {
        token: env::var("CURSOR_TOKEN").ok(),
        api_key: env::var("CURSOR_API_KEY").ok(),
    };
    collect_sources_with_cursor(&home, &CursorResolver::default_home(), cursor_env, filter)
}

fn home_dir() -> PathBuf {
    env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."))
}

fn claude_status_stamp() -> Option<SystemTime> {
    let home = env::var_os("HOME")?;
    fs::metadata(PathBuf::from(home).join(".tessera/claude_status.json"))
        .and_then(|meta| meta.modified())
        .ok()
}

fn now_s() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_secs_f64()
}

struct LineCache {
    line: Option<String>,
    at: Instant,
    last_good: HashMap<&'static str, LastGoodEntry>,
    claude_stamp: Option<SystemTime>,
}

pub struct AiTokensCapability {
    last_pushed: Option<String>,
    filter: AiTokensFilter,
    cache: Arc<Mutex<LineCache>>,
    inflight: Arc<AtomicBool>,
}

impl AiTokensCapability {
    pub fn new() -> Self {
        Self::with_filter(AiTokensFilter::default())
    }

    pub fn with_filter(filter: AiTokensFilter) -> Self {
        Self {
            last_pushed: None,
            filter,
            cache: Arc::new(Mutex::new(LineCache {
                line: None,
                at: Instant::now()
                    .checked_sub(Duration::from_secs(USAGE_CACHE_TTL_S))
                    .unwrap_or_else(Instant::now),
                last_good: HashMap::new(),
                claude_stamp: None,
            })),
            inflight: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn set_filter(&mut self, filter: AiTokensFilter) {
        if self.filter != filter {
            self.filter = filter;
            if let Ok(mut cache) = self.cache.lock() {
                cache.line = None;
                cache.at = Instant::now()
                    .checked_sub(Duration::from_secs(USAGE_CACHE_TTL_S))
                    .unwrap_or_else(Instant::now);
            }
        }
    }

    /// Blocks on the network. Doctor and a one-shot collect use this.
    pub fn build_line(&mut self) -> String {
        let home = home_dir();
        refresh_claude_if_enabled(self.filter, || claude_live::refresh_usage_cache(&home));
        let fresh = collect_sources_filtered(self.filter);
        self.store(fresh)
    }

    /// Last line, kicking a worker when it is stale. The USB session must not wait on HTTP.
    pub fn cached_or_kick(&mut self) -> String {
        if let Some(line) = self.fresh_line() {
            return line;
        }
        self.kick();
        self.cache
            .lock()
            .ok()
            .and_then(|cache| cache.line.clone())
            .unwrap_or_else(|| self.placeholder())
    }

    fn fresh_line(&self) -> Option<String> {
        let cache = self.cache.lock().ok()?;
        let stamp = claude_status_stamp();
        if stamp != cache.claude_stamp {
            return None;
        }
        let line = cache.line.clone()?;
        (cache.at.elapsed() < Duration::from_secs(USAGE_CACHE_TTL_S)).then_some(line)
    }

    fn kick(&self) {
        if self.inflight.swap(true, Ordering::SeqCst) {
            return;
        }
        let filter = self.filter;
        let cache = Arc::clone(&self.cache);
        let inflight = Arc::clone(&self.inflight);
        thread::spawn(move || {
            struct Done(Arc<AtomicBool>);
            impl Drop for Done {
                fn drop(&mut self) {
                    self.0.store(false, Ordering::SeqCst);
                }
            }
            let _done = Done(inflight);
            let home = home_dir();
            refresh_claude_if_enabled(filter, || claude_live::refresh_usage_cache(&home));
            let fresh = collect_sources_filtered(filter);
            let summary = fresh
                .iter()
                .map(|source| format!("{} {}", source.id, source.display))
                .collect::<Vec<_>>()
                .join(", ");
            eprintln!("Tessera: aitokens refresh ({summary})");
            let stamp = claude_status_stamp();
            let mut guard = match cache.lock() {
                Ok(guard) => guard,
                Err(poison) => poison.into_inner(),
            };
            let now = Instant::now();
            let sources = merge_last_good(
                &mut guard.last_good,
                &fresh,
                now,
                Duration::from_secs(AITOKENS_LAST_GOOD_TTL_S),
            );
            guard.line = Some(encode_aitokens_line(&sources));
            guard.at = now;
            guard.claude_stamp = stamp;
        });
    }

    fn store(&self, fresh: Vec<AiTokenSource>) -> String {
        let mut guard = match self.cache.lock() {
            Ok(guard) => guard,
            Err(poison) => poison.into_inner(),
        };
        let now = Instant::now();
        let sources = merge_last_good(
            &mut guard.last_good,
            &fresh,
            now,
            Duration::from_secs(AITOKENS_LAST_GOOD_TTL_S),
        );
        let line = encode_aitokens_line(&sources);
        guard.line = Some(line.clone());
        guard.at = now;
        guard.claude_stamp = claude_status_stamp();
        line
    }

    fn placeholder(&self) -> String {
        let mut sources = Vec::new();
        if self.filter.codex {
            sources.push(AiTokenSource::unavailable("codex"));
        }
        if self.filter.claude {
            sources.push(AiTokenSource::unavailable("claude"));
        }
        if self.filter.cursor {
            sources.push(AiTokenSource::unavailable("cursor"));
        }
        encode_aitokens_line(&sources)
    }

    pub fn last_pushed_line(&self) -> Option<&str> {
        self.last_pushed.as_deref()
    }

    pub fn note_pushed(&mut self, line: &str) {
        self.last_pushed = Some(line.to_string());
    }

    pub fn handle_line(&mut self, text: &str) -> Vec<String> {
        if text.trim() == "AITOKENS?" {
            vec![self.cached_or_kick()]
        } else {
            Vec::new()
        }
    }
}

impl Default for AiTokensCapability {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preserves_last_good_values_when_next_poll_unavailable() {
        let good = AiTokenSource::available("cursor", SourceKind::Pct, 80, "10% · 20%", "01 jan");
        let bad = AiTokenSource::unavailable("cursor");
        let mut store = HashMap::new();
        let now = Instant::now();
        store.insert(
            "cursor",
            LastGoodEntry {
                source: good.clone(),
                ok_at: now,
            },
        );
        let merged = merge_last_good(
            &mut store,
            &[bad],
            now,
            Duration::from_secs(AITOKENS_LAST_GOOD_TTL_S),
        );
        let c = merged.iter().find(|s| s.id == "cursor").unwrap();
        assert!(c.available, "must keep serving numbers, not empty");
        assert_eq!(c.display, "10% · 20%");
        assert_eq!(c.value, 80);
    }

    #[test]
    fn clears_last_good_after_ttl_hours() {
        let good = AiTokenSource::available("cursor", SourceKind::Pct, 80, "10% · 20%", "01 jan");
        let bad = AiTokenSource::unavailable("cursor");
        let now = Instant::now();
        let ok_at = now
            .checked_sub(Duration::from_secs(AITOKENS_LAST_GOOD_TTL_S + 60))
            .expect("instant supports 6h subtract");
        let mut store = HashMap::new();
        store.insert(
            "cursor",
            LastGoodEntry {
                source: good,
                ok_at,
            },
        );
        let merged = merge_last_good(
            &mut store,
            std::slice::from_ref(&bad),
            now,
            Duration::from_secs(AITOKENS_LAST_GOOD_TTL_S),
        );
        let c = merged.iter().find(|s| s.id == "cursor").unwrap();
        assert!(!c.available);
        assert_ne!(c.display, "10% · 20%");
        assert!(!store.contains_key("cursor"));
    }

    #[test]
    fn collect_sources_includes_cursor_slot_when_auth_json_present() {
        let temp = tempfile::tempdir().unwrap();
        let auth = temp.path().join("auth.json");
        fs::write(&auth, r#"{"accessToken":"test-access"}"#).unwrap();

        let resolver = CursorResolver::with_paths(temp.path().join("missing.vscdb"), vec![auth]);
        let sources = collect_sources_with_cursor(
            temp.path(),
            &resolver,
            CursorCredentials::default(),
            AiTokensFilter::default(),
        );
        let source = sources
            .iter()
            .find(|source| source.id == "cursor")
            .expect("Cursor source");

        assert_eq!(source.id, "cursor");
        // Live API will fail with a fake token; still report the cursor slot.
        assert!(!source.available);
        assert!(
            source.display.contains("indisponível")
                || source.display == "abra Cursor"
                || source.display == "—"
                || source.display == "acesso",
            "display={}",
            source.display
        );
    }

    #[test]
    fn claude_only_filter_skips_cursor_and_codex_reads() {
        let mut codex_reads = 0;
        let mut claude_reads = 0;
        let mut cursor_reads = 0;
        let claude_only = AiTokensFilter {
            cursor: false,
            claude: true,
            codex: false,
        };
        let sources = collect_enabled(
            claude_only,
            || {
                codex_reads += 1;
                AiTokenSource::unavailable("codex")
            },
            || {
                claude_reads += 1;
                AiTokenSource::unavailable("claude")
            },
            || {
                cursor_reads += 1;
                AiTokenSource::unavailable("cursor")
            },
        );
        assert_eq!(codex_reads, 0);
        assert_eq!(cursor_reads, 0);
        assert_eq!(claude_reads, 1);
        assert_eq!(sources.len(), 1);
        assert_eq!(sources[0].id, "claude");

        let mut refresh = 0;
        refresh_claude_if_enabled(claude_only, || refresh += 1);
        assert_eq!(refresh, 1);
        refresh_claude_if_enabled(
            AiTokensFilter {
                cursor: true,
                claude: false,
                codex: true,
            },
            || refresh += 1,
        );
        assert_eq!(refresh, 1);
    }

    #[test]
    fn claude_status_one_percent_stays_one_percent_used() {
        let temp = tempfile::tempdir().unwrap();
        let tessera = temp.path().join(".tessera");
        fs::create_dir_all(&tessera).unwrap();
        fs::write(
            tessera.join("claude_status.json"),
            format!(
                r#"{{"generated_at":{},"percent_used":1,"reset_label":"19:50"}}"#,
                now_s()
            ),
        )
        .unwrap();
        let source = read_claude(temp.path());
        assert!(source.available);
        assert_eq!(source.value, 99);
        assert_eq!(source.display, "99%");
    }

    #[test]
    fn claude_keeps_last_known_reading_for_a_week() {
        let temp = tempfile::tempdir().unwrap();
        let tessera = temp.path().join(".tessera");
        fs::create_dir_all(&tessera).unwrap();
        let generated = now_s() - 36.0 * 3600.0;
        fs::write(
            tessera.join("claude_usage_cache.json"),
            format!(
                r#"{{"generated_at":{generated},"percent_used":8,"reset_label":"19:50","source":"claude_desktop_local"}}"#
            ),
        )
        .unwrap();
        let source = read_claude(temp.path());
        assert!(source.available);
        assert_eq!(source.value, 92);
        assert_eq!(source.display, "92% antigo");
        assert_eq!(source.reset, "19:50");
    }

    #[test]
    fn claude_statusline_beats_an_older_cache() {
        let temp = tempfile::tempdir().unwrap();
        let tessera = temp.path().join(".tessera");
        fs::create_dir_all(&tessera).unwrap();
        let now = now_s();
        fs::write(
            tessera.join("claude_usage_cache.json"),
            format!(
                r#"{{"generated_at":{},"percent_used":8,"reset_label":"19:50"}}"#,
                now - 3600.0
            ),
        )
        .unwrap();
        fs::write(
            tessera.join("claude_status.json"),
            format!(
                r#"{{"generated_at":{},"percent_used":40,"reset_label":"06:10"}}"#,
                now - 30.0
            ),
        )
        .unwrap();
        let source = read_claude(temp.path());
        assert_eq!(source.value, 60);
        assert_eq!(source.display, "60%");
        assert_eq!(source.reset, "06:10");
    }

    #[test]
    fn claude_desktop_blob_reports_remaining() {
        let temp = tempfile::tempdir().unwrap();
        let dir = temp
            .path()
            .join("Library/Application Support/Claude/Local Storage/leveldb");
        fs::create_dir_all(&dir).unwrap();
        let observed = (now_s() * 1000.0) as u64;
        fs::write(
            dir.join("000001.log"),
            format!(r#"noise "utilization":25,"observedAt":{observed},"resetsAt":1793200000"#),
        )
        .unwrap();
        let source = read_claude(temp.path());
        assert!(source.available, "display={}", source.display);
        assert_eq!(source.value, 75);
    }

    #[test]
    #[ignore = "hits live Cursor/Codex APIs"]
    fn live_collect_sources_prints_wire_line() {
        let line = encode_aitokens_line(&collect_sources());
        eprintln!("live AITOKENS line: {line}");
        assert!(line.starts_with("AITOKENS\tv1\t"));
        assert!(
            line.contains("\tcursor\t1\t") || line.contains("\tcodex\t1\t"),
            "expected at least one live source: {line}"
        );
    }
}

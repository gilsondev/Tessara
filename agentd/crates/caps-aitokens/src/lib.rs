//! AITOKENS capability — credential discovery + live usage (Cursor/Codex) and
//! Claude statusLine cache → USB wire line.

mod codex_live;
mod cursor_live;

use std::collections::HashMap;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
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
        Self {
            id,
            available: false,
            kind: SourceKind::None,
            value: 0,
            display: "—".to_string(),
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
    let output = Command::new("security")
        .args([
            "find-generic-password",
            "-s",
            service,
            "-a",
            "cursor-user",
            "-w",
        ])
        .output()
        .ok()?;
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

/// Reads Claude Code's statusLine output. Claude Desktop cache support is
/// intentionally read-only: the legacy cache is accepted when it uses this
/// same JSON shape, but stale values are not published.
pub fn read_claude_status(tessera_home: &Path) -> AiTokenSource {
    let status = tessera_home.join("claude_status.json");
    let cache = tessera_home.join("claude_usage_cache.json");
    for path in [status, cache] {
        let Ok(raw) = fs::read_to_string(path) else {
            continue;
        };
        let Ok(value) = serde_json::from_str::<ClaudeStatus>(&raw) else {
            continue;
        };
        let Some(used) = value.percent_used else {
            continue;
        };
        let fresh = value.generated_at.is_none_or(|at| now_s() - at <= 120.0);
        if !fresh || !used.is_finite() {
            continue;
        }
        let remaining = (100.0 - used).round().clamp(0.0, 100.0) as i32;
        return AiTokenSource::available(
            "claude",
            SourceKind::Pct,
            remaining,
            format!("{remaining}%"),
            value
                .reset_label
                .unwrap_or_default()
                .chars()
                .take(15)
                .collect::<String>(),
        );
    }
    AiTokenSource::unavailable("claude")
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

/// Builds the fixed legacy order: Codex, Claude, Cursor.
fn collect_sources_with_cursor(
    home: &Path,
    resolver: &CursorResolver,
    cursor_env: CursorCredentials,
) -> Vec<AiTokenSource> {
    vec![
        read_codex_auth(home),
        read_claude_status(&home.join(".tessera")),
        read_cursor(resolver, cursor_env),
    ]
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
    let all = collect_sources_with_cursor(&home, &CursorResolver::default_home(), cursor_env);
    all.into_iter()
        .filter(|s| match s.id {
            "cursor" => filter.cursor,
            "claude" => filter.claude,
            "codex" => filter.codex,
            _ => true,
        })
        .collect()
}

fn home_dir() -> PathBuf {
    env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."))
}

fn now_s() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_secs_f64()
}

pub struct AiTokensCapability {
    last_pushed: Option<String>,
    cached_line: Option<String>,
    cached_at: Instant,
    last_good: HashMap<&'static str, LastGoodEntry>,
    filter: AiTokensFilter,
}

impl AiTokensCapability {
    pub fn new() -> Self {
        Self::with_filter(AiTokensFilter::default())
    }

    pub fn with_filter(filter: AiTokensFilter) -> Self {
        Self {
            last_pushed: None,
            cached_line: None,
            cached_at: Instant::now()
                .checked_sub(Duration::from_secs(USAGE_CACHE_TTL_S))
                .unwrap_or_else(Instant::now),
            last_good: HashMap::new(),
            filter,
        }
    }

    pub fn set_filter(&mut self, filter: AiTokensFilter) {
        if self.filter != filter {
            self.filter = filter;
            // Force refresh on next build so toggles apply immediately.
            self.cached_line = None;
        }
    }

    pub fn build_line(&mut self) -> String {
        if let Some(line) = &self.cached_line {
            if self.cached_at.elapsed() < Duration::from_secs(USAGE_CACHE_TTL_S) {
                return line.clone();
            }
        }
        let fresh = collect_sources_filtered(self.filter);
        let now = Instant::now();
        let sources = merge_last_good(
            &mut self.last_good,
            &fresh,
            now,
            Duration::from_secs(AITOKENS_LAST_GOOD_TTL_S),
        );
        let line = encode_aitokens_line(&sources);
        let summary: Vec<String> = sources
            .iter()
            .map(|s| {
                if s.available {
                    format!("{}={}", s.id, s.display)
                } else {
                    format!("{}=—", s.id)
                }
            })
            .collect();
        eprintln!("Tessera: aitokens refresh ({})", summary.join(", "));
        self.cached_line = Some(line.clone());
        self.cached_at = now;
        line
    }

    pub fn last_pushed_line(&self) -> Option<&str> {
        self.last_pushed.as_deref()
    }

    pub fn note_pushed(&mut self, line: &str) {
        self.last_pushed = Some(line.to_string());
    }

    pub fn handle_line(&mut self, text: &str) -> Vec<String> {
        (text.trim() == "AITOKENS?")
            .then(|| vec![self.build_line()])
            .unwrap_or_default()
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
        let sources =
            collect_sources_with_cursor(temp.path(), &resolver, CursorCredentials::default());
        let source = sources
            .iter()
            .find(|source| source.id == "cursor")
            .expect("Cursor source");

        assert_eq!(source.id, "cursor");
        // Live API will fail with a fake token; still report the cursor slot.
        assert!(!source.available);
        assert!(
            source.display.contains("401")
                || source.display.contains("refresh")
                || source.display == "—",
            "display={}",
            source.display
        );
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

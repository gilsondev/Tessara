//! AITOKENS capability — local credential discovery and wire encoding.
//!
//! Credential discovery deliberately does not imply quota availability: until a
//! provider usage request is implemented, a discovered Cursor/Codex session is
//! reported as unavailable instead of fabricating a 100% quota.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use rusqlite::{Connection, OpenFlags};
use serde::Deserialize;

pub const AITOKENS_PUSH_INTERVAL_S: u64 = 30;

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

    /// Cursor order: state.vscdb → Keychain → auth.json → environment.
    pub fn resolve(&self, env_credentials: CursorCredentials) -> Option<CursorSession> {
        if let Some(session) = self.read_state_db() {
            return Some(session);
        }
        if self.read_keychain {
            if let Some(session) = self.read_keychain() {
                return Some(session);
            }
        }
        if let Some(session) = self.read_auth_json() {
            return Some(session);
        }
        if env_credentials
            .api_key
            .as_deref()
            .is_some_and(|key| key.starts_with("crsr_"))
        {
            return Some(CursorSession {
                access: env_credentials.api_key,
                refresh: None,
                source: CursorSource::ApiKeyEnv,
            });
        }
        env_credentials
            .token
            .filter(|token| !token.trim().is_empty())
            .map(|access| CursorSession {
                access: Some(access),
                refresh: None,
                source: CursorSource::TokenEnv,
            })
    }

    fn read_state_db(&self) -> Option<CursorSession> {
        if !self.state_db.is_file() {
            return None;
        }
        let db =
            Connection::open_with_flags(&self.state_db, OpenFlags::SQLITE_OPEN_READ_ONLY).ok()?;
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

/// Detects Codex authentication without calling ChatGPT. A detected token
/// remains unavailable until the WHAM usage client is ported.
pub fn read_codex_auth(home: &Path) -> AiTokenSource {
    let path = home.join(".codex/auth.json");
    let Ok(raw) = fs::read_to_string(path) else {
        return AiTokenSource::unavailable("codex");
    };
    let has_access = serde_json::from_str::<serde_json::Value>(&raw)
        .ok()
        .is_some_and(|json| {
            json.pointer("/tokens/access_token")
                .and_then(serde_json::Value::as_str)
                .is_some_and(|token| !token.is_empty())
        });
    let _detected = has_access;
    AiTokenSource::unavailable("codex")
}

/// Builds the fixed legacy order: Codex, Claude, Cursor.
pub fn collect_sources() -> Vec<AiTokenSource> {
    let home = home_dir();
    let tessera_home = home.join(".tessera");
    let cursor_env = CursorCredentials {
        token: env::var("CURSOR_TOKEN").ok(),
        api_key: env::var("CURSOR_API_KEY").ok(),
    };
    let _cursor_session = CursorResolver::default_home().resolve(cursor_env);
    vec![
        read_codex_auth(&home),
        read_claude_status(&tessera_home),
        AiTokenSource::unavailable("cursor"),
    ]
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
}

impl AiTokensCapability {
    pub fn new() -> Self {
        Self { last_pushed: None }
    }

    pub fn build_line(&self) -> String {
        encode_aitokens_line(&collect_sources())
    }

    pub fn last_pushed_line(&self) -> Option<&str> {
        self.last_pushed.as_deref()
    }

    pub fn note_pushed(&mut self, line: &str) {
        self.last_pushed = Some(line.to_string());
    }

    pub fn handle_line(&self, text: &str) -> Vec<String> {
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

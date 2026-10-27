//! Machine scan — detect local apps and capability prerequisites.

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};
use tessera_caps_aitokens::{CursorCredentials, CursorResolver};
use tessera_caps_calendar::CalendarPaths;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ScanState {
    Ready,
    NeedsAuth,
    Missing,
}

impl ScanState {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Ready => "ready",
            Self::NeedsAuth => "needs_auth",
            Self::Missing => "missing",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ScanItem {
    pub id: String,
    pub state: ScanState,
    pub enabled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ScanResult {
    pub scanned_at: String,
    pub found: Vec<ScanItem>,
}

/// Paths used for detection (override in tests).
#[derive(Debug, Clone)]
pub struct ScanRoots {
    pub home: PathBuf,
    /// `~/Applications` (per-user installs).
    pub applications: PathBuf,
    /// `/Applications` (system installs — Spotify, Cursor, Claude, …).
    pub system_applications: PathBuf,
}

impl ScanRoots {
    pub fn from_home(home: PathBuf) -> Self {
        Self {
            applications: home.join("Applications"),
            system_applications: PathBuf::from("/Applications"),
            home,
        }
    }

    pub fn default_home() -> Self {
        let home = std::env::var_os("HOME")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("."));
        Self::from_home(home)
    }

    /// True if `Name.app` exists in user or system Applications.
    pub fn has_app(&self, bundle_name: &str) -> bool {
        self.applications.join(bundle_name).is_dir()
            || self.system_applications.join(bundle_name).is_dir()
    }
}

pub fn run_scan(roots: &ScanRoots, config_enabled: ScanConfigEnabled) -> ScanResult {
    let found = vec![
        detect_cursor(roots, config_enabled.aitokens),
        detect_claude(roots, config_enabled.aitokens),
        detect_codex(roots, config_enabled.aitokens),
        detect_media(roots, config_enabled.media),
        detect_focus(roots, config_enabled.focus),
        detect_calendar(roots, config_enabled.calendar),
    ];
    ScanResult {
        scanned_at: iso_now(),
        found,
    }
}

#[derive(Debug, Clone, Copy, Default)]
pub struct ScanConfigEnabled {
    pub media: bool,
    pub focus: bool,
    pub aitokens: bool,
    pub calendar: bool,
}

impl ScanConfigEnabled {
    pub fn toggles_from_scan(found: &[ScanItem]) -> Self {
        let item = |id: &str| found.iter().find(|f| f.id == id);
        let on = |id: &str| item(id).is_some_and(|f| f.state != ScanState::Missing);
        Self {
            media: on("media"),
            focus: on("focus"),
            aitokens: on("cursor") || on("claude") || on("codex"),
            calendar: on("calendar"),
        }
    }
}

fn detect_cursor(roots: &ScanRoots, enabled: bool) -> ScanItem {
    let state_db = roots
        .home
        .join("Library/Application Support/Cursor/User/globalStorage/state.vscdb");
    let auth = roots.home.join(".cursor/auth.json");
    let resolver = CursorResolver::with_paths(
        state_db.clone(),
        vec![auth.clone(), roots.home.join(".config/cursor/auth.json")],
    );
    let has_install = roots.has_app("Cursor.app") || state_db.is_file() || auth.is_file();
    let state = if !has_install {
        ScanState::Missing
    } else if resolver.resolve(CursorCredentials::default()).is_some() {
        ScanState::Ready
    } else {
        ScanState::NeedsAuth
    };
    ScanItem {
        id: "cursor".to_string(),
        state,
        enabled,
    }
}

fn detect_claude(roots: &ScanRoots, enabled: bool) -> ScanItem {
    let has_app = roots.has_app("Claude.app");
    let tessera = roots.home.join(".tessera");
    let status_ok = claude_quota_ready(&tessera.join("claude_status.json"))
        || claude_quota_ready(&tessera.join("claude_usage_cache.json"));
    let state = if !has_app {
        ScanState::Missing
    } else if status_ok {
        ScanState::Ready
    } else {
        ScanState::NeedsAuth
    };
    ScanItem {
        id: "claude".to_string(),
        state,
        enabled,
    }
}

fn detect_codex(roots: &ScanRoots, enabled: bool) -> ScanItem {
    let auth = roots.home.join(".codex/auth.json");
    let state = if !auth.is_file() {
        ScanState::Missing
    } else if auth_has_access_token(&auth) {
        ScanState::Ready
    } else {
        ScanState::NeedsAuth
    };
    ScanItem {
        id: "codex".to_string(),
        state,
        enabled,
    }
}

fn claude_quota_ready(path: &Path) -> bool {
    let Ok(raw) = fs::read_to_string(path) else {
        return false;
    };
    serde_json::from_str::<serde_json::Value>(&raw)
        .ok()
        .and_then(|json| json.get("percent_used").and_then(serde_json::Value::as_f64))
        .is_some()
}

fn auth_has_access_token(path: &Path) -> bool {
    let Ok(raw) = fs::read_to_string(path) else {
        return false;
    };
    serde_json::from_str::<serde_json::Value>(&raw)
        .ok()
        .and_then(|json| {
            json.pointer("/tokens/access_token")
                .and_then(serde_json::Value::as_str)
                .map(|t| !t.is_empty())
        })
        .unwrap_or(false)
}

fn detect_media(roots: &ScanRoots, enabled: bool) -> ScanItem {
    let state = if roots.has_app("Spotify.app") || roots.has_app("Music.app") {
        ScanState::Ready
    } else {
        ScanState::Missing
    };
    ScanItem {
        id: "media".to_string(),
        state,
        enabled,
    }
}

fn detect_focus(roots: &ScanRoots, enabled: bool) -> ScanItem {
    let mode_db = roots
        .home
        .join("Library/DoNotDisturb/DB/ModeConfigurations.json");
    let assertions = roots.home.join("Library/DoNotDisturb/DB/Assertions.json");
    let state = if !mode_db.is_file() && !assertions.is_file() {
        ScanState::Missing
    } else if file_readable(&mode_db) || file_readable(&assertions) {
        ScanState::Ready
    } else {
        ScanState::NeedsAuth
    };
    ScanItem {
        id: "focus".to_string(),
        state,
        enabled,
    }
}

fn file_readable(path: &Path) -> bool {
    path.is_file() && fs::File::open(path).is_ok()
}

fn detect_calendar(roots: &ScanRoots, enabled: bool) -> ScanItem {
    let paths = CalendarPaths::under_home(roots.home.clone());
    let state = match tessera_caps_calendar::probe(&paths) {
        tessera_caps_calendar::LocalProbe::Ready => ScanState::Ready,
        tessera_caps_calendar::LocalProbe::Missing => ScanState::Missing,
        tessera_caps_calendar::LocalProbe::Blocked
        | tessera_caps_calendar::LocalProbe::Empty
        | tessera_caps_calendar::LocalProbe::Error => ScanState::NeedsAuth,
    };
    ScanItem {
        id: "calendar".to_string(),
        state,
        enabled,
    }
}

fn iso_now() -> String {
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    format!("{secs}")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn roots(temp: &tempfile::TempDir) -> ScanRoots {
        let mut r = ScanRoots::from_home(temp.path().to_path_buf());
        // Isolate from the real /Applications so host Spotify/Music don't leak into tests.
        r.system_applications = temp.path().join("SystemApplications");
        r
    }

    #[test]
    fn cursor_ready_when_auth_json_has_token() {
        let temp = tempfile::tempdir().unwrap();
        let auth = temp.path().join(".cursor/auth.json");
        fs::create_dir_all(auth.parent().unwrap()).unwrap();
        fs::write(&auth, r#"{"accessToken":"x"}"#).unwrap();
        let r = run_scan(
            &roots(&temp),
            ScanConfigEnabled {
                aitokens: true,
                ..Default::default()
            },
        );
        let cursor = r.found.iter().find(|i| i.id == "cursor").unwrap();
        assert_eq!(cursor.state, ScanState::Ready);
    }

    #[test]
    fn codex_missing_without_auth_file() {
        let temp = tempfile::tempdir().unwrap();
        let r = run_scan(&roots(&temp), ScanConfigEnabled::default());
        let codex = r.found.iter().find(|i| i.id == "codex").unwrap();
        assert_eq!(codex.state, ScanState::Missing);
    }

    #[test]
    fn media_ready_when_spotify_app_present() {
        let temp = tempfile::tempdir().unwrap();
        fs::create_dir_all(temp.path().join("Applications/Spotify.app")).unwrap();
        let r = run_scan(
            &roots(&temp),
            ScanConfigEnabled {
                media: true,
                ..Default::default()
            },
        );
        let media = r.found.iter().find(|i| i.id == "media").unwrap();
        assert_eq!(media.state, ScanState::Ready);
    }

    #[test]
    fn media_ready_when_system_spotify_present() {
        let temp = tempfile::tempdir().unwrap();
        let mut r = roots(&temp);
        let sys = temp.path().join("SystemApps");
        fs::create_dir_all(sys.join("Spotify.app")).unwrap();
        r.system_applications = sys;
        let result = run_scan(
            &r,
            ScanConfigEnabled {
                media: true,
                ..Default::default()
            },
        );
        let media = result.found.iter().find(|i| i.id == "media").unwrap();
        assert_eq!(media.state, ScanState::Ready);
    }

    #[test]
    fn toggles_from_scan_disable_missing_capabilities() {
        let found = vec![
            ScanItem {
                id: "cursor".to_string(),
                state: ScanState::Ready,
                enabled: true,
            },
            ScanItem {
                id: "claude".to_string(),
                state: ScanState::Missing,
                enabled: false,
            },
            ScanItem {
                id: "codex".to_string(),
                state: ScanState::Missing,
                enabled: false,
            },
            ScanItem {
                id: "media".to_string(),
                state: ScanState::Missing,
                enabled: false,
            },
            ScanItem {
                id: "focus".to_string(),
                state: ScanState::Ready,
                enabled: true,
            },
            ScanItem {
                id: "calendar".to_string(),
                state: ScanState::Missing,
                enabled: false,
            },
        ];
        let t = ScanConfigEnabled::toggles_from_scan(&found);
        assert!(t.aitokens);
        assert!(!t.media);
        assert!(t.focus);
        assert!(!t.calendar);
    }

    #[test]
    fn claude_needs_auth_until_status_has_a_percent() {
        let temp = tempfile::tempdir().unwrap();
        fs::create_dir_all(temp.path().join("Applications/Claude.app")).unwrap();
        let tessera = temp.path().join(".tessera");
        fs::create_dir_all(&tessera).unwrap();
        fs::write(
            tessera.join("claude_status.json"),
            r#"{"percent_used":null,"source":"claude_statusline"}"#,
        )
        .unwrap();
        let result = run_scan(&roots(&temp), ScanConfigEnabled::default());
        let claude = result.found.iter().find(|i| i.id == "claude").unwrap();
        assert_eq!(claude.state, ScanState::NeedsAuth);

        fs::write(
            tessera.join("claude_status.json"),
            r#"{"percent_used":8,"source":"claude_statusline"}"#,
        )
        .unwrap();
        let result = run_scan(&roots(&temp), ScanConfigEnabled::default());
        let claude = result.found.iter().find(|i| i.id == "claude").unwrap();
        assert_eq!(claude.state, ScanState::Ready);
    }

    #[test]
    fn calendar_missing_without_mac_database() {
        let temp = tempfile::tempdir().unwrap();
        let result = run_scan(&roots(&temp), ScanConfigEnabled::default());
        let calendar = result.found.iter().find(|i| i.id == "calendar").unwrap();
        assert_eq!(calendar.state, ScanState::Missing);
    }
}

//! MEDIA capability — Now Playing encode + panel commands.

use tessara_protocol::media_line;

pub const MEDIA_PUSH_INTERVAL_S: u64 = 8;
/// Poll watcher this often when media is enabled; push immediately if wire line changed.
pub const MEDIA_POLL_INTERVAL_S: u64 = 2;

/// True when `encoded` should be pushed (never pushed before, or differs from last push).
pub fn media_line_changed(last_pushed: Option<&str>, encoded: &str) -> bool {
    last_pushed != Some(encoded)
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MediaInfo {
    pub state: MediaState,
    pub title: String,
    pub artist: String,
    pub app: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MediaState {
    Playing,
    Paused,
    Stopped,
}

impl MediaState {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Playing => "playing",
            Self::Paused => "paused",
            Self::Stopped => "stopped",
        }
    }

    pub fn parse(raw: &str) -> Self {
        match raw {
            "playing" => Self::Playing,
            "paused" => Self::Paused,
            _ => Self::Stopped,
        }
    }
}

/// Golden-compatible encoder (delegates to `tessara_protocol::media_line`).
pub fn encode_media_line(
    ok: bool,
    state: &str,
    title: &str,
    artist: &str,
    app: &str,
) -> String {
    media_line(ok, state, title, artist, app)
}

pub trait MediaWatcher {
    /// `None` → wire OK=0 line (watcher unavailable).
    fn read(&mut self) -> Option<MediaInfo>;
}

/// First ship: always reports stopped with empty metadata (OK=1).
pub struct StubWatcher;

impl MediaWatcher for StubWatcher {
    fn read(&mut self) -> Option<MediaInfo> {
        Some(MediaInfo {
            state: MediaState::Stopped,
            title: String::new(),
            artist: String::new(),
            app: String::new(),
        })
    }
}

#[cfg(target_os = "macos")]
mod macos {
    use super::{MediaInfo, MediaState, MediaWatcher};
    use std::process::Command;

    const OSASCRIPT: &str = r#"
    set titleText to ""
    set artistText to ""
    set appName to ""
    set playState to "stopped"
    try
      if application "Music" is running then
        tell application "Music"
          if player state is playing then
            set playState to "playing"
          else if player state is paused then
            set playState to "paused"
          end if
          if player state is not stopped then
            set titleText to name of current track
            set artistText to artist of current track
            set appName to "Music"
          end if
        end tell
      end if
    end try
    if titleText is "" then
      try
        if application "Spotify" is running then
          tell application "Spotify"
            if player state is playing then
              set playState to "playing"
            else if player state is paused then
              set playState to "paused"
            end if
            if player state is not stopped then
              set titleText to name of current track
              set artistText to artist of current track
              set appName to "Spotify"
            end if
          end tell
        end if
      end try
    end if
    return playState & tab & titleText & tab & artistText & tab & appName
    "#;

    pub struct MacNowPlayingWatcher;

    impl MediaWatcher for MacNowPlayingWatcher {
        fn read(&mut self) -> Option<MediaInfo> {
            let out = Command::new("osascript")
                .args(["-e", OSASCRIPT])
                .output()
                .ok()?;
            if !out.status.success() {
                return None;
            }
            let line = String::from_utf8_lossy(&out.stdout);
            let line = line.trim_end_matches('\n');
            let mut parts = line.split('\t');
            let state = parts.next().unwrap_or("stopped");
            let title = parts.next().unwrap_or("").to_string();
            let artist = parts.next().unwrap_or("").to_string();
            let app = parts.next().unwrap_or("").to_string();
            Some(MediaInfo {
                state: MediaState::parse(state),
                title,
                artist,
                app,
            })
        }
    }

    impl Default for MacNowPlayingWatcher {
        fn default() -> Self {
            Self
        }
    }
}

pub fn default_watcher() -> Box<dyn MediaWatcher> {
    #[cfg(target_os = "macos")]
    {
        Box::new(macos::MacNowPlayingWatcher::default())
    }
    #[cfg(not(target_os = "macos"))]
    {
        Box::new(StubWatcher)
    }
}

pub struct MediaCapability {
    watcher: Box<dyn MediaWatcher>,
    last_pushed: Option<String>,
}

impl MediaCapability {
    pub fn new(watcher: Box<dyn MediaWatcher>) -> Self {
        Self {
            watcher,
            last_pushed: None,
        }
    }

    pub fn last_pushed_line(&self) -> Option<&str> {
        self.last_pushed.as_deref()
    }

    pub fn note_pushed(&mut self, line: &str) {
        self.last_pushed = Some(line.to_string());
    }

    pub fn with_default_watcher() -> Self {
        Self::new(default_watcher())
    }

    pub fn build_line(&mut self) -> String {
        match self.watcher.read() {
            None => encode_media_line(false, "stopped", "", "", ""),
            Some(info) => encode_media_line(
                true,
                info.state.as_str(),
                &info.title,
                &info.artist,
                &info.app,
            ),
        }
    }

    /// Panel inbound line → outbound USB lines (may be empty).
    pub fn handle_line(&mut self, text: &str) -> Vec<String> {
        let raw = text.trim();
        if raw.is_empty() {
            return vec![];
        }
        if raw == "MEDIA?" || raw.starts_with("MEDIA?") {
            return vec![self.build_line()];
        }
        if raw.starts_with("MEDIA_COVER") {
            return vec![];
        }
        let parts: Vec<&str> = raw.split('\t').collect();
        if parts.first().copied() != Some("MEDIA") || parts.len() < 2 {
            return vec![];
        }
        let cmd = parts[1].trim().to_ascii_uppercase();
        match cmd.as_str() {
            "PLAYPAUSE" | "NEXT" | "PREV" | "PREVIOUS" => {
                // Transport stub until controls are ported.
                vec![self.build_line()]
            }
            _ => vec![],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encode_fail_matches_python() {
        assert_eq!(
            encode_media_line(false, "stopped", "", "", ""),
            "MEDIA\tv1\t0\t\t\t\t"
        );
    }

    #[test]
    fn media_line_changed_compares_full_wire() {
        let playing = "MEDIA\tv1\t1\tplaying\tSong\tArtist\tMusic";
        let stopped = "MEDIA\tv1\t1\tstopped\t\t\t";
        assert!(media_line_changed(None, playing));
        assert!(!media_line_changed(Some(playing), playing));
        assert!(media_line_changed(Some(stopped), playing));
    }
}

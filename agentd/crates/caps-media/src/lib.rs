//! MEDIA capability — Now Playing encode + panel commands.

mod cover;

use std::sync::mpsc::{self, Receiver, Sender};
use std::thread;

use tessera_protocol::media_line;

pub const MEDIA_PUSH_INTERVAL_S: u64 = 8;
/// Slow safety-net poll when DNC wakes are available (mac_events). Was 2s; events own latency now.
pub const MEDIA_POLL_INTERVAL_S: u64 = 8;
/// Cover edge length pushed to the panel (RGB565).
pub const MEDIA_COVER_EDGE: u32 = 200;

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

/// Golden-compatible encoder (delegates to `tessera_protocol::media_line`).
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
            let out = tessera_proc::command_output_timeout(
                Command::new("osascript").args(["-e", OSASCRIPT]),
                std::time::Duration::from_secs(2),
            )?;
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
        Box::new(macos::MacNowPlayingWatcher)
    }
    #[cfg(not(target_os = "macos"))]
    {
        Box::new(StubWatcher)
    }
}

pub struct MediaCapability {
    watcher: Box<dyn MediaWatcher>,
    last_pushed: Option<String>,
    last_info: Option<MediaInfo>,
    cover_tx: Sender<MediaInfo>,
    cover_rx: Receiver<(String, Vec<String>)>,
    cover_ready: Option<(String, Vec<String>)>,
    cover_inflight: Option<String>,
}

fn start_cover_worker() -> (Sender<MediaInfo>, Receiver<(String, Vec<String>)>) {
    let (job_tx, job_rx) = mpsc::channel::<MediaInfo>();
    let (out_tx, out_rx) = mpsc::channel();
    thread::spawn(move || {
        while let Ok(info) = job_rx.recv() {
            let key = cover::cover_key(&info);
            let lines = cover::fetch_cover_bytes(&info)
                .and_then(|bytes| cover::jpeg_or_png_to_rgb565(&bytes, MEDIA_COVER_EDGE))
                .map(|rgb| {
                    cover::encode_cover_chunks(&key, MEDIA_COVER_EDGE, MEDIA_COVER_EDGE, &rgb)
                })
                .unwrap_or_default();
            let _ = out_tx.send((key, lines));
        }
    });
    (job_tx, out_rx)
}

impl MediaCapability {
    pub fn new(watcher: Box<dyn MediaWatcher>) -> Self {
        let (cover_tx, cover_rx) = start_cover_worker();
        Self {
            watcher,
            last_pushed: None,
            last_info: None,
            cover_tx,
            cover_rx,
            cover_ready: None,
            cover_inflight: None,
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
            None => {
                self.last_info = None;
                encode_media_line(false, "stopped", "", "", "")
            }
            Some(info) => {
                let line = encode_media_line(
                    true,
                    info.state.as_str(),
                    &info.title,
                    &info.artist,
                    &info.app,
                );
                self.last_info = Some(info);
                line
            }
        }
    }

    /// Cover for the last `build_line` track. The download runs on another thread;
    /// this returns only a cover that is already encoded. `None` while it is in flight.
    pub fn build_cover_chunks(&mut self) -> Option<(String, Vec<String>)> {
        while let Ok(ready) = self.cover_rx.try_recv() {
            if self.cover_inflight.as_deref() == Some(ready.0.as_str()) {
                self.cover_inflight = None;
            }
            if !ready.1.is_empty() {
                self.cover_ready = Some(ready);
            }
        }
        let info = self.last_info.clone()?;
        if info.state == MediaState::Stopped || info.title.is_empty() {
            return None;
        }
        let key = cover::cover_key(&info);
        if let Some((ready_key, lines)) = &self.cover_ready {
            if ready_key == &key {
                return Some((key.clone(), lines.clone()));
            }
        }
        if self.cover_inflight.as_deref() != Some(key.as_str()) && self.cover_tx.send(info).is_ok()
        {
            self.cover_inflight = Some(key);
        }
        None
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
        if let Some(rest) = raw.strip_prefix("MEDIA_COVER?") {
            let lines = self
                .build_cover_chunks()
                .map(|(_, lines)| lines)
                .unwrap_or_default();
            let rest = rest.trim_start_matches('\t');
            if rest.is_empty() {
                return lines;
            }
            let mut parts = rest.splitn(2, '\t');
            let key = parts.next().unwrap_or("");
            let list = parts.next().unwrap_or("");
            let indexes: Vec<usize> = list.split(',').filter_map(|text| text.parse().ok()).collect();
            return cover::cover_lines_for(lines, key, &indexes);
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
    fn cover_request_returns_before_the_download() {
        let mut cap = MediaCapability::new(Box::new(StubWatcher));
        cap.last_info = Some(MediaInfo {
            state: MediaState::Playing,
            title: "Orvalho".into(),
            artist: "Celiana Satyl".into(),
            app: "Spotify".into(),
        });
        let started = std::time::Instant::now();
        assert!(cap.build_cover_chunks().is_none());
        assert!(started.elapsed() < std::time::Duration::from_millis(200));
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

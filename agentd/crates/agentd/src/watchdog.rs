//! If the USB session stops sending heartbeats, exit so launchd starts a clean process.

use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

/// Three missed 8s heartbeats, plus a little slack.
pub const STALE_SECS: u64 = 30;

static LAST_BEAT: AtomicU64 = AtomicU64::new(0);

pub fn note_heartbeat() {
    LAST_BEAT.store(now_s(), Ordering::Relaxed);
}

pub fn clear_session() {
    LAST_BEAT.store(0, Ordering::Relaxed);
}

pub fn should_restart(last_beat_unix: u64, now_unix: u64) -> bool {
    last_beat_unix != 0 && now_unix.saturating_sub(last_beat_unix) > STALE_SECS
}

fn now_s() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// Clears the beat when the session function returns, including after a disconnect.
/// A thread stuck inside the session never drops this, so the watchdog still fires.
pub struct SessionGuard;

impl Drop for SessionGuard {
    fn drop(&mut self) {
        clear_session();
    }
}

pub fn spawn() {
    std::thread::spawn(|| loop {
        std::thread::sleep(Duration::from_secs(5));
        let last = LAST_BEAT.load(Ordering::Relaxed);
        if should_restart(last, now_s()) {
            eprintln!("watchdog: sessão USB sem heartbeat há {STALE_SECS} s, reiniciando o agent");
            std::process::exit(75);
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn idle_wait_for_the_cable_does_not_restart() {
        assert!(!should_restart(0, 1_000));
    }

    #[test]
    fn a_quiet_session_restarts_after_thirty_seconds() {
        assert!(!should_restart(1_000, 1_030));
        assert!(should_restart(1_000, 1_031));
    }
}

//! USB session: handshake, panel commands, capability push heartbeats.

use std::io::{self, Read, Write};
use std::sync::Arc;
use std::time::{Duration, Instant};

use tessera_caps_focus::{
    focus_line_changed, FocusCapability, FOCUS_POLL_INTERVAL_S, FOCUS_PUSH_INTERVAL_S,
};
use tessera_caps_aitokens::{
    aitokens_line_changed, AiTokensCapability, AiTokensFilter, AITOKENS_PUSH_INTERVAL_S,
};
use tessera_caps_calendar::{
    calendar_line_changed, CalendarCapability, CALENDAR_PUSH_INTERVAL_S,
};
use tessera_caps_todoist::{
    todoist_line_changed, TodoistCapability, TODOIST_PUSH_INTERVAL_S,
};
use tessera_caps_media::{
    media_line_changed, MediaCapability, MEDIA_POLL_INTERVAL_S, MEDIA_PUSH_INTERVAL_S,
};
use tessera_logbuf::SharedLogBuf;
use tessera_protocol::agent_ok_line;
use tessera_usb::{open_tessera_port, write_handshake, write_lines, PortName, UsbError};

use tessera_profile::{read_config, ProfilePaths};

use crate::ipc::UsbHub;
use crate::logutil::{self, describe_usb, widget_of_line};
use crate::mac_events;

/// Firmware marks the Mac offline after ~15s without an `AGENT` line (`agent::kFreshMs`).
const AGENT_HEARTBEAT_INTERVAL_S: u64 = 8;

const ONCE_READ_S: u64 = 2;
const RETRY_S: f64 = 1.5;
const CONFIG_RELOAD_S: u64 = 2;

type Logs = Option<SharedLogBuf>;

fn load_caps(paths: &ProfilePaths) -> (bool, bool, bool, AiTokensFilter, bool, bool) {
    match read_config(&paths.config_toml()) {
        Ok(c) => (
            c.media,
            c.focus,
            c.aitokens,
            AiTokensFilter {
                cursor: c.aitokens_cursor,
                claude: c.aitokens_claude,
                codex: c.aitokens_codex,
            },
            c.calendar,
            c.todoist,
        ),
        Err(_) => (true, true, true, AiTokensFilter::default(), true, true),
    }
}

fn log_from_panel(logs: &Logs, line: &str) {
    let w = widget_of_line(line);
    let kind = if line.contains('?') {
        "query"
    } else {
        "cmd"
    };
    // Never echo secrets (Todoist token migration / TOKEN).
    let summary = if line.starts_with("TODOIST_TOKEN\t") || line.starts_with("TOKEN\t") {
        "token (recebido)".to_string()
    } else {
        describe_usb(line)
    };
    logutil::info(
        logs,
        format!("usb painel→Mac · {w} · {kind} · {summary}"),
    );
}

fn log_to_panel(logs: &Logs, widget: &str, action: &str, line: &str) {
    logutil::info(
        logs,
        format!(
            "usb Mac→painel · {widget} · {action} · {}",
            describe_usb(line)
        ),
    );
}

pub fn print_candidate_ports() {
    let ports = tessera_usb::list_candidate_ports();
    if ports.is_empty() {
        eprintln!("Tessera: nenhuma porta USB Tessera (usbmodem / Tessera)");
        return;
    }
    eprintln!("Tessera: portas candidatas:");
    for PortName(name) in &ports {
        eprintln!("  {name}");
    }
}

pub fn run_once(path: &str) -> Result<(), UsbError> {
    eprintln!("Tessera: abrindo {path}");
    let mut port = open_tessera_port(path)?;
    write_handshake(&mut port, &agent_ok_line())?;
    eprintln!("Tessera: handshake → {}", agent_ok_line());

    let deadline = Instant::now() + Duration::from_secs(ONCE_READ_S);
    let mut scratch = [0u8; 256];
    while Instant::now() < deadline {
        match port.read(&mut scratch) {
            Ok(0) => continue,
            Ok(n) => {
                let chunk = String::from_utf8_lossy(&scratch[..n]);
                for line in chunk.lines() {
                    let line = line.trim();
                    if !line.is_empty() {
                        eprintln!("Tessera: ← {line}");
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
            Err(e) => return Err(e.into()),
        }
    }
    eprintln!("Tessera: --once concluído ({ONCE_READ_S}s leitura)");
    Ok(())
}

fn set_usb_link(hub: &Option<Arc<UsbHub>>, up: bool) {
    if let Some(hub) = hub {
        hub.set_up(up);
    }
}

fn take_reboot(hub: &Option<Arc<UsbHub>>) -> bool {
    hub.as_ref()
        .is_some_and(|h| h.reboot.swap(false, std::sync::atomic::Ordering::SeqCst))
}

fn wait_port_drop(port: &mut dyn Read, budget: Duration) -> bool {
    let start = Instant::now();
    let mut buf = [0u8; 64];
    while start.elapsed() < budget {
        match port.read(&mut buf) {
            Ok(0) => return true,
            Err(e) if e.kind() == io::ErrorKind::TimedOut => continue,
            Err(_) => return true,
            Ok(_) => {}
        }
    }
    false
}

/// Ask the panel to `esp_restart`. Returns true when the cable dropped.
/// If it stays up, pulse DTR and return false — the chip did not reboot.
fn restart_panel(mut port: impl Read + Write, path: &str, logs: &Logs) -> bool {
    logutil::info(logs, "usb: restart do painel (REBOOT)");
    if write_lines(&mut port, &["REBOOT".to_string()]).is_ok() {
        log_to_panel(logs, "panel", "reboot", "REBOOT");
    }
    let dropped = wait_port_drop(&mut port, Duration::from_millis(1500));
    drop(port);
    if dropped {
        logutil::info(logs, "usb: painel reiniciando");
        return true;
    }
    match tessera_usb::pulse_dtr(path) {
        Ok(()) => logutil::info(logs, format!("usb: pulso DTR em {path}")),
        Err(e) => logutil::warn(logs, format!("usb: pulso DTR falhou — {e}")),
    }
    false
}

/// Blocking loop on a fixed serial path (daemon background thread).
pub fn run_session(
    path: &str,
    profile_paths: ProfilePaths,
    usb_link: Option<Arc<UsbHub>>,
    logs: Logs,
) -> Result<(), UsbError> {
    logutil::info(&logs, format!("usb: abrindo sessão {path}"));
    if let Some(hub) = &usb_link {
        hub.set_port(path);
    }
    let mut port = open_tessera_port(path)?;
    write_handshake(&mut port, &agent_ok_line())?;
    set_usb_link(&usb_link, true);
    log_to_panel(&logs, "agent", "handshake", &agent_ok_line());

    let (
        mut media_enabled,
        mut focus_enabled,
        mut aitokens_enabled,
        filter,
        mut calendar_enabled,
        mut todoist_enabled,
    ) = load_caps(&profile_paths);
    logutil::info(
        &logs,
        format!(
            "usb: caps media={media_enabled} focus={focus_enabled} aitokens={aitokens_enabled} calendar={calendar_enabled} todoist={todoist_enabled}"
        ),
    );
    let mut media = MediaCapability::with_default_watcher();
    let mut focus = FocusCapability::new();
    let mut aitokens = AiTokensCapability::with_filter(filter);
    let mut calendar = CalendarCapability::default();
    let mut todoist = TodoistCapability::default();
    let mut last_config_reload = Instant::now();
    let wake_rx = mac_events::start();
    let mut buf = Vec::<u8>::new();
    let mut scratch = [0u8; 256];
    let mut last_media_heartbeat = Instant::now()
        .checked_sub(Duration::from_secs(MEDIA_PUSH_INTERVAL_S))
        .unwrap_or_else(Instant::now);
    let mut last_media_poll = Instant::now()
        .checked_sub(Duration::from_secs(MEDIA_POLL_INTERVAL_S))
        .unwrap_or_else(Instant::now);
    let mut last_focus_heartbeat = Instant::now()
        .checked_sub(Duration::from_secs(FOCUS_PUSH_INTERVAL_S))
        .unwrap_or_else(Instant::now);
    let mut last_focus_poll = Instant::now()
        .checked_sub(Duration::from_secs(FOCUS_POLL_INTERVAL_S))
        .unwrap_or_else(Instant::now);
    let mut last_cover_key = String::new();
    let mut last_aitokens_heartbeat = Instant::now()
        .checked_sub(Duration::from_secs(AITOKENS_PUSH_INTERVAL_S))
        .unwrap_or_else(Instant::now);
    let mut last_calendar_heartbeat = Instant::now()
        .checked_sub(Duration::from_secs(CALENDAR_PUSH_INTERVAL_S))
        .unwrap_or_else(Instant::now);
    let mut last_todoist_heartbeat = Instant::now()
        .checked_sub(Duration::from_secs(TODOIST_PUSH_INTERVAL_S))
        .unwrap_or_else(Instant::now);
    let mut last_agent_heartbeat = Instant::now();
    // Push macOS appearance on connect so Auto mode matches before the first change.
    let mut last_theme_dark = {
        let dark = mac_events::macos_prefers_dark();
        let line = mac_events::theme_line(dark);
        write_lines(&mut port, &[line.clone()])?;
        log_to_panel(&logs, "theme", "push", &line);
        Some(dark)
    };

    loop {
        if take_reboot(&usb_link) {
            set_usb_link(&usb_link, false);
            let dropped = restart_panel(port, path, &logs);
            if let Some(hub) = &usb_link {
                hub.reboot_result.store(
                    if dropped {
                        crate::ipc::REBOOT_DROPPED
                    } else {
                        crate::ipc::REBOOT_DTR
                    },
                    std::sync::atomic::Ordering::SeqCst,
                );
            }
            return Err(UsbError::Io(io::Error::new(
                io::ErrorKind::Other,
                "painel reiniciando",
            )));
        }
        if last_config_reload.elapsed() >= Duration::from_secs(CONFIG_RELOAD_S) {
            let (m, f, a, filter, cal, td) = load_caps(&profile_paths);
            if (m, f, a, cal, td)
                != (
                    media_enabled,
                    focus_enabled,
                    aitokens_enabled,
                    calendar_enabled,
                    todoist_enabled,
                )
            {
                logutil::info(
                    &logs,
                    format!(
                        "usb: caps atualizadas media={m} focus={f} aitokens={a} calendar={cal} todoist={td}"
                    ),
                );
            }
            media_enabled = m;
            focus_enabled = f;
            aitokens_enabled = a;
            calendar_enabled = cal;
            todoist_enabled = td;
            aitokens.set_filter(filter);
            last_config_reload = Instant::now();
        }
        match port.read(&mut scratch) {
            Ok(0) => {}
            Ok(n) => {
                buf.extend_from_slice(&scratch[..n]);
                while let Some(pos) = buf.iter().position(|&b| b == b'\n') {
                    let line_bytes: Vec<u8> = buf.drain(..=pos).collect();
                    let text =
                        String::from_utf8_lossy(&line_bytes[..line_bytes.len().saturating_sub(1)]);
                    let text = text.trim();
                    if text.is_empty() {
                        continue;
                    }
                    // Queries (`NAME?`) always answer so the panel isn't blind when a
                    // capability toggle is off; mutations + push stay gated by config.
                    let is_query = text.contains('?');
                    log_from_panel(&logs, text);
                    if text.starts_with("MEDIA") && (media_enabled || is_query) {
                        let replies = media.handle_line(text);
                        if !replies.is_empty() {
                            write_lines(&mut port, &replies)?;
                            for reply in &replies {
                                if reply.starts_with("MEDIA\t") {
                                    media.note_pushed(reply);
                                    log_to_panel(
                                        &logs,
                                        "media",
                                        if is_query { "reply" } else { "push" },
                                        reply,
                                    );
                                } else if reply.starts_with("COVER") {
                                    log_to_panel(&logs, "media", "cover", reply);
                                }
                            }
                            last_media_heartbeat = Instant::now();
                            last_media_poll = Instant::now();
                        }
                    } else if text.starts_with("FOCUS") && (focus_enabled || is_query) {
                        let replies = focus.handle_line(text);
                        if !replies.is_empty() {
                            write_lines(&mut port, &replies)?;
                            for reply in &replies {
                                if reply.starts_with("FOCUS\t") {
                                    focus.note_pushed(reply);
                                    log_to_panel(
                                        &logs,
                                        "focus",
                                        if is_query { "reply" } else { "push" },
                                        reply,
                                    );
                                }
                            }
                            last_focus_heartbeat = Instant::now();
                            last_focus_poll = Instant::now();
                        }
                    } else if text.starts_with("AITOKENS") && (aitokens_enabled || is_query) {
                        let replies = aitokens.handle_line(text);
                        if !replies.is_empty() {
                            write_lines(&mut port, &replies)?;
                            for reply in &replies {
                                if reply.starts_with("AITOKENS\t") {
                                    aitokens.note_pushed(reply);
                                    log_to_panel(
                                        &logs,
                                        "aitokens",
                                        if is_query { "reply" } else { "push" },
                                        reply,
                                    );
                                }
                            }
                            last_aitokens_heartbeat = Instant::now();
                        }
                    } else if text.starts_with("CALENDAR") && (calendar_enabled || is_query) {
                        let replies = calendar.handle_line(text);
                        if !replies.is_empty() {
                            write_lines(&mut port, &replies)?;
                            for reply in &replies {
                                if reply.starts_with("CALENDAR\t") {
                                    calendar.note_pushed(reply);
                                    log_to_panel(
                                        &logs,
                                        "calendar",
                                        if is_query { "reply" } else { "push" },
                                        reply,
                                    );
                                }
                            }
                        }
                    } else if text.starts_with("TODOIST") && (todoist_enabled || is_query) {
                        let replies = todoist.handle_line(text);
                        if !replies.is_empty() {
                            let is_close_ack = replies
                                .iter()
                                .any(|r| r.starts_with("TODOIST_CLOSE\tv1\t"));
                            if is_close_ack {
                                write_lines(&mut port, &replies)?;
                                for reply in &replies {
                                    log_to_panel(&logs, "todoist", "close-ack", reply);
                                }
                            } else {
                                // Always answer — dropping a duplicate once left the panel
                                // with empty lists until the Mac cache happened to change.
                                write_lines(&mut port, &replies)?;
                                todoist.note_pushed(&replies);
                                last_todoist_heartbeat = Instant::now();
                                log_to_panel(
                                    &logs,
                                    "todoist",
                                    if is_query { "reply" } else { "push" },
                                    &format!("{} linha(s)", replies.len()),
                                );
                            }
                        }
                    } else if text.starts_with("THEME") {
                        let dark = mac_events::macos_prefers_dark();
                        let line = mac_events::theme_line(dark);
                        write_lines(&mut port, &[line.clone()])?;
                        last_theme_dark = Some(dark);
                        log_to_panel(&logs, "theme", "reply", &line);
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
            Err(e) => {
                set_usb_link(&usb_link, false);
                logutil::warn(&logs, format!("usb: link caiu — {e}"));
                return Err(e.into());
            }
        }

        if last_agent_heartbeat.elapsed() >= Duration::from_secs(AGENT_HEARTBEAT_INTERVAL_S) {
            write_lines(&mut port, &[agent_ok_line()])?;
            last_agent_heartbeat = Instant::now();
            // Heartbeat is frequent — skip Activity spam; stderr only.
            eprintln!("Tessera: AGENT heartbeat → painel");
        }

        // DNC / Focus / Theme notifications → immediate push (coalesced).
        let (wake_media, wake_focus, wake_theme) = mac_events::drain(&wake_rx);
        if wake_focus {
            std::thread::sleep(Duration::from_millis(80));
        }
        if wake_theme {
            std::thread::sleep(Duration::from_millis(50));
            let dark = mac_events::macos_prefers_dark();
            if last_theme_dark != Some(dark) {
                let line = mac_events::theme_line(dark);
                write_lines(&mut port, &[line.clone()])?;
                last_theme_dark = Some(dark);
                log_to_panel(&logs, "theme", "push", &line);
            }
        }

        if media_enabled {
            let poll_due =
                last_media_poll.elapsed() >= Duration::from_secs(MEDIA_POLL_INTERVAL_S);
            let heartbeat_due =
                last_media_heartbeat.elapsed() >= Duration::from_secs(MEDIA_PUSH_INTERVAL_S);
            if wake_media || poll_due || heartbeat_due {
                if poll_due {
                    last_media_poll = Instant::now();
                }
                let line = media.build_line();
                let changed = media_line_changed(media.last_pushed_line(), &line);
                let push = wake_media || heartbeat_due || changed;
                if push {
                    write_lines(&mut port, &[line.clone()])?;
                    media.note_pushed(&line);
                    last_media_heartbeat = Instant::now();
                    let action = if wake_media {
                        "wake"
                    } else if changed {
                        "change"
                    } else {
                        "heartbeat"
                    };
                    // Heartbeat unchanged is noisy — log only wake/change.
                    if action != "heartbeat" || changed {
                        log_to_panel(&logs, "media", action, &line);
                    }
                    if let Some((key, chunks)) = media.build_cover_chunks() {
                        if key != last_cover_key {
                            write_lines(&mut port, &chunks)?;
                            last_cover_key = key;
                            logutil::info(
                                &logs,
                                format!(
                                    "usb Mac→painel · media · cover · {} chunk(s)",
                                    chunks.len()
                                ),
                            );
                        }
                    }
                }
            }
        }

        if focus_enabled {
            let poll_due =
                last_focus_poll.elapsed() >= Duration::from_secs(FOCUS_POLL_INTERVAL_S);
            let heartbeat_due =
                last_focus_heartbeat.elapsed() >= Duration::from_secs(FOCUS_PUSH_INTERVAL_S);
            let db_dirty = (wake_focus || poll_due) && focus.poll_dirty();
            if poll_due {
                last_focus_poll = Instant::now();
            }
            if wake_focus || poll_due || heartbeat_due {
                let line = focus.build_line();
                let changed = focus_line_changed(focus.last_pushed_line(), &line);
                let push = wake_focus || heartbeat_due || db_dirty || changed;
                if push {
                    write_lines(&mut port, &[line.clone()])?;
                    focus.note_pushed(&line);
                    last_focus_heartbeat = Instant::now();
                    let action = if wake_focus || db_dirty {
                        "wake"
                    } else if changed {
                        "change"
                    } else {
                        "heartbeat"
                    };
                    if action != "heartbeat" || changed {
                        log_to_panel(&logs, "focus", action, &line);
                    }
                }
            }
        }

        if aitokens_enabled {
            let heartbeat_due =
                last_aitokens_heartbeat.elapsed() >= Duration::from_secs(AITOKENS_PUSH_INTERVAL_S);
            if heartbeat_due {
                let line = aitokens.build_line();
                let changed = aitokens_line_changed(aitokens.last_pushed_line(), &line);
                if changed || heartbeat_due {
                    write_lines(&mut port, &[line.clone()])?;
                    aitokens.note_pushed(&line);
                    last_aitokens_heartbeat = Instant::now();
                    if changed {
                        log_to_panel(&logs, "aitokens", "change", &line);
                    }
                }
            }
        }

        if calendar_enabled {
            let heartbeat_due = last_calendar_heartbeat.elapsed()
                >= Duration::from_secs(CALENDAR_PUSH_INTERVAL_S);
            if heartbeat_due {
                let line = calendar.build_events_line();
                let changed = calendar_line_changed(calendar.last_pushed_line(), &line);
                if changed || heartbeat_due {
                    write_lines(&mut port, &[line.clone()])?;
                    calendar.note_pushed(&line);
                    last_calendar_heartbeat = Instant::now();
                    if changed {
                        log_to_panel(&logs, "calendar", "change", &line);
                    }
                }
            }
        }

        if todoist_enabled {
            let heartbeat_due = last_todoist_heartbeat.elapsed()
                >= Duration::from_secs(TODOIST_PUSH_INTERVAL_S);
            if heartbeat_due {
                let lines = todoist.build_lines();
                let changed = todoist_line_changed(todoist.last_pushed_line(), &lines);
                // Always re-send on heartbeat: a panel that missed a bundle (reboot, lost
                // line) has no other way back to a full list. Repeats are safe — the panel
                // stages per bundle and only commits on TODOIST_END.
                write_lines(&mut port, &lines)?;
                todoist.note_pushed(&lines);
                last_todoist_heartbeat = Instant::now();
                if changed {
                    log_to_panel(
                        &logs,
                        "todoist",
                        "change",
                        &format!("{} linha(s)", lines.len()),
                    );
                }
            }
        }
        if let Some(hub) = &usb_link {
            hub.touch();
        }
    }
}

/// Auto-connect first Tessera port; reconnect on failure (matches legacy Python loop shape).
pub fn run_auto(
    profile_paths: ProfilePaths,
    usb_link: Option<Arc<UsbHub>>,
    logs: Logs,
) {
    logutil::info(&logs, "usb: aguardando painel no cabo…");
    loop {
        let ports = tessera_usb::list_candidate_ports();
        if ports.is_empty() {
            set_usb_link(&usb_link, false);
            std::thread::sleep(Duration::from_secs_f64(RETRY_S));
            continue;
        }
        let PortName(path) = &ports[0];
        match run_session(path, profile_paths.clone(), usb_link.clone(), logs.clone()) {
            Ok(()) => {}
            Err(e) => {
                logutil::warn(&logs, format!("usb: {path} — {e}; reconectando…"));
                set_usb_link(&usb_link, false);
                std::thread::sleep(Duration::from_secs_f64(RETRY_S * 2.0));
            }
        }
    }
}

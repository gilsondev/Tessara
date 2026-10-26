//! USB session: handshake, panel commands, capability push heartbeats.

use std::io::Read;
use std::sync::{Arc, RwLock};
use std::time::{Duration, Instant};

use tessara_caps_focus::{
    focus_line_changed, FocusCapability, FOCUS_POLL_INTERVAL_S, FOCUS_PUSH_INTERVAL_S,
};
use tessara_caps_aitokens::{
    aitokens_line_changed, AiTokensCapability, AITOKENS_PUSH_INTERVAL_S,
};
use tessara_caps_media::{
    media_line_changed, MediaCapability, MEDIA_POLL_INTERVAL_S, MEDIA_PUSH_INTERVAL_S,
};
use tessara_protocol::agent_ok_line;
use tessara_usb::{open_tessara_port, write_handshake, write_lines, PortName, UsbError};

use crate::ipc::UsbLink;

const ONCE_READ_S: u64 = 2;
const RETRY_S: f64 = 1.5;

pub fn print_candidate_ports() {
    let ports = tessara_usb::list_candidate_ports();
    if ports.is_empty() {
        eprintln!("agentd: nenhuma porta USB Tessara (usbmodem / Tessara)");
        return;
    }
    eprintln!("agentd: portas candidatas:");
    for PortName(name) in &ports {
        eprintln!("  {name}");
    }
}

pub fn run_once(path: &str) -> Result<(), UsbError> {
    eprintln!("agentd: abrindo {path}");
    let mut port = open_tessara_port(path)?;
    write_handshake(&mut port, &agent_ok_line())?;
    eprintln!("agentd: handshake → {}", agent_ok_line());

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
                        eprintln!("agentd: ← {line}");
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
            Err(e) => return Err(e.into()),
        }
    }
    eprintln!("agentd: --once concluído ({ONCE_READ_S}s leitura)");
    Ok(())
}

fn set_usb_link(link: &Option<Arc<RwLock<UsbLink>>>, up: bool) {
    if let Some(usb) = link {
        if let Ok(mut guard) = usb.write() {
            *guard = if up { UsbLink::Up } else { UsbLink::Disconnected };
        }
    }
}

/// Blocking loop on a fixed serial path (daemon background thread).
pub fn run_session(
    path: &str,
    media_enabled: bool,
    focus_enabled: bool,
    aitokens_enabled: bool,
    usb_link: Option<Arc<RwLock<UsbLink>>>,
) -> Result<(), UsbError> {
    eprintln!("agentd: USB sessão em {path}");
    let mut port = open_tessara_port(path)?;
    write_handshake(&mut port, &agent_ok_line())?;
    set_usb_link(&usb_link, true);
    eprintln!("agentd: conectado {path}");

    let mut media = MediaCapability::with_default_watcher();
    let mut focus = FocusCapability::new();
    let mut aitokens = AiTokensCapability::new();
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
    let mut last_aitokens_heartbeat = Instant::now()
        .checked_sub(Duration::from_secs(AITOKENS_PUSH_INTERVAL_S))
        .unwrap_or_else(Instant::now);

    loop {
        match port.read(&mut scratch) {
            Ok(0) => {}
            Ok(n) => {
                buf.extend_from_slice(&scratch[..n]);
                while let Some(pos) = buf.iter().position(|&b| b == b'\n') {
                    let line_bytes: Vec<u8> = buf.drain(..=pos).collect();
                    let text = String::from_utf8_lossy(&line_bytes[..line_bytes.len().saturating_sub(1)]);
                    let text = text.trim();
                    if text.is_empty() {
                        continue;
                    }
                    if media_enabled && text.starts_with("MEDIA") {
                        if text.contains('\t') && !text.ends_with('?') {
                            eprintln!("agentd: media ← {text}");
                        }
                        let replies = media.handle_line(text);
                        if !replies.is_empty() {
                            write_lines(&mut port, &replies)?;
                            for reply in &replies {
                                if reply.starts_with("MEDIA\t") {
                                    media.note_pushed(reply);
                                }
                            }
                            last_media_heartbeat = Instant::now();
                            last_media_poll = Instant::now();
                        }
                    } else if focus_enabled && text.starts_with("FOCUS") {
                        if text.contains('\t') && !text.ends_with('?') {
                            eprintln!("agentd: focus ← {text}");
                        }
                        let replies = focus.handle_line(text);
                        if !replies.is_empty() {
                            write_lines(&mut port, &replies)?;
                            for reply in &replies {
                                if reply.starts_with("FOCUS\t") {
                                    focus.note_pushed(reply);
                                }
                            }
                            last_focus_heartbeat = Instant::now();
                            last_focus_poll = Instant::now();
                        }
                    } else if aitokens_enabled && text.starts_with("AITOKENS") {
                        let replies = aitokens.handle_line(text);
                        if !replies.is_empty() {
                            write_lines(&mut port, &replies)?;
                            for reply in &replies {
                                if reply.starts_with("AITOKENS\t") {
                                    aitokens.note_pushed(reply);
                                }
                            }
                            last_aitokens_heartbeat = Instant::now();
                        }
                    } else if !text.is_empty() {
                        eprintln!("agentd: ← {text}");
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
            Err(e) => {
                set_usb_link(&usb_link, false);
                return Err(e.into());
            }
        }

        let mut wrote_push = false;
        if media_enabled {
            let poll_due =
                last_media_poll.elapsed() >= Duration::from_secs(MEDIA_POLL_INTERVAL_S);
            let heartbeat_due =
                last_media_heartbeat.elapsed() >= Duration::from_secs(MEDIA_PUSH_INTERVAL_S);
            if poll_due || heartbeat_due {
                if poll_due {
                    last_media_poll = Instant::now();
                }
                let line = media.build_line();
                let push = heartbeat_due
                    || media_line_changed(media.last_pushed_line(), &line);
                if push {
                    write_lines(&mut port, &[line.clone()])?;
                    media.note_pushed(&line);
                    last_media_heartbeat = Instant::now();
                    wrote_push = true;
                }
            }
        }

        if focus_enabled && !wrote_push {
            let poll_due =
                last_focus_poll.elapsed() >= Duration::from_secs(FOCUS_POLL_INTERVAL_S);
            let heartbeat_due =
                last_focus_heartbeat.elapsed() >= Duration::from_secs(FOCUS_PUSH_INTERVAL_S);
            let db_dirty = poll_due && focus.poll_dirty();
            if poll_due {
                last_focus_poll = Instant::now();
            }
            if poll_due || heartbeat_due {
                let line = focus.build_line();
                let push = heartbeat_due
                    || db_dirty
                    || focus_line_changed(focus.last_pushed_line(), &line);
                if push {
                    write_lines(&mut port, &[line.clone()])?;
                    focus.note_pushed(&line);
                    last_focus_heartbeat = Instant::now();
                }
            }
        }

        if aitokens_enabled && !wrote_push {
            let heartbeat_due =
                last_aitokens_heartbeat.elapsed() >= Duration::from_secs(AITOKENS_PUSH_INTERVAL_S);
            if heartbeat_due {
                let line = aitokens.build_line();
                if aitokens_line_changed(aitokens.last_pushed_line(), &line) || heartbeat_due {
                    write_lines(&mut port, &[line.clone()])?;
                    aitokens.note_pushed(&line);
                    last_aitokens_heartbeat = Instant::now();
                }
            }
        }
    }
}

/// Auto-connect first Tessara port; reconnect on failure (matches legacy Python loop shape).
pub fn run_auto(
    media_enabled: bool,
    focus_enabled: bool,
    aitokens_enabled: bool,
    usb_link: Option<Arc<RwLock<UsbLink>>>,
) {
    eprintln!("agentd: aguardando painel USB…");
    loop {
        let ports = tessara_usb::list_candidate_ports();
        if ports.is_empty() {
            set_usb_link(&usb_link, false);
            std::thread::sleep(Duration::from_secs_f64(RETRY_S));
            continue;
        }
        let PortName(path) = &ports[0];
        match run_session(
            path,
            media_enabled,
            focus_enabled,
            aitokens_enabled,
            usb_link.clone(),
        ) {
            Ok(()) => {}
            Err(e) => {
                eprintln!("agentd: USB {path}: {e}");
                set_usb_link(&usb_link, false);
                std::thread::sleep(Duration::from_secs_f64(RETRY_S * 2.0));
            }
        }
    }
}

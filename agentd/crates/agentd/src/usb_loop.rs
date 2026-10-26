//! USB session: handshake, panel commands, capability push heartbeats.

use std::io::Read;
use std::sync::{Arc, RwLock};
use std::time::{Duration, Instant};

use tessara_caps_media::{MediaCapability, MEDIA_PUSH_INTERVAL_S};
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
    usb_link: Option<Arc<RwLock<UsbLink>>>,
) -> Result<(), UsbError> {
    eprintln!("agentd: USB sessão em {path}");
    let mut port = open_tessara_port(path)?;
    write_handshake(&mut port, &agent_ok_line())?;
    set_usb_link(&usb_link, true);
    eprintln!("agentd: conectado {path}");

    let mut media = MediaCapability::with_default_watcher();
    let mut buf = Vec::<u8>::new();
    let mut scratch = [0u8; 256];
    let mut last_media = Instant::now()
        .checked_sub(Duration::from_secs(MEDIA_PUSH_INTERVAL_S))
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
                            last_media = Instant::now();
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

        if media_enabled && last_media.elapsed() >= Duration::from_secs(MEDIA_PUSH_INTERVAL_S) {
            write_lines(&mut port, &[media.build_line()])?;
            last_media = Instant::now();
        }
    }
}

/// Auto-connect first Tessara port; reconnect on failure (matches legacy Python loop shape).
pub fn run_auto(media_enabled: bool, usb_link: Option<Arc<RwLock<UsbLink>>>) {
    eprintln!("agentd: aguardando painel USB…");
    loop {
        let ports = tessara_usb::list_candidate_ports();
        if ports.is_empty() {
            set_usb_link(&usb_link, false);
            std::thread::sleep(Duration::from_secs_f64(RETRY_S));
            continue;
        }
        let PortName(path) = &ports[0];
        match run_session(path, media_enabled, usb_link.clone()) {
            Ok(()) => {}
            Err(e) => {
                eprintln!("agentd: USB {path}: {e}");
                set_usb_link(&usb_link, false);
                std::thread::sleep(Duration::from_secs_f64(RETRY_S * 2.0));
            }
        }
    }
}

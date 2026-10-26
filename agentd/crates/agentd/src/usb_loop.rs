//! One-shot USB smoke: open, DTR, handshake, brief read.

use std::io::Read;
use std::time::{Duration, Instant};

use tessara_protocol::agent_ok_line;
use tessara_usb::{open_tessara_port, write_handshake, PortName, UsbError};

const ONCE_READ_S: u64 = 2;

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

//! Serial CDC helpers (DTR on open — required by ESP32 CDC).

use std::io::{self, Write};
use std::time::Duration;

pub const BAUD: u32 = 115_200;
pub const READ_TIMEOUT_MS: u64 = 200;

#[derive(Debug, thiserror::Error)]
pub enum UsbError {
    #[error("serial port: {0}")]
    Serial(#[from] serialport::Error),
    #[error("io: {0}")]
    Io(#[from] io::Error),
}

pub struct PortName(pub String);

/// Tessera / ESP32 CDC candidates (matches `agent.runtime.app_ports` naming).
pub fn list_candidate_ports() -> Vec<PortName> {
    let mut names: Vec<String> = serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .map(|info| info.port_name)
        .filter(|name| is_tessera_candidate(name))
        .collect();
    names.sort();
    names.dedup();
    names.into_iter().map(PortName).collect()
}

fn is_tessera_candidate(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    if name.ends_with("1401") {
        return false;
    }
    lower.contains("usbmodem") || lower.contains("tessera")
}

/// Open serial at 115200 with read timeout; assert DTR after settle (match Python `open_serial`).
pub fn open_tessera_port(path: &str) -> Result<Box<dyn serialport::SerialPort>, UsbError> {
    let mut port = serialport::new(path, BAUD)
        .timeout(Duration::from_millis(READ_TIMEOUT_MS))
        .open()
        .map_err(UsbError::from)?;
    // dsrdtr=False on Python open avoids the macOS CDC reset pulse; we do not toggle DTR
    // until after settle below.
    let _ = port.write_request_to_send(false);
    std::thread::sleep(Duration::from_millis(500)); // settle after open (CDC enumerate)
    // ESP32 USB CDC suppresses device→host TX while DTR is low, so panel commands
    // (MEDIA\tPLAYPAUSE, CALENDAR?, …) never arrive. Assert DTR after settle — open
    // already happened without the reset pulse policy from Python.
    let _ = port.write_data_terminal_ready(true);
    Ok(port)
}

/// Write AGENT handshake line + newline (transport); callers pass protocol body without `\n`.
pub fn write_handshake(w: &mut impl Write, line: &str) -> io::Result<()> {
    write_line(w, line)
}

/// One protocol line + newline; brief pause so the panel can parse before the next line.
pub fn write_line(w: &mut impl Write, line: &str) -> io::Result<()> {
    let body = line.trim_end_matches('\n');
    if body.is_empty() {
        return Ok(());
    }
    w.write_all(body.as_bytes())?;
    w.write_all(b"\n")?;
    w.flush()?;
    std::thread::sleep(Duration::from_millis(20));
    Ok(())
}

pub fn write_lines(w: &mut impl Write, lines: &[String]) -> io::Result<()> {
    for line in lines {
        write_line(w, line)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tessera_protocol::agent_ok_line;

    #[test]
    fn write_handshake_vec_u8() {
        let mut buf = Vec::new();
        write_handshake(&mut buf, &agent_ok_line()).unwrap();
        assert_eq!(buf, b"AGENT\tv1\t1\n");
    }
}

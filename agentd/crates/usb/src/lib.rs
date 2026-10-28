//! Serial CDC helpers (DTR on open — required by ESP32 CDC).

use std::io::{self, Write};
use std::time::Duration;

pub const BAUD: u32 = 115_200;
pub const READ_TIMEOUT_MS: u64 = 200;
/// Espressif USB CDC on the first supported board (`boards/jc3248w535.json`).
pub const PANEL_VID: u16 = 0x303A;
pub const PANEL_PID: u16 = 0x1001;

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
    let mut ranked: Vec<(u8, String)> = serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .filter_map(|info| {
            let (vid, pid) = match info.port_type {
                serialport::SerialPortType::UsbPort(usb) => (Some(usb.vid), Some(usb.pid)),
                _ => (None, None),
            };
            let rank = port_rank(&info.port_name, vid, pid);
            (rank > 0).then_some((rank, info.port_name))
        })
        .collect();
    ranked.sort_by(|a, b| b.0.cmp(&a.0).then_with(|| a.1.cmp(&b.1)));
    ranked.dedup_by(|a, b| a.1 == b.1);
    ranked.into_iter().map(|(_, name)| PortName(name)).collect()
}

/// Higher is a better match. `0` is not a panel port.
/// The bootloader (`…1401`) is never a session port. A known VID/PID wins over a
/// generic `usbmodem` name so another dongle is not taken first.
pub fn port_rank(name: &str, vid: Option<u16>, pid: Option<u16>) -> u8 {
    if name.ends_with("1401") {
        return 0;
    }
    if vid == Some(PANEL_VID) && pid == Some(PANEL_PID) {
        return 2;
    }
    let lower = name.to_ascii_lowercase();
    if lower.contains("usbmodem") || lower.contains("tessera") {
        1
    } else {
        0
    }
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

/// Force an ESP32 USB-CDC reset. Only for an explicit "restart the panel" action.
/// Normal opens must keep using `open_tessera_port`, which avoids this pulse.
pub fn pulse_dtr(path: &str) -> Result<(), UsbError> {
    let mut port = serialport::new(path, BAUD)
        .timeout(Duration::from_millis(READ_TIMEOUT_MS))
        .open()
        .map_err(UsbError::from)?;
    let _ = port.write_data_terminal_ready(true);
    std::thread::sleep(Duration::from_millis(100));
    let _ = port.write_data_terminal_ready(false);
    std::thread::sleep(Duration::from_millis(150));
    let _ = port.write_data_terminal_ready(true);
    Ok(())
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
    fn panel_vid_pid_outranks_a_generic_modem() {
        assert_eq!(
            port_rank("/dev/cu.usbmodem288485548A341", Some(PANEL_VID), Some(PANEL_PID)),
            2
        );
        assert_eq!(port_rank("/dev/cu.usbmodemOTHER", None, None), 1);
        assert_eq!(port_rank("/dev/cu.usbmodem1401", Some(PANEL_VID), Some(PANEL_PID)), 0);
        assert_eq!(port_rank("/dev/cu.Bluetooth-Incoming-Port", None, None), 0);
    }

    #[test]
    fn write_handshake_vec_u8() {
        let mut buf = Vec::new();
        write_handshake(&mut buf, &agent_ok_line()).unwrap();
        assert_eq!(buf, b"AGENT\tv1\t1\n");
    }
}

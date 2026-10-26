//! Serial CDC helpers (DTR on open — required by ESP32 CDC).

pub struct PortName(pub String);

pub fn list_candidate_ports() -> Vec<PortName> {
    // Implement with serialport::available_ports in Task 3
    Vec::new()
}

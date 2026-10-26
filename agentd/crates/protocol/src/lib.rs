//! USB line codec — Tessara agent ↔ panel.

pub fn agent_ok_line(version: &str) -> String {
    // Match legacy spirit: AGENT\tv1\t1 (ok) — keep fields documented in tests
    format!("AGENT\tv1\t1\t{version}")
}

pub fn parse_prefix(line: &str) -> &str {
    line.split('\t').next().unwrap_or("")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agent_line_has_tabs() {
        let s = agent_ok_line("0.1.0");
        assert!(s.starts_with("AGENT\tv1\t1\t"));
        assert_eq!(parse_prefix(&s), "AGENT");
    }
}

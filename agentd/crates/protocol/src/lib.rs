//! USB line codec — Tessara agent ↔ panel.
//!
//! Field layout matches `agent/src/agent/protocol.py` and capability encoders.

/// Legacy OK handshake (`agent_hello(ok=True)`). No trailing `\n`; callers add at transport.
pub fn agent_ok_line() -> String {
    "AGENT\tv1\t1".to_string()
}

/// Legacy fail handshake (`agent_hello(ok=False)`).
pub fn agent_fail_line(detail: &str) -> String {
    let reason = detail
        .chars()
        .map(|c| if c == '\t' { ' ' } else { c })
        .collect::<String>();
    let reason = reason.chars().take(40).collect::<String>();
    let reason = if reason.is_empty() { "fail".to_string() } else { reason };
    format!("AGENT\tv1\t0\t{reason}")
}

pub fn parse_prefix(line: &str) -> &str {
    line.split('\t').next().unwrap_or("")
}

fn sanitize_field(text: &str, limit: usize) -> String {
    let cleaned: String = text
        .chars()
        .map(|c| match c {
            '\t' | '\n' | '\r' => ' ',
            c => c,
        })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ");
    if cleaned.chars().count() > limit {
        let truncated: String = cleaned.chars().take(limit.saturating_sub(1)).collect();
        format!("{truncated}…")
    } else {
        cleaned
    }
}

/// Matches `agent.capabilities.media.encode_line`.
pub fn media_line(
    ok: bool,
    state: &str,
    title: &str,
    artist: &str,
    app: &str,
) -> String {
    if !ok {
        return "MEDIA\tv1\t0\t\t\t\t".to_string();
    }
    let st = match state {
        "playing" | "paused" | "stopped" => state,
        _ => "stopped",
    };
    format!(
        "MEDIA\tv1\t1\t{st}\t{}\t{}\t{}",
        sanitize_field(title, 40),
        sanitize_field(artist, 40),
        sanitize_field(app, 24),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agent_line_has_tabs() {
        let s = agent_ok_line();
        assert_eq!(s, "AGENT\tv1\t1");
        assert_eq!(parse_prefix(&s), "AGENT");
    }

    #[test]
    fn media_encode_matches_python_shape() {
        let s = media_line(true, "playing", "Test Song", "Test Artist", "Music");
        assert!(s.starts_with("MEDIA\tv1\t1\tplaying\t"));
    }
}

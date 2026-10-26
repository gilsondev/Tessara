//! Golden USB lines captured from the legacy Python agent encoders.

use tessara_protocol::{agent_ok_line, media_line, parse_prefix};

#[test]
fn golden_agent_prefix() {
    let raw = include_str!("fixtures/lines/agent_ok.txt").trim_end_matches('\n');
    assert_eq!(parse_prefix(raw), "AGENT");
}

#[test]
fn golden_agent_line_matches_python() {
    let raw = include_str!("fixtures/lines/agent_ok.txt").trim_end_matches('\n');
    assert_eq!(agent_ok_line(), raw);
}

#[test]
fn golden_media_prefix() {
    let raw = include_str!("fixtures/lines/media_playing.txt").trim_end_matches('\n');
    assert_eq!(parse_prefix(raw), "MEDIA");
}

#[test]
fn golden_media_line_matches_python() {
    let raw = include_str!("fixtures/lines/media_playing.txt").trim_end_matches('\n');
    let encoded = media_line(true, "playing", "Test Song", "Test Artist", "Music");
    assert_eq!(encoded, raw);
}

#[test]
fn golden_media_not_ok() {
    let raw = include_str!("fixtures/lines/media_not_ok.txt").trim_end_matches('\n');
    assert_eq!(media_line(false, "stopped", "", "", ""), raw);
}

#[test]
fn golden_media_stopped() {
    let raw = include_str!("fixtures/lines/media_stopped.txt").trim_end_matches('\n');
    assert_eq!(media_line(true, "stopped", "", "", ""), raw);
}

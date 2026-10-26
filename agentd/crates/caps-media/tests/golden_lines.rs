//! Golden MEDIA lines from legacy Python `encode_line`.

use tessara_caps_media::encode_media_line;

#[test]
fn golden_media_not_ok() {
    let raw = include_str!("fixtures/lines/media_not_ok.txt").trim_end_matches('\n');
    assert_eq!(encode_media_line(false, "stopped", "", "", ""), raw);
}

#[test]
fn golden_media_stopped() {
    let raw = include_str!("fixtures/lines/media_stopped.txt").trim_end_matches('\n');
    assert_eq!(encode_media_line(true, "stopped", "", "", ""), raw);
}

#[test]
fn golden_media_playing() {
    let raw = include_str!("fixtures/lines/media_playing.txt").trim_end_matches('\n');
    assert_eq!(
        encode_media_line(true, "playing", "Test Song", "Test Artist", "Music"),
        raw
    );
}

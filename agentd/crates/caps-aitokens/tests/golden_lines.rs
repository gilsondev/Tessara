use tessera_caps_aitokens::{encode_aitokens_line, AiTokenSource, SourceKind};

#[test]
fn golden_mixed_sources_matches_python_wire_format() {
    let sources = [
        AiTokenSource::available("codex", SourceKind::Pct, 72, "72% · 91%", "14:30"),
        AiTokenSource::unavailable("claude"),
        AiTokenSource::available("cursor", SourceKind::Pct, 87, "10% · 13%", "28 out"),
    ];
    let raw = include_str!("fixtures/lines/aitokens_mixed.txt").trim_end_matches('\n');
    assert_eq!(encode_aitokens_line(&sources), raw);
}

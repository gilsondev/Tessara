//! Golden FOCUS lines from legacy Python `encode_line`.

use tessara_caps_focus::{encode_focus_line, FocusMode};

#[test]
fn golden_focus_not_ok() {
    let raw = include_str!("fixtures/lines/focus_not_ok.txt").trim_end_matches('\n');
    assert_eq!(encode_focus_line(false, "", "", &[]), raw);
}

#[test]
fn golden_focus_active_list() {
    let raw = include_str!("fixtures/lines/focus_active_list.txt").trim_end_matches('\n');
    let modes = vec![
        FocusMode {
            id: "trabalho".into(),
            name: "Trabalho".into(),
        },
        FocusMode {
            id: "pessoal".into(),
            name: "Pessoal".into(),
        },
    ];
    assert_eq!(encode_focus_line(true, "trabalho", "Trabalho", &modes), raw);
}

#[test]
fn golden_focus_ok_empty_active() {
    let raw = include_str!("fixtures/lines/focus_ok_empty_active.txt").trim_end_matches('\n');
    assert_eq!(encode_focus_line(true, "", "", &[]), raw);
}

use tessara_caps_calendar::{encode_calendar_line, CalendarEvent};

#[test]
fn main_event_line_matches_golden() {
    let actual = encode_calendar_line(
        true,
        &[CalendarEvent {
            start_hhmm: "0930".into(),
            end_hhmm: "1015".into(),
            all_day: false,
            title: "Daily\tstandup".into(),
            location: "Sala\nAzul".into(),
            duration_min: 45,
            color: "#1a73e8".into(),
            day_offset: 0,
        }],
    );
    let expected = include_str!("fixtures/lines/calendar_event.txt").trim_end();
    assert_eq!(actual, expected);
}

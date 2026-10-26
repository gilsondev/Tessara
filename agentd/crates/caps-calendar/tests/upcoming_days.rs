use tessera_caps_calendar::{cache_age_in_days, encode_calendar_line, CalendarEvent};

fn event(title: &str, start: &str, day_offset: u8) -> CalendarEvent {
    CalendarEvent {
        start_hhmm: start.into(),
        end_hhmm: "1000".into(),
        all_day: false,
        title: title.into(),
        location: String::new(),
        duration_min: 45,
        color: "1A73E8".into(),
        day_offset,
    }
}

/// Evening case: nothing left today, so tomorrow must reach the panel instead of an empty line.
#[test]
fn encodes_events_past_today() {
    let line = encode_calendar_line(true, &[event("Devocional", "0915", 1)]);
    let fields: Vec<&str> = line.split('\t').collect();
    assert_eq!(&fields[..4], &["CALENDAR", "v1", "1", "1"]);
    assert_eq!(fields[4], "0915");
    assert_eq!(fields[7], "Devocional");
    assert_eq!(fields[11], "1", "day_offset must survive the wire");
}

#[test]
fn keeps_both_days_in_one_line() {
    let line = encode_calendar_line(
        true,
        &[event("Hoje", "1700", 0), event("Amanhã", "0915", 1)],
    );
    assert!(line.contains("\tHoje\t"));
    assert!(line.contains("\tAmanhã\t"));
}

#[test]
fn cache_written_today_is_not_aged() {
    let now = chrono::Local::now().to_rfc3339();
    assert_eq!(cache_age_in_days(&now), Some(0));
}

#[test]
fn cache_from_yesterday_ages_one_day() {
    let yesterday = (chrono::Local::now() - chrono::Duration::days(1)).to_rfc3339();
    assert_eq!(cache_age_in_days(&yesterday), Some(1));
}

#[test]
fn cache_with_garbage_timestamp_is_ignored() {
    assert_eq!(cache_age_in_days("ontem"), None);
}

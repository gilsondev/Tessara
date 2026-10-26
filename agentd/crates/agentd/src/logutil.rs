//! Shared logging into stderr (LaunchAgent log) + Activity UI ring buffer.

use std::time::{SystemTime, UNIX_EPOCH};

use tessera_logbuf::SharedLogBuf;

/// UTC clock for log lines (`HH:MM:SS`).
pub fn stamp() -> String {
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    let s = secs % 86_400;
    format!(
        "{:02}:{:02}:{:02}",
        s / 3600,
        (s % 3600) / 60,
        s % 60
    )
}

pub fn append(logs: &Option<SharedLogBuf>, line: impl AsRef<str>) {
    let line = line.as_ref();
    eprintln!("{line}");
    if let Some(buf) = logs {
        buf.append(line.to_string());
    }
}

pub fn info(logs: &Option<SharedLogBuf>, msg: impl AsRef<str>) {
    append(logs, format!("{} INFO  {}", stamp(), msg.as_ref()));
}

pub fn warn(logs: &Option<SharedLogBuf>, msg: impl AsRef<str>) {
    append(logs, format!("{} WARN  {}", stamp(), msg.as_ref()));
}

/// Shorten USB / portal payloads for the Activity log.
pub fn clip(s: &str, max: usize) -> String {
    let t = s.trim();
    if t.len() <= max {
        return t.to_string();
    }
    format!("{}…(+{}B)", &t[..max], t.len() - max)
}

/// Classify a USB protocol line into a widget id for monitoring.
pub fn widget_of_line(line: &str) -> &'static str {
    let head = line.split(['\t', '?', ' ']).next().unwrap_or("");
    match head {
        "MEDIA" | "COVER" => "media",
        "FOCUS" => "focus",
        "AITOKENS" => "aitokens",
        "CALENDAR" => "calendar",
        "TODOIST" | "TODOIST_CLOSE" | "TODOIST_SEL" | "TODOIST_TOKEN" | "TODOIST_TASKS"
        | "TODOIST_END" => "todoist",
        "THEME" => "theme",
        "AGENT" => "agent",
        _ => "other",
    }
}

fn hhmm(raw: &str) -> String {
    let d: String = raw.chars().filter(|c| c.is_ascii_digit()).collect();
    if d.len() >= 4 {
        format!("{}:{}", &d[..2], &d[2..4])
    } else if raw.is_empty() {
        "—".into()
    } else {
        raw.to_string()
    }
}

/// Human-readable summary of a USB protocol line for investigation.
pub fn describe_usb(line: &str) -> String {
    let line = line.trim();
    if line.is_empty() {
        return String::new();
    }
    if line.ends_with('?') {
        return format!("{} (pedido do painel)", line.trim_end_matches('?'));
    }
    let parts: Vec<&str> = line.split('\t').collect();
    let head = parts.first().copied().unwrap_or("");
    match head {
        "AGENT" => "handshake ok".into(),
        "THEME" => {
            let mode = parts.get(3).unwrap_or(&"?");
            format!("aparência={mode}")
        }
        "MEDIA" => {
            // MEDIA v1 linked state title artist app …
            let state = parts.get(3).unwrap_or(&"—");
            let title = parts.get(4).unwrap_or(&"");
            let artist = parts.get(5).unwrap_or(&"");
            let app = parts.get(6).unwrap_or(&"");
            if title.is_empty() && artist.is_empty() {
                format!("estado={state} app={app}")
            } else {
                format!("estado={state} “{title}” — {artist} ({app})")
            }
        }
        "COVER" => format!("capa arte ({} campos)", parts.len().saturating_sub(1)),
        "FOCUS" => {
            let on = parts.get(2).unwrap_or(&"0");
            let name = parts.get(3).unwrap_or(&"");
            if *on == "1" || *on == "true" {
                format!("Focus ligado · {}", if name.is_empty() { "modo" } else { *name })
            } else {
                "Focus desligado".into()
            }
        }
        "AITOKENS" => {
            let mut bits = Vec::new();
            let mut i = 3usize;
            while i + 4 < parts.len() {
                let id = parts[i];
                let linked = parts.get(i + 1).copied().unwrap_or("0");
                let label = parts.get(i + 4).copied().unwrap_or("—");
                if linked == "1" {
                    bits.push(format!("{id}={label}"));
                } else {
                    bits.push(format!("{id}=off"));
                }
                i += 5;
            }
            if bits.is_empty() {
                "cotas".into()
            } else {
                bits.join(" · ")
            }
        }
        "CALENDAR" => {
            // CALENDAR v1 1 linked [start end allday title loc dur color day]*
            let linked = parts.get(3).unwrap_or(&"0");
            if *linked != "1" {
                return "agenda desligada / sem conta".into();
            }
            let mut events = Vec::new();
            let mut i = 4usize;
            while i + 7 < parts.len() && events.len() < 3 {
                let start = hhmm(parts[i]);
                let all_day = parts.get(i + 2).copied().unwrap_or("0") == "1";
                let title = parts.get(i + 3).copied().unwrap_or("");
                if !title.is_empty() {
                    if all_day {
                        events.push(format!("Dia todo · {title}"));
                    } else {
                        events.push(format!("{start} · {title}"));
                    }
                }
                i += 8;
            }
            if events.is_empty() {
                "ligada · sem eventos próximos".into()
            } else {
                format!("{} evento(s): {}", events.len(), events.join(" | "))
            }
        }
        "TODOIST" => {
            let linked = parts.get(3).unwrap_or(&"0");
            if *linked != "1" {
                return "todoist sem token no Mac".into();
            }
            let views = parts.get(4).and_then(|s| s.parse::<u32>().ok()).unwrap_or(0);
            format!("ligado · {views} lista(s)")
        }
        "TODOIST_TASKS" => {
            let index = parts.get(2).unwrap_or(&"?");
            let n = parts.get(3).unwrap_or(&"0");
            format!("lista[{index}] · {n} tarefa(s)")
        }
        "TODOIST_END" => {
            let ok = parts.get(2).unwrap_or(&"0");
            let linked = parts.get(3).unwrap_or(&"0");
            format!("fim ok={ok} linked={linked}")
        }
        "TODOIST_TOKEN" | "TOKEN" => "token (recebido)".into(),
        "TODOIST_SEL" => "seleção de listas".into(),
        "TODOIST_CLOSE" => {
            // Mac→panel ack is `v1\tok\tid`; panel→Mac request is just `id`.
            if parts.get(1).copied() != Some("v1") {
                let id = parts.get(1).unwrap_or(&"");
                return format!("fechar {id}");
            }
            let ok = parts.get(2).unwrap_or(&"0");
            let id = parts.get(3).unwrap_or(&"");
            if *ok == "1" {
                format!("fechou {id}")
            } else {
                format!("close falhou {id}")
            }
        }
        _ => clip(line, 160),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn todoist_token_never_echoed() {
        let secret = "TODOIST_TOKEN\te1df1c93683a8b19d907fe1c6c090d801819eeaa";
        let out = describe_usb(secret);
        assert_eq!(out, "token (recebido)");
        assert!(!out.contains("e1df"));
        assert!(!describe_usb("TOKEN\talso-secret").contains("also-secret"));
    }

    #[test]
    fn todoist_header_and_tasks_summaries() {
        assert!(describe_usb("TODOIST\tv1\t1\t1\t7\tHoje\ttoday").contains("7 lista"));
        assert_eq!(
            describe_usb("TODOIST_TASKS\tv1\t3\t16\tid\t1\ttitle\t"),
            "lista[3] · 16 tarefa(s)"
        );
        assert!(describe_usb("TODOIST_END\tv1\t1\t1\t7").contains("linked=1"));
    }
}

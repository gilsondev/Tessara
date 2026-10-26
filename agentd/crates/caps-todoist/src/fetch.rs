//! Todoist REST on the Mac — token + filter sync + task filter + close.

use std::fs;

use serde_json::Value;

use crate::{CachedTask, CachedView, TodoistPaths, MAX_TASKS, MAX_VIEWS};

const API: &str = "https://api.todoist.com/api/v1";

fn pct_encode(value: &str) -> String {
    let mut out = String::with_capacity(value.len() * 3);
    for b in value.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char);
            }
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

fn wire_clean(value: &str, max: usize) -> String {
    value
        .chars()
        .map(|c| match c {
            '\t' | '\r' | '\n' => ' ',
            c => c,
        })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
        .chars()
        .take(max)
        .collect()
}

fn auth_get(token: &str, path_and_query: &str) -> Result<Value, String> {
    let url = format!("{API}{path_and_query}");
    let resp = ureq::get(&url)
        .set("Authorization", &format!("Bearer {token}"))
        .timeout(std::time::Duration::from_secs(25))
        .call()
        .map_err(|e| format!("GET {path_and_query}: {e}"))?;
    resp.into_json()
        .map_err(|e| format!("json {path_and_query}: {e}"))
}

fn auth_post_form(token: &str, path: &str, form: &str) -> Result<Value, String> {
    let url = format!("{API}{path}");
    let resp = ureq::post(&url)
        .set("Authorization", &format!("Bearer {token}"))
        .set("Content-Type", "application/x-www-form-urlencoded")
        .timeout(std::time::Duration::from_secs(25))
        .send_string(form)
        .map_err(|e| format!("POST {path}: {e}"))?;
    let status = resp.status();
    if status == 204 {
        return Ok(Value::Null);
    }
    match resp.into_json::<Value>() {
        Ok(v) => Ok(v),
        Err(_) => Ok(Value::Null),
    }
}

fn fallback_views() -> Vec<CachedView> {
    const ROWS: &[(&str, &str)] = &[
        ("Hoje", "today | overdue"),
        (
            "Casa",
            "@Casa & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho",
        ),
        (
            "Computador",
            "@Computador & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho",
        ),
        (
            "Trabalho",
            "@Trabalho & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho",
        ),
        (
            "Rua",
            "@Rua & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho",
        ),
        (
            "Celular",
            "@Celular & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho",
        ),
        ("Aguardando", "@Delegado"),
    ];
    ROWS.iter()
        .take(MAX_VIEWS)
        .map(|(name, query)| CachedView {
            name: (*name).to_string(),
            query: (*query).to_string(),
            tasks: Vec::new(),
        })
        .collect()
}

fn load_views(token: &str) -> Vec<CachedView> {
    let Ok(doc) = auth_post_form(
        token,
        "/sync",
        "sync_token=*&resource_types=%5B%22filters%22%5D",
    ) else {
        return fallback_views();
    };
    let Some(filters) = doc.get("filters").and_then(|v| v.as_array()) else {
        return fallback_views();
    };
    let mut fav: Vec<(i64, String, String)> = Vec::new();
    for item in filters {
        if item.get("is_deleted").and_then(|v| v.as_bool()).unwrap_or(false) {
            continue;
        }
        if !item
            .get("is_favorite")
            .and_then(|v| v.as_bool())
            .unwrap_or(false)
        {
            continue;
        }
        let name = item
            .get("name")
            .and_then(|v| v.as_str())
            .unwrap_or("Filtro");
        let query = item.get("query").and_then(|v| v.as_str()).unwrap_or("");
        if query.is_empty() {
            continue;
        }
        let order = item.get("item_order").and_then(|v| v.as_i64()).unwrap_or(0);
        fav.push((order, wire_clean(name, 27), query.to_string()));
    }
    fav.sort_by_key(|(o, _, _)| *o);
    let mut views = vec![CachedView {
        name: "Hoje".into(),
        query: "today | overdue".into(),
        tasks: Vec::new(),
    }];
    for (_, name, query) in fav {
        if views.len() >= MAX_VIEWS {
            break;
        }
        views.push(CachedView {
            name,
            query: wire_clean(&query, 219),
            tasks: Vec::new(),
        });
    }
    if views.len() == 1 {
        return fallback_views();
    }
    views
}

fn meta_from_task(item: &Value) -> String {
    if let Some(due) = item.get("due") {
        if let Some(s) = due.get("string").and_then(|v| v.as_str()) {
            if !s.is_empty() {
                return wire_clean(s, 71);
            }
        }
        let when = due
            .get("datetime")
            .and_then(|v| v.as_str())
            .or_else(|| due.get("date").and_then(|v| v.as_str()))
            .unwrap_or("");
        if !when.is_empty() {
            let recurring = due
                .get("is_recurring")
                .and_then(|v| v.as_bool())
                .unwrap_or(false);
            let label = if recurring {
                format!("repete · {when}")
            } else {
                when.to_string()
            };
            return wire_clean(&label, 71);
        }
    }
    if let Some(date) = item
        .get("deadline")
        .and_then(|d| d.get("date"))
        .and_then(|v| v.as_str())
    {
        return wire_clean(&format!("prazo {date}"), 71);
    }
    String::new()
}

fn load_tasks(token: &str, query: &str) -> Result<Vec<CachedTask>, String> {
    let mut out = Vec::new();
    let mut cursor = String::new();
    loop {
        let mut path = format!(
            "/tasks/filter?limit=40&query={}",
            pct_encode(query)
        );
        if !cursor.is_empty() {
            path.push_str(&format!("&cursor={}", pct_encode(&cursor)));
        }
        let doc = auth_get(token, &path)?;
        cursor.clear();
        if let Some(next) = doc.get("next_cursor").and_then(|v| v.as_str()) {
            if !next.is_empty() {
                cursor = next.to_string();
            }
        }
        let Some(results) = doc.get("results").and_then(|v| v.as_array()) else {
            break;
        };
        for item in results {
            if out.len() >= MAX_TASKS {
                cursor.clear();
                break;
            }
            let id = item
                .get("id")
                .and_then(|v| {
                    v.as_str()
                        .map(|s| s.to_string())
                        .or_else(|| v.as_u64().map(|n| n.to_string()))
                        .or_else(|| v.as_i64().map(|n| n.to_string()))
                })
                .unwrap_or_default();
            if id.is_empty() {
                continue;
            }
            let content = item
                .get("content")
                .and_then(|v| v.as_str())
                .unwrap_or("Tarefa");
            let priority = item
                .get("priority")
                .and_then(|v| v.as_u64())
                .unwrap_or(1)
                .clamp(1, 4) as u8;
            out.push(CachedTask {
                id: wire_clean(&id, 19),
                content: wire_clean(content, 179),
                meta: meta_from_task(item),
                priority,
            });
        }
        if cursor.is_empty() || out.len() >= MAX_TASKS {
            break;
        }
    }
    Ok(out)
}

/// Refresh all views into `cache.json`. Returns view count.
pub fn refresh_cache(paths: &TodoistPaths, selected: Option<&[CachedView]>) -> Result<usize, String> {
    let token = paths
        .read_token()
        .ok_or_else(|| "sem token".to_string())?;
    let mut views = if let Some(sel) = selected {
        if sel.is_empty() {
            load_views(&token)
        } else {
            sel.iter()
                .take(MAX_VIEWS)
                .map(|v| CachedView {
                    name: v.name.clone(),
                    query: v.query.clone(),
                    tasks: Vec::new(),
                })
                .collect()
        }
    } else {
        load_views(&token)
    };

    for view in &mut views {
        match load_tasks(&token, &view.query) {
            Ok(tasks) => view.tasks = tasks,
            Err(err) => {
                eprintln!("Tessera: todoist filter {:?}: {err}", view.name);
            }
        }
    }

    let payload = serde_json::json!({
        "fetched_at": format!("{:?}", std::time::SystemTime::now()),
        "views": views,
    });
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    let raw = serde_json::to_string_pretty(&payload).map_err(|e| e.to_string())?;
    fs::write(paths.cache_json(), raw).map_err(|e| e.to_string())?;
    Ok(views.len())
}

pub fn close_task(paths: &TodoistPaths, id: &str) -> Result<(), String> {
    let token = paths
        .read_token()
        .ok_or_else(|| "sem token".to_string())?;
    let id = id.trim();
    if id.is_empty() {
        return Err("id vazio".into());
    }
    let path = format!("/tasks/{}/close", pct_encode(id));
    let url = format!("{API}{path}");
    let resp = ureq::post(&url)
        .set("Authorization", &format!("Bearer {token}"))
        .set("Content-Type", "application/x-www-form-urlencoded")
        .timeout(std::time::Duration::from_secs(20))
        .send_string("")
        .map_err(|e| format!("close: {e}"))?;
    let status = resp.status();
    if (200..300).contains(&status) || status == 204 {
        return Ok(());
    }
    let body = resp.into_string().unwrap_or_default();
    if status == 404
        || body.to_ascii_lowercase().contains("already")
        || body.to_ascii_lowercase().contains("not_found")
        || body.to_ascii_lowercase().contains("not found")
    {
        return Ok(());
    }
    Err(format!("close HTTP {status}"))
}

pub fn save_token(paths: &TodoistPaths, token: &str) -> Result<(), String> {
    let token = token.trim();
    if token.is_empty() {
        return Err("token vazio".into());
    }
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    fs::write(paths.token_file(), token.as_bytes()).map_err(|e| e.to_string())?;
    // Drop world-readable bits when possible (unix).
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = fs::set_permissions(paths.token_file(), fs::Permissions::from_mode(0o600));
    }
    Ok(())
}

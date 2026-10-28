//! TODOIST capability — HTTPS on the Mac; panel gets USB snapshots only.

mod fetch;

use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};

pub const TODOIST_REFRESH_INTERVAL_S: u64 = 120;
pub const TODOIST_PUSH_INTERVAL_S: u64 = 45;
pub(crate) const MAX_VIEWS: usize = 8;
pub(crate) const MAX_TASKS: usize = 60;

/// Write `bytes` via a unique sibling temp file so a reader never sees a partial cache.
pub(crate) fn write_atomic(path: &std::path::Path, bytes: impl AsRef<[u8]>) -> std::io::Result<()> {
    tessera_proc::write_atomic(path, bytes)
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CachedTask {
    pub id: String,
    pub content: String,
    pub meta: String,
    pub priority: u8,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CachedView {
    pub name: String,
    pub query: String,
    #[serde(default)]
    pub tasks: Vec<CachedTask>,
}

#[derive(Debug, Clone)]
pub struct TodoistPaths {
    pub root: PathBuf,
}

impl TodoistPaths {
    pub fn default_home() -> Self {
        let home = std::env::var_os("HOME")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("."));
        Self {
            root: home.join(".tessera/todoist"),
        }
    }

    pub fn token_file(&self) -> PathBuf {
        self.root.join("token")
    }

    pub fn cache_json(&self) -> PathBuf {
        self.root.join("cache.json")
    }

    pub fn selection_json(&self) -> PathBuf {
        self.root.join("selection.json")
    }

    pub fn read_token(&self) -> Option<String> {
        fs::read_to_string(self.token_file())
            .ok()
            .map(|t| t.trim().to_string())
            .filter(|t| !t.is_empty())
    }
}

fn wire_field(value: &str, max_chars: usize) -> String {
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
        .take(max_chars)
        .collect()
}

/// Wire is multi-line: catalog header, TASKS line(s) per view (all tasks up to
/// `MAX_TASKS` — same ceiling as the panel Wi‑Fi path), then END.
/// Joined with `\n` only for change-detection hashing.
///
/// Panel console buffer is 4KB/line — pack until the budget; overflow continues
/// on the next line (panel appends). Goal: one line per list whenever possible.
const WIRE_LINE_BUDGET: usize = 3500;

fn encode_tasks_line(view_index: usize, tasks: &[&CachedTask]) -> String {
    let mut fields = vec![
        "TODOIST_TASKS".to_string(),
        "v1".to_string(),
        view_index.to_string(),
        tasks.len().to_string(),
    ];
    for task in tasks {
        fields.push(wire_field(&task.id, 19));
        fields.push(task.priority.clamp(1, 4).to_string());
        fields.push(wire_field(&task.content, 64));
        fields.push(wire_field(&task.meta, 28));
    }
    fields.join("\t")
}

fn encode_view_task_lines(view_index: usize, tasks: &[&CachedTask]) -> Vec<String> {
    if tasks.is_empty() {
        return vec![encode_tasks_line(view_index, &[])];
    }
    let mut lines = Vec::new();
    let mut chunk: Vec<&CachedTask> = Vec::new();
    for task in tasks {
        chunk.push(task);
        let line = encode_tasks_line(view_index, &chunk);
        if line.len() > WIRE_LINE_BUDGET && chunk.len() > 1 {
            chunk.pop();
            lines.push(encode_tasks_line(view_index, &chunk));
            chunk.clear();
            chunk.push(task);
        }
    }
    if !chunk.is_empty() {
        lines.push(encode_tasks_line(view_index, &chunk));
    }
    lines
}

pub fn encode_todoist_lines(ok: bool, linked: bool, views: &[CachedView]) -> Vec<String> {
    let mut lines = Vec::new();
    let mut header = vec![
        "TODOIST".to_string(),
        "v1".to_string(),
        if ok { "1" } else { "0" }.to_string(),
        if linked { "1" } else { "0" }.to_string(),
    ];
    if !linked {
        header.push("0".into());
        lines.push(header.join("\t"));
        lines.push(format!(
            "TODOIST_END\tv1\t{}\t0\t0",
            if ok { "1" } else { "0" }
        ));
        return lines;
    }
    let views: Vec<&CachedView> = views.iter().take(MAX_VIEWS).collect();
    header.push(views.len().to_string());
    for view in &views {
        header.push(wire_field(&view.name, 27));
        header.push(wire_field(&view.query, 219));
    }
    lines.push(header.join("\t"));
    for (i, view) in views.iter().enumerate() {
        let tasks: Vec<&CachedTask> = view.tasks.iter().take(MAX_TASKS).collect();
        lines.extend(encode_view_task_lines(i, &tasks));
    }
    lines.push(format!(
        "TODOIST_END\tv1\t{}\t1\t{}",
        if ok { "1" } else { "0" },
        views.len()
    ));
    lines
}

pub fn todoist_bundle_key(lines: &[String]) -> String {
    lines.join("\n")
}

pub fn todoist_line_changed(last_pushed: Option<&str>, lines: &[String]) -> bool {
    last_pushed != Some(todoist_bundle_key(lines).as_str())
}

pub fn encode_close_ack(ok: bool, id: &str) -> String {
    format!(
        "TODOIST_CLOSE\tv1\t{}\t{}",
        if ok { "1" } else { "0" },
        wire_field(id, 19)
    )
}

pub struct TodoistCapability {
    paths: TodoistPaths,
    selected: Vec<CachedView>,
    last_pushed: Option<String>,
    last_refresh: Instant,
    sync_minutes: u64,
    refreshing: Arc<AtomicBool>,
}

impl TodoistCapability {
    pub fn new(paths: TodoistPaths) -> Self {
        let selected = fs::read_to_string(paths.selection_json())
            .ok()
            .and_then(|raw| serde_json::from_str::<Vec<CachedView>>(&raw).ok())
            .unwrap_or_default()
            .into_iter()
            .map(|mut v| {
                v.tasks.clear();
                v
            })
            .collect();
        Self {
            paths,
            selected,
            last_pushed: None,
            last_refresh: Instant::now()
                .checked_sub(Duration::from_secs(TODOIST_REFRESH_INTERVAL_S))
                .unwrap_or_else(Instant::now),
            sync_minutes: 15,
            refreshing: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn default_home() -> Self {
        Self::new(TodoistPaths::default_home())
    }

    pub fn set_sync_minutes(&mut self, minutes: u64) {
        self.sync_minutes = minutes.clamp(5, 60);
    }

    pub fn last_pushed_line(&self) -> Option<&str> {
        self.last_pushed.as_deref()
    }

    pub fn note_pushed(&mut self, lines: &[String]) {
        self.last_pushed = Some(todoist_bundle_key(lines));
    }

    pub fn note_pushed_str(&mut self, key: &str) {
        self.last_pushed = Some(key.to_string());
    }

    pub fn linked(&self) -> bool {
        self.paths.read_token().is_some()
    }

    pub fn save_token(&self, token: &str) -> Result<(), String> {
        fetch::save_token(&self.paths, token)
    }

    fn refresh_interval(&self) -> Duration {
        Duration::from_secs(self.sync_minutes.saturating_mul(60).max(60))
    }

    fn load_cached_views(&self) -> Vec<CachedView> {
        let Ok(raw) = fs::read_to_string(self.paths.cache_json()) else {
            return Vec::new();
        };
        let Ok(value) = serde_json::from_str::<serde_json::Value>(&raw) else {
            return Vec::new();
        };
        let Some(items) = value.get("views").and_then(|v| v.as_array()) else {
            return Vec::new();
        };
        items
            .iter()
            .filter_map(|item| serde_json::from_value::<CachedView>(item.clone()).ok())
            .take(MAX_VIEWS)
            .collect()
    }

    pub fn refresh_cache_if_due(&mut self) {
        if self.last_refresh.elapsed() < self.refresh_interval() {
            return;
        }
        if self.refreshing.load(Ordering::SeqCst) {
            return;
        }
        self.last_refresh = Instant::now();
        if !self.linked() {
            return;
        }
        let paths = self.paths.clone();
        let selected = self.selected.clone();
        let flag = Arc::clone(&self.refreshing);
        flag.store(true, Ordering::SeqCst);
        thread::spawn(move || {
            struct Done(Arc<AtomicBool>);
            impl Drop for Done {
                fn drop(&mut self) {
                    self.0.store(false, Ordering::SeqCst);
                }
            }
            let _done = Done(flag);
            let sel = if selected.is_empty() {
                None
            } else {
                Some(selected)
            };
            match fetch::refresh_cache(&paths, sel.as_deref()) {
                Ok(n) => eprintln!("Tessera: todoist refresh ok ({n} listas)"),
                Err(err) => eprintln!("Tessera: todoist refresh: {err}"),
            }
        });
    }

    pub fn force_refresh(&mut self) {
        self.last_refresh = Instant::now()
            .checked_sub(self.refresh_interval())
            .unwrap_or_else(Instant::now);
        self.refresh_cache_if_due();
    }

    pub fn build_lines(&mut self) -> Vec<String> {
        self.refresh_cache_if_due();
        let linked = self.linked();
        let views = if linked {
            self.load_cached_views()
        } else {
            Vec::new()
        };
        encode_todoist_lines(true, linked, &views)
    }

    /// Legacy single-string helper for tests / logging.
    pub fn build_line(&mut self) -> String {
        self.build_lines().join("\n")
    }

    fn persist_selection(&self) {
        let slim: Vec<CachedView> = self
            .selected
            .iter()
            .map(|v| CachedView {
                name: v.name.clone(),
                query: v.query.clone(),
                tasks: Vec::new(),
            })
            .collect();
        if let Ok(raw) = serde_json::to_string_pretty(&slim) {
            let _ = fs::create_dir_all(&self.paths.root);
            let _ = fs::write(self.paths.selection_json(), raw);
        }
    }

    /// `TODOIST_SEL\tname\tquery\tname\tquery…`
    pub fn apply_selection_line(&mut self, raw: &str) {
        let Some(rest) = raw.strip_prefix("TODOIST_SEL") else {
            return;
        };
        let rest = rest.trim_start_matches('\t');
        let parts: Vec<&str> = rest.split('\t').collect();
        let mut views = Vec::new();
        let mut i = 0;
        while i + 1 < parts.len() && views.len() < MAX_VIEWS {
            let name = parts[i].trim();
            let query = parts[i + 1].trim();
            i += 2;
            if name.is_empty() || query.is_empty() {
                continue;
            }
            views.push(CachedView {
                name: wire_field(name, 27),
                query: wire_field(query, 219),
                tasks: Vec::new(),
            });
        }
        self.selected = views;
        self.persist_selection();
        self.force_refresh();
    }

    pub fn handle_line(&mut self, raw: &str) -> Vec<String> {
        let raw = raw.trim();
        if raw.starts_with("TODOIST_CLOSE\t") && !raw.starts_with("TODOIST_CLOSE\tv1") {
            let id = raw.strip_prefix("TODOIST_CLOSE\t").unwrap_or("").trim();
            let ok = fetch::close_task(&self.paths, id).is_ok();
            if ok {
                // Drop task from cache so next push matches UI.
                self.drop_task_from_cache(id);
            }
            return vec![encode_close_ack(ok, id)];
        }
        if raw.starts_with("TODOIST_SEL\t") || raw == "TODOIST_SEL" {
            self.apply_selection_line(raw);
            return self.build_lines();
        }
        if raw.starts_with("TODOIST_TOKEN\t") {
            let token = raw.strip_prefix("TODOIST_TOKEN\t").unwrap_or("").trim();
            let _ = self.save_token(token);
            self.force_refresh();
            // The panel sends this while it is still writing. A bundle reply on
            // this same turn fills the USB buffer and drops the link.
            return Vec::new();
        }
        if raw == "TODOIST?" || raw.starts_with("TODOIST?\t") {
            // Default: serve cache (and refresh only if due). `TODOIST?\tforce` hits the API.
            // Force-on-every-ask stacked multi-line USB bundles and zeroed lists on the panel.
            let force = raw.split('\t').any(|part| part == "force");
            if force || self.load_cached_views().is_empty() {
                self.force_refresh();
            }
            return self.build_lines();
        }
        Vec::new()
    }

    fn drop_task_from_cache(&self, id: &str) {
        let mut views = self.load_cached_views();
        for view in &mut views {
            view.tasks.retain(|t| t.id != id);
        }
        let payload = serde_json::json!({
            "fetched_at": format!("{:?}", std::time::SystemTime::now()),
            "views": views,
        });
        if let Ok(raw) = serde_json::to_string_pretty(&payload) {
            let _ = write_atomic(&self.paths.cache_json(), raw);
        }
    }
}

impl Default for TodoistCapability {
    fn default() -> Self {
        Self::default_home()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encode_unlinked() {
        let lines = encode_todoist_lines(true, false, &[]);
        assert_eq!(lines[0], "TODOIST\tv1\t1\t0\t0");
        assert!(lines.iter().any(|l| l.starts_with("TODOIST_END\t")));
    }

    #[test]
    fn encode_one_view_one_task() {
        let lines = encode_todoist_lines(
            true,
            true,
            &[CachedView {
                name: "Hoje".into(),
                query: "today | overdue".into(),
                tasks: vec![CachedTask {
                    id: "123".into(),
                    content: "Comprar\tpão".into(),
                    meta: "hoje".into(),
                    priority: 4,
                }],
            }],
        );
        assert_eq!(lines[0], "TODOIST\tv1\t1\t1\t1\tHoje\ttoday | overdue");
        assert_eq!(
            lines[1],
            "TODOIST_TASKS\tv1\t0\t1\t123\t4\tComprar pão\thoje"
        );
        assert_eq!(lines[2], "TODOIST_END\tv1\t1\t1\t1");
    }

    #[test]
    fn close_ack_wire() {
        assert_eq!(
            encode_close_ack(true, "abc"),
            "TODOIST_CLOSE\tv1\t1\tabc"
        );
    }

    #[test]
    fn encode_sends_all_tasks_like_wifi_ceiling() {
        let tasks: Vec<CachedTask> = (0..16)
            .map(|i| CachedTask {
                id: format!("id{i}"),
                content: format!("task {i}"),
                meta: String::new(),
                priority: 1,
            })
            .collect();
        let lines = encode_todoist_lines(
            true,
            true,
            &[CachedView {
                name: "Trabalho".into(),
                query: "@Trabalho".into(),
                tasks,
            }],
        );
        let task_lines: Vec<_> = lines
            .iter()
            .filter(|l| l.starts_with("TODOIST_TASKS\t"))
            .collect();
        // 16 short tasks fit one line under the budget.
        assert_eq!(task_lines.len(), 1);
        assert!(task_lines[0].starts_with("TODOIST_TASKS\tv1\t0\t16\t"));
        assert!(task_lines[0].len() <= WIRE_LINE_BUDGET);
    }

    #[test]
    fn token_is_saved_without_writing_a_bundle() {
        let root = std::env::temp_dir().join(format!(
            "tessera-todoist-token-{}",
            std::process::id()
        ));
        let _ = fs::remove_dir_all(&root);
        let mut cap = TodoistCapability::new(TodoistPaths { root: root.clone() });
        let lines = cap.handle_line("TODOIST_TOKEN\tsecret-token");
        assert!(lines.is_empty());
        assert!(cap.linked());
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn query_without_force_serves_cache_shape() {
        // Unlinked → still a valid 2-line bundle (no panic / empty).
        let mut cap = TodoistCapability::new(TodoistPaths {
            root: std::env::temp_dir().join(format!("tessera-todoist-test-{}", std::process::id())),
        });
        let lines = cap.handle_line("TODOIST?");
        assert!(lines.iter().any(|l| l.starts_with("TODOIST\t")));
        assert!(lines.iter().any(|l| l.starts_with("TODOIST_END\t")));
    }

    #[test]
    fn encode_empty_view_still_emits_tasks_line() {
        let lines = encode_todoist_lines(
            true,
            true,
            &[CachedView {
                name: "Celular".into(),
                query: "@Celular".into(),
                tasks: vec![],
            }],
        );
        assert_eq!(lines[1], "TODOIST_TASKS\tv1\t0\t0");
    }

    #[test]
    fn encode_splits_when_over_budget() {
        let tasks: Vec<CachedTask> = (0..40)
            .map(|i| CachedTask {
                id: format!("id{i:02}"),
                content: "x".repeat(64),
                meta: "m".repeat(28),
                priority: 1,
            })
            .collect();
        let lines = encode_todoist_lines(
            true,
            true,
            &[CachedView {
                name: "Big".into(),
                query: "@Big".into(),
                tasks,
            }],
        );
        let task_lines: Vec<_> = lines
            .iter()
            .filter(|l| l.starts_with("TODOIST_TASKS\t"))
            .collect();
        assert!(task_lines.len() >= 2, "expected split, got {}", task_lines.len());
        for line in &task_lines {
            assert!(line.len() <= WIRE_LINE_BUDGET + 80); // last overflowed single still ok
        }
        let total: usize = task_lines
            .iter()
            .map(|l| {
                l.split('\t')
                    .nth(3)
                    .and_then(|n| n.parse().ok())
                    .unwrap_or(0)
            })
            .sum();
        assert_eq!(total, 40);
    }
}

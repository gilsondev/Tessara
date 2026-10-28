//! FOCUS capability — list / SET / OFF macOS Focus modes (parity with Python agent).

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Mutex, OnceLock};
use std::thread;
use std::time::{Duration, Instant};

const READ_TIMEOUT: Duration = Duration::from_secs(2);
const SHORTCUT_RUN_TIMEOUT: Duration = Duration::from_secs(8);

#[cfg(test)]
static SHORTCUT_OVERRIDE: Mutex<Option<std::sync::Arc<dyn Fn(&str) -> bool + Send + Sync>>> =
    Mutex::new(None);
#[cfg(test)]
static FOCUS_TEST: Mutex<()> = Mutex::new(());

use serde_json::Value;
use tessera_protocol::focus_line;

pub const FOCUS_PUSH_INTERVAL_S: u64 = 10;
/// Safety-net poll; Focus wakes come from DNC + Assertions mtime on mac_events.
pub const FOCUS_POLL_INTERVAL_S: u64 = 8;

const ID_MAX: usize = 63;
const NAME_MAX: usize = 40;

static SHORTCUT_NAMES: OnceLock<Mutex<(Instant, std::collections::HashSet<String>)>> =
    OnceLock::new();

const SLUG_TO_APPLE: &[(&str, &str)] = &[
    ("nao-perturbe", "com.apple.donotdisturb.mode.default"),
    ("pessoal", "com.apple.focus.personal-time"),
    ("trabalho", "com.apple.focus.work"),
    ("sono", "com.apple.sleep.sleep-mode"),
    ("menos-interrupcoes", "com.apple.focus.reduce-interruptions"),
    ("exercicio", "com.apple.donotdisturb.mode.workout"),
    ("estudos", "com.apple.donotdisturb.mode.books.vertical.fill"),
];

const SILENT_ON: &[(&str, &[&str])] = &[
    (
        "nao-perturbe",
        &["Tessera Focus Não Perturbe", "Definir Não Perturbe"],
    ),
    ("pessoal", &["Tessera Focus Pessoal"]),
    ("trabalho", &["Tessera Focus Trabalho"]),
    ("sono", &["Tessera Focus Sono"]),
    (
        "menos-interrupcoes",
        &["Tessera Focus Menos Interrupções"],
    ),
    ("exercicio", &["Tessera Focus Exercício"]),
    ("estudos", &["Tessera Focus Estudos"]),
];

const SILENT_OFF: &[&str] = &[
    "Tessera Focus Off",
    "Desativar Não Perturbe",
    "Interromper Foco no Trabalho",
    "Interromper Foco nos Estudos",
];

const MUTABLE_ON_SHORTCUT: &str = "Definir Não Perturbe";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FocusMode {
    pub id: String,
    pub name: String,
}

pub fn encode_focus_line(
    ok: bool,
    active_id: &str,
    active_name: &str,
    modes: &[FocusMode],
) -> String {
    let pairs: Vec<(&str, &str)> = modes
        .iter()
        .map(|m| (m.id.as_str(), m.name.as_str()))
        .collect();
    focus_line(ok, active_id, active_name, &pairs)
}

pub fn focus_line_changed(last_pushed: Option<&str>, encoded: &str) -> bool {
    last_pushed != Some(encoded)
}

fn mode_db_path() -> PathBuf {
    dirs_home().join("Library/DoNotDisturb/DB/ModeConfigurations.json")
}

fn assertions_db_path() -> PathBuf {
    dirs_home().join("Library/DoNotDisturb/DB/Assertions.json")
}

fn dirs_home() -> PathBuf {
    std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/"))
}

fn sanitize(text: &str, limit: usize) -> String {
    let cleaned: String = text
        .chars()
        .map(|c| match c {
            '\t' | '\n' | '\r' => ' ',
            c => c,
        })
        .collect();
    let trimmed = cleaned.split_whitespace().collect::<Vec<_>>().join(" ");
    if trimmed.chars().count() > limit {
        trimmed.chars().take(limit).collect()
    } else {
        trimmed
    }
}

fn slug(name: &str) -> String {
    let folded = name.trim().to_lowercase();
    let base: String = folded
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() {
                c
            } else {
                '-'
            }
        })
        .collect();
    let collapsed = base
        .split('-')
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join("-");
    let s = if collapsed.is_empty() {
        "focus".to_string()
    } else {
        collapsed
    };
    s.chars().take(ID_MAX).collect()
}

fn friendly_from_id(mode_id: &str) -> Option<&'static str> {
    let low = mode_id.to_lowercase();
    const NEEDLES: &[(&str, &str)] = &[
        ("donotdisturb.mode.default", "Não Perturbe"),
        ("reduce-interruptions", "Menos Interrupções"),
        ("sleep-mode", "Sono"),
        ("personal", "Pessoal"),
        ("workout", "Exercício"),
        ("focus.work", "Trabalho"),
        ("mode.work", "Trabalho"),
        ("books", "Estudos"),
        ("driving", "Dirigindo"),
        ("gaming", "Jogos"),
        ("mindfulness", "Mindfulness"),
    ];
    for (needle, label) in NEEDLES {
        if *needle == ".work" && low.contains("workout") {
            continue;
        }
        if low.contains(needle) {
            return Some(label);
        }
    }
    None
}

fn looks_like_bundle_id(text: &str) -> bool {
    let t = text.trim();
    t.starts_with("com.") || t.starts_with("net.") || t.contains('.')
}

fn display_name(mode_id: &str, name: &str) -> String {
    if !name.is_empty() && !looks_like_bundle_id(name) && name != mode_id {
        return sanitize(name, NAME_MAX);
    }
    if let Some(mapped) = friendly_from_id(mode_id) {
        return mapped.to_string();
    }
    if !name.is_empty() && !looks_like_bundle_id(name) {
        return sanitize(name, NAME_MAX);
    }
    "Focus".to_string()
}

fn load_plist_json(path: &Path) -> Option<Value> {
    if !path.is_file() {
        return None;
    }
    let out = tessera_proc::command_output_timeout(
        Command::new("plutil").args(["-convert", "json", "-o", "-", path.to_str()?]),
        READ_TIMEOUT,
    )?;
    if !out.status.success() {
        return None;
    }
    serde_json::from_slice(&out.stdout).ok()
}

fn push_mode(
    out: &mut Vec<FocusMode>,
    seen: &mut std::collections::HashSet<String>,
    mode_id: &str,
    name: &str,
) {
    let id_src = if mode_id.is_empty() {
        slug(name)
    } else {
        mode_id.to_string()
    };
    let mid = sanitize(&id_src, ID_MAX);
    if mid.is_empty() || seen.contains(&mid) {
        return;
    }
    let nm = display_name(&mid, name);
    seen.insert(mid.clone());
    out.push(FocusMode { id: mid, name: nm });
}

fn modes_from_db() -> Vec<FocusMode> {
    let Some(root) = load_plist_json(&mode_db_path()) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    let mut seen = std::collections::HashSet::new();

    if let Some(data) = root.get("data").and_then(|d| d.as_array()) {
        for block in data {
            let configs = block.get("modeConfigurations").and_then(|c| c.as_object());
            if let Some(configs) = configs {
                for (key, cfg) in configs {
                    let mut name = String::new();
                    let mut ident = key.clone();
                    if let Some(mode) = cfg.get("mode").and_then(|m| m.as_object()) {
                        if let Some(n) = mode.get("name").and_then(|v| v.as_str()) {
                            name = n.to_string();
                        }
                        ident = mode
                            .get("modeIdentifier")
                            .or_else(|| mode.get("identifier"))
                            .and_then(|v| v.as_str())
                            .unwrap_or(key)
                            .to_string();
                    }
                    if !name.is_empty() {
                        push_mode(&mut out, &mut seen, &ident, &name);
                    }
                }
                if !out.is_empty() {
                    return out.into_iter().take(12).collect();
                }
            }
        }
    }
    out
}

fn list_modes() -> Vec<FocusMode> {
    // Only modes configured on this Mac — never invent Control Center defaults.
    modes_from_db()
}

fn active_from_assertions(modes: &[FocusMode]) -> (String, String) {
    let root = match load_plist_json(&assertions_db_path()) {
        Some(v) => v,
        None => return (String::new(), String::new()),
    };
    let data = root.get("data").and_then(|d| d.as_array());
    let first = data.and_then(|a| a.first()).and_then(|v| v.as_object());
    let records = first
        .and_then(|o| o.get("storeAssertionRecords"))
        .and_then(|r| r.as_array());
    let records = match records {
        Some(r) if !r.is_empty() => r,
        _ => return (String::new(), String::new()),
    };

    let mut best_id = String::new();
    let mut best_ts = -1.0_f64;
    for rec in records {
        let Some(rec) = rec.as_object() else { continue };
        let details = rec.get("assertionDetails").and_then(|d| d.as_object());
        let Some(details) = details else { continue };
        let mid = details
            .get("assertionDetailsModeIdentifier")
            .or_else(|| details.get("modeIdentifier"))
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if mid.is_empty() {
            continue;
        }
        let ts = rec
            .get("assertionStartDateTimestamp")
            .and_then(|v| v.as_f64())
            .unwrap_or(0.0);
        if best_id.is_empty() || ts >= best_ts {
            best_id = mid.to_string();
            best_ts = ts;
        }
    }
    if best_id.is_empty() {
        return (String::new(), String::new());
    }
    let found_name = modes
        .iter()
        .find(|m| m.id == best_id)
        .map(|m| m.name.as_str())
        .unwrap_or("");
    let nm = display_name(&best_id, found_name);
    (sanitize(&best_id, ID_MAX), sanitize(&nm, NAME_MAX))
}

pub struct FocusSnapshot {
    pub ok: bool,
    pub active_id: String,
    pub active_name: String,
    pub modes: Vec<FocusMode>,
}

pub fn snapshot_with_optimistic(
    optimistic_id: &str,
    optimistic_name: &str,
) -> FocusSnapshot {
    let modes = list_modes();
    if modes.is_empty() {
        return FocusSnapshot {
            ok: false,
            active_id: String::new(),
            active_name: String::new(),
            modes: vec![],
        };
    }
    let (mut active_id, mut active_name) = active_from_assertions(&modes);
    if active_id.is_empty() && (!optimistic_id.is_empty() || !optimistic_name.is_empty()) {
        active_id = optimistic_id.to_string();
        active_name = optimistic_name.to_string();
    } else if !active_id.is_empty() {
        active_name = display_name(&active_id, &active_name);
    }
    FocusSnapshot {
        ok: true,
        active_id,
        active_name,
        modes: modes.into_iter().take(12).collect(),
    }
}

fn known_shortcut_names() -> std::collections::HashSet<String> {
    let cache = SHORTCUT_NAMES.get_or_init(|| Mutex::new((Instant::now(), Default::default())));
    let mut guard = cache.lock().unwrap();
    if guard.0.elapsed() < Duration::from_secs(90) && !guard.1.is_empty() {
        return guard.1.clone();
    }
    let Some(out) = tessera_proc::command_output_timeout(
        Command::new("shortcuts").args(["list"]),
        READ_TIMEOUT,
    ) else {
        return guard.1.clone();
    };
    let mut names = std::collections::HashSet::new();
    if out.status.success() {
        for line in String::from_utf8_lossy(&out.stdout).lines() {
            let t = line.trim();
            if !t.is_empty() {
                names.insert(t.to_string());
            }
        }
    }
    guard.0 = Instant::now();
    guard.1 = names.clone();
    names
}

fn run_shortcut(label: &str) -> bool {
    if label.is_empty() {
        return false;
    }
    tessera_proc::command_output_timeout(
        Command::new("shortcuts").args(["run", label]),
        SHORTCUT_RUN_TIMEOUT,
    )
    .map(|o| o.status.success())
    .unwrap_or(false)
}

fn run_known_shortcut(label: &str) -> bool {
    if label.is_empty() {
        return false;
    }
    #[cfg(test)]
    if let Some(run) = SHORTCUT_OVERRIDE.lock().ok().and_then(|guard| guard.clone()) {
        return run(label);
    }
    if !known_shortcut_names().contains(label) {
        return false;
    }
    run_shortcut(label)
}

fn apple_to_slug() -> HashMap<&'static str, &'static str> {
    SLUG_TO_APPLE
        .iter()
        .map(|(s, a)| (*a, *s))
        .collect()
}

fn focus_slug(mid: &str, name: &str) -> String {
    let apple_map = apple_to_slug();
    for (slug_key, _labels) in SILENT_ON {
        if mid == *slug_key {
            return slug_key.to_string();
        }
    }
    if let Some(s) = apple_map.get(mid) {
        return (*s).to_string();
    }
    let friendly = friendly_from_id(mid).unwrap_or(name);
    let s = slug(friendly);
    if SILENT_ON.iter().any(|(k, _)| *k == s.as_str()) {
        return s;
    }
    let low = name.to_lowercase();
    if low.contains("perturb") || low.contains("disturb") {
        return "nao-perturbe".to_string();
    }
    if low.contains("pessoal") || low.contains("personal") {
        return "pessoal".to_string();
    }
    if low.contains("trabalho") || low == "work" {
        return "trabalho".to_string();
    }
    if low.contains("sono") || low.contains("sleep") {
        return "sono".to_string();
    }
    s
}

fn row_for_id(mode_id: &str, modes: &[FocusMode]) -> Option<FocusMode> {
    let want = mode_id.trim();
    if want.is_empty() {
        return None;
    }
    for row in modes {
        if row.id == want || row.name == want {
            return Some(row.clone());
        }
    }
    let want_slug = slug(want);
    for row in modes {
        if slug(&row.name) == want_slug || slug(&row.id) == want_slug {
            return Some(row.clone());
        }
    }
    for (slug_key, apple) in SLUG_TO_APPLE {
        if *slug_key == want || *slug_key == want_slug {
            if let Some(row) = modes.iter().find(|m| m.id == *apple) {
                return Some(row.clone());
            }
            return Some(FocusMode {
                id: (*apple).to_string(),
                name: want.to_string(),
            });
        }
    }
    None
}

fn shortcut_on_candidates(mid: &str, name: &str) -> Vec<String> {
    let slug_key = focus_slug(mid, name);
    let mut out: Vec<String> = Vec::new();
    for (key, labels) in SILENT_ON {
        if *key == slug_key.as_str() {
            out.extend(labels.iter().map(|s| (*s).to_string()));
        }
    }
    let display = display_name(mid, name);
    out.push(format!("Tessera Focus {display}"));
    if slug_key == "nao-perturbe" {
        out.push(MUTABLE_ON_SHORTCUT.to_string());
    }
    let mut seen = std::collections::HashSet::new();
    out.into_iter()
        .filter(|l| seen.insert(l.clone()))
        .collect()
}

fn apple_epoch_now() -> f64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64() - 978_307_200.0)
        .unwrap_or(0.0)
}

fn resolve_apple_mode_id(mode_id: &str, modes: &[FocusMode]) -> String {
    let target = mode_id.trim();
    if target.is_empty() {
        return String::new();
    }
    if let Some(row) = row_for_id(target, modes) {
        // Prefer real Apple identifiers from ModeConfigurations.
        if row.id.contains('.') {
            return row.id;
        }
        let slug_key = focus_slug(&row.id, &row.name);
        for (s, apple) in SLUG_TO_APPLE {
            if *s == slug_key.as_str() {
                return (*apple).to_string();
            }
        }
        return row.id;
    }
    let slug_key = focus_slug(target, target);
    for (s, apple) in SLUG_TO_APPLE {
        if *s == slug_key.as_str() || *apple == target {
            return (*apple).to_string();
        }
    }
    target.to_string()
}

fn notify_focus_changed(_enabled: bool) {
    // Restart DND daemon so Control Center re-reads Assertions.json.
    if let Some(uid) = tessera_proc::command_output_timeout(
        Command::new("id").arg("-u"),
        READ_TIMEOUT,
    ) {
        let uid = String::from_utf8_lossy(&uid.stdout).trim().to_string();
        for svc in [
            format!("gui/{uid}/com.apple.donotdisturbd"),
            format!("gui/{uid}/com.apple.donotdisturb.private.donotdisturbd"),
        ] {
            let _ = tessera_proc::command_output_timeout(
                Command::new("launchctl").args(["kickstart", "-k", &svc]),
                READ_TIMEOUT,
            );
        }
    }
}

/// Write Focus assertion directly (works without Tessera Shortcuts installed).
fn apply_focus_via_assertions(apple_mode_id: Option<&str>) -> bool {
    let path = assertions_db_path();
    let mut root = match load_plist_json(&path) {
        Some(v) => v,
        None => {
            // Minimal envelope if file missing.
            serde_json::json!({
                "header": {"version": 8, "timestamp": apple_epoch_now()},
                "data": [{
                    "storeInvalidationRecords": [],
                    "storeInvalidationRequestRecords": [],
                    "storeAssertionRecords": []
                }]
            })
        }
    };
    let now = apple_epoch_now();
    if let Some(header) = root.get_mut("header").and_then(|h| h.as_object_mut()) {
        header.insert("timestamp".into(), serde_json::json!(now));
    }
    let Some(data) = root.get_mut("data").and_then(|d| d.as_array_mut()) else {
        return false;
    };
    if data.is_empty() {
        data.push(serde_json::json!({
            "storeInvalidationRecords": [],
            "storeInvalidationRequestRecords": [],
            "storeAssertionRecords": []
        }));
    }
    let Some(block) = data.get_mut(0).and_then(|b| b.as_object_mut()) else {
        return false;
    };
    let records = match apple_mode_id {
        Some(mid) if !mid.is_empty() => {
            let uuid = format!(
                "{:08X}-{:04X}-{:04X}-{:04X}-{:012X}",
                (now as u64) & 0xFFFF_FFFF,
                ((now as u64) >> 16) & 0xFFFF,
                0x4000 | (((now as u64) >> 32) & 0x0FFF),
                0x8000 | (((now as u64) >> 40) & 0x3FFF),
                (now as u64).wrapping_mul(0x9E37) & 0xFFFF_FFFF_FFFF
            );
            serde_json::json!([{
                "assertionUUID": uuid,
                "assertionSource": {
                    "assertionClientIdentifier": "dev.tessera.agentd",
                    "assertionSourceDeviceIdentifier": "local"
                },
                "assertionStartDateTimestamp": now,
                "assertionDetails": {
                    "assertionDetailsIdentifier": "dev.tessera.agentd.focus",
                    "assertionDetailsModeIdentifier": mid,
                    "assertionDetailsReason": "user-action"
                }
            }])
        }
        _ => serde_json::json!([]),
    };
    block.insert("storeAssertionRecords".into(), records);
    let Ok(text) = serde_json::to_string_pretty(&root) else {
        return false;
    };
    if std::fs::write(&path, text).is_err() {
        eprintln!("agentd: focus assertions write falhou em {}", path.display());
        return false;
    }
    notify_focus_changed(apple_mode_id.is_some());
    true
}

pub fn set_focus(mode_id: &str, modes: &[FocusMode]) -> bool {
    let target = mode_id.trim();
    if target.is_empty() {
        return false;
    }
    let row = row_for_id(target, modes);
    let (name, mid) = match row {
        Some(r) => (r.name.clone(), r.id.clone()),
        None => (target.to_string(), slug(target)),
    };
    let slug_key = focus_slug(&mid, &name);

    if slug_key == "nao-perturbe" && run_known_shortcut(MUTABLE_ON_SHORTCUT) {
        eprintln!("agentd: focus ON {name:?} via {MUTABLE_ON_SHORTCUT:?}");
        return true;
    }

    for label in shortcut_on_candidates(&mid, &name) {
        if label == MUTABLE_ON_SHORTCUT {
            continue;
        }
        if run_known_shortcut(&label) {
            eprintln!("agentd: focus ON {name:?} via {label:?}");
            return true;
        }
    }

    let apple = resolve_apple_mode_id(target, modes);
    if apply_focus_via_assertions(Some(&apple)) {
        eprintln!("agentd: focus ON {name:?} via Assertions.json ({apple})");
        return true;
    }
    eprintln!("agentd: focus SET falhou para {name:?}");
    false
}

pub fn clear_focus() -> bool {
    for name in SILENT_OFF {
        if run_known_shortcut(name) {
            eprintln!("agentd: focus OFF via {name:?}");
            return true;
        }
    }
    if apply_focus_via_assertions(None) {
        eprintln!("agentd: focus OFF via Assertions.json");
        return true;
    }
    eprintln!("agentd: focus OFF falhou");
    false
}

enum FocusJob {
    Set { mode_id: String, modes: Vec<FocusMode> },
    Off,
}

pub struct FocusCapability {
    last_modes: Vec<FocusMode>,
    last_pushed: Option<String>,
    optimistic_id: String,
    optimistic_name: String,
    last_assert_mtime: f64,
    last_modes_mtime: f64,
    jobs: Sender<FocusJob>,
    done: Receiver<bool>,
}

impl FocusCapability {
    pub fn new() -> Self {
        let (jobs, job_rx) = mpsc::channel();
        let (done_tx, done) = mpsc::channel();
        thread::spawn(move || {
            while let Ok(mut job) = job_rx.recv() {
                while let Ok(next) = job_rx.try_recv() {
                    job = next;
                }
                let ok = match job {
                    FocusJob::Set { mode_id, modes } => {
                        let modes = if modes.is_empty() { list_modes() } else { modes };
                        set_focus(&mode_id, &modes)
                    }
                    FocusJob::Off => clear_focus(),
                };
                let _ = done_tx.send(ok);
            }
        });
        Self {
            last_modes: Vec::new(),
            last_pushed: None,
            optimistic_id: String::new(),
            optimistic_name: String::new(),
            last_assert_mtime: 0.0,
            last_modes_mtime: 0.0,
            jobs,
            done,
        }
    }

    pub fn last_pushed_line(&self) -> Option<&str> {
        self.last_pushed.as_deref()
    }

    pub fn note_pushed(&mut self, line: &str) {
        self.last_pushed = Some(line.to_string());
        self.last_assert_mtime = file_mtime(&assertions_db_path());
        self.last_modes_mtime = file_mtime(&mode_db_path());
    }

    fn snapshot(&mut self) -> FocusSnapshot {
        let modes = list_modes();
        self.last_modes = modes.clone();
        let snap = snapshot_with_optimistic(&self.optimistic_id, &self.optimistic_name);
        if !snap.active_id.is_empty() {
            self.optimistic_id.clear();
            self.optimistic_name.clear();
        }
        snap
    }

    pub fn build_line(&mut self) -> String {
        let snap = self.snapshot();
        encode_focus_line(
            snap.ok,
            &snap.active_id,
            &snap.active_name,
            &snap.modes,
        )
    }

    pub fn poll_dirty(&mut self) -> bool {
        let assert_m = file_mtime(&assertions_db_path());
        let modes_m = file_mtime(&mode_db_path());
        if assert_m != self.last_assert_mtime || modes_m != self.last_modes_mtime {
            self.last_assert_mtime = assert_m;
            self.last_modes_mtime = modes_m;
            return true;
        }
        false
    }

    pub fn handle_line(&mut self, text: &str) -> Vec<String> {
        let raw = text.trim();
        if raw.is_empty() {
            return vec![];
        }
        if raw.starts_with("FOCUS?") || raw == "FOCUS\tLIST" {
            return vec![self.build_line()];
        }
        if raw.starts_with("FOCUS\tSET\t") {
            let mode_id = raw.split('\t').nth(2).unwrap_or("").trim();
            let modes = self.last_modes.clone();
            if let Some(row) = row_for_id(mode_id, &modes) {
                self.optimistic_id = row.id;
                self.optimistic_name = sanitize(&row.name, NAME_MAX);
            } else {
                self.optimistic_id = slug(mode_id);
                self.optimistic_name = sanitize(mode_id, NAME_MAX);
            }
            let _ = self.jobs.send(FocusJob::Set {
                mode_id: mode_id.to_string(),
                modes,
            });
            // The shortcut runs on the worker. The USB loop sends the line when it finishes.
            return vec![];
        }
        if raw.starts_with("FOCUS\tOFF") || raw == "FOCUS\tOFF" {
            self.optimistic_id.clear();
            self.optimistic_name.clear();
            let _ = self.jobs.send(FocusJob::Off);
            return vec![];
        }
        vec![]
    }

    /// Line to push after a SET/OFF worker finishes. Empty while the shortcut is still running.
    pub fn take_finished_line(&mut self) -> Option<String> {
        let mut saw = false;
        let mut ok = true;
        while let Ok(result) = self.done.try_recv() {
            saw = true;
            ok = result;
        }
        if !saw {
            return None;
        }
        if !ok {
            self.optimistic_id.clear();
            self.optimistic_name.clear();
        }
        Some(self.build_line())
    }
}

impl Default for FocusCapability {
    fn default() -> Self {
        Self::new()
    }
}

fn file_mtime(path: &Path) -> f64 {
    std::fs::metadata(path)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encode_fail_matches_python() {
        assert_eq!(
            encode_focus_line(false, "", "", &[]),
            "FOCUS\tv1\t0\t\t\t0"
        );
    }

    #[test]
    fn encode_ok_with_modes() {
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
        assert_eq!(
            encode_focus_line(true, "trabalho", "Trabalho", &modes),
            "FOCUS\tv1\t1\ttrabalho\tTrabalho\t2\ttrabalho\tTrabalho\tpessoal\tPessoal"
        );
    }

    #[test]
    fn queued_focus_sets_keep_only_the_last() {
        let _guard = FOCUS_TEST.lock().unwrap();
        let seen = std::sync::Arc::new(std::sync::Mutex::new(Vec::<String>::new()));
        let log = seen.clone();
        *SHORTCUT_OVERRIDE.lock().unwrap() = Some(std::sync::Arc::new(move |label: &str| {
            log.lock().unwrap().push(label.to_string());
            if label.contains("hold") {
                thread::sleep(Duration::from_millis(800));
            }
            true
        }));
        struct Clear;
        impl Drop for Clear {
            fn drop(&mut self) {
                *SHORTCUT_OVERRIDE.lock().unwrap() = None;
            }
        }
        let _clear = Clear;
        let mut cap = FocusCapability::new();
        assert!(cap.handle_line("FOCUS\tSET\thold").is_empty());
        thread::sleep(Duration::from_millis(150));
        assert!(cap.handle_line("FOCUS\tSET\ta").is_empty());
        assert!(cap.handle_line("FOCUS\tSET\tb").is_empty());
        assert!(cap.handle_line("FOCUS\tSET\tc").is_empty());
        let deadline = Instant::now() + Duration::from_secs(4);
        loop {
            let labels = seen.lock().unwrap().clone();
            if labels.iter().any(|label| label.contains("Focus c")) {
                break;
            }
            assert!(Instant::now() < deadline, "labels={labels:?}");
            thread::sleep(Duration::from_millis(40));
        }
        thread::sleep(Duration::from_millis(200));
        let labels = seen.lock().unwrap().clone();
        assert!(
            !labels.iter().any(|label| label.contains("Focus a") || label.contains("Focus b")),
            "{labels:?}"
        );
        let line = cap.take_finished_line().unwrap_or_default();
        assert!(line.contains('c'), "{line}");
    }

    #[test]
    fn shortcut_run_leaves_the_caller_free() {
        let _guard = FOCUS_TEST.lock().unwrap();
        let finished = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let flag = finished.clone();
        *SHORTCUT_OVERRIDE.lock().unwrap() = Some(std::sync::Arc::new(move |_label| {
            thread::sleep(Duration::from_secs(4));
            flag.store(true, std::sync::atomic::Ordering::SeqCst);
            true
        }));
        struct Clear;
        impl Drop for Clear {
            fn drop(&mut self) {
                *SHORTCUT_OVERRIDE.lock().unwrap() = None;
            }
        }
        let _clear = Clear;
        let mut cap = FocusCapability::new();
        let started = Instant::now();
        let replies = cap.handle_line("FOCUS\tSET\ttrabalho");
        assert!(replies.is_empty());
        assert!(started.elapsed() < Duration::from_millis(400));
        let mut beats = 0;
        while started.elapsed() < Duration::from_secs(3) {
            beats += 1;
            thread::sleep(Duration::from_millis(200));
        }
        assert!(beats >= 8, "beats={beats}");
        assert!(!finished.load(std::sync::atomic::Ordering::SeqCst));
        let deadline = Instant::now() + Duration::from_secs(6);
        let mut line = None;
        while line.is_none() && Instant::now() < deadline {
            line = cap.take_finished_line();
            thread::sleep(Duration::from_millis(50));
        }
        assert!(finished.load(std::sync::atomic::Ordering::SeqCst));
        assert!(line.unwrap().starts_with("FOCUS\t"));
    }

    #[test]
    fn focus_line_changed_compares_full_wire() {
        let a = "FOCUS\tv1\t1\ttrabalho\tTrabalho\t7\t…";
        let b = "FOCUS\tv1\t1\t\t\t7\t…";
        assert!(focus_line_changed(None, a));
        assert!(!focus_line_changed(Some(a), a));
        assert!(focus_line_changed(Some(b), a));
    }
}

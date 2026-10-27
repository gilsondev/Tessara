//! Localhost HTTP IPC for companion UI (127.0.0.1 only).

use std::net::SocketAddr;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, AtomicUsize, Ordering};
use std::sync::{Arc, RwLock};
use std::time::{SystemTime, UNIX_EPOCH};

use axum::extract::{Query, State};
use axum::http::Method;
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::{Deserialize, Serialize};
use tessera_doctor::{prepare_permission, DoctorDeps, DoctorReport, PreparePermission};
use tessera_logbuf::SharedLogBuf;
use tessera_profile::{
    load_or_init_with_paths, read_scan_json, write_config, write_machine_display_name,
    write_scan_json, AgentdConfig, ProfilePaths,
};
use tessera_scan::{run_scan, ScanConfigEnabled, ScanItem, ScanResult, ScanRoots};
use tokio::net::TcpListener;
use tower_http::cors::{Any, CorsLayer};

pub const DEFAULT_IPC_PORT: u16 = 19792;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UsbLink {
    Disconnected,
    #[allow(dead_code)] // set when USB loop connects (parity task)
    Up,
}

impl UsbLink {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Disconnected => "disconnected",
            Self::Up => "up",
        }
    }
}

/// USB session facts the UI can read even while a capability call is blocked.
pub struct UsbHub {
    pub link: RwLock<UsbLink>,
    pub port: RwLock<String>,
    /// Unix seconds of the last finished USB-loop turn. `0` means never.
    pub last_unix: AtomicU64,
    pub reboot: AtomicBool,
    /// See `REBOOT_*`. The USB loop writes the outcome; IPC waits on it.
    pub reboot_result: AtomicU8,
}

pub const REBOOT_IDLE: u8 = 0;
pub const REBOOT_PENDING: u8 = 1;
pub const REBOOT_DROPPED: u8 = 2;
pub const REBOOT_DTR: u8 = 3;

impl UsbHub {
    pub fn new() -> Self {
        Self {
            link: RwLock::new(UsbLink::Disconnected),
            port: RwLock::new(String::new()),
            last_unix: AtomicU64::new(0),
            reboot: AtomicBool::new(false),
            reboot_result: AtomicU8::new(REBOOT_IDLE),
        }
    }

    pub fn link(&self) -> UsbLink {
        self.link
            .read()
            .map(|g| *g)
            .unwrap_or(UsbLink::Disconnected)
    }

    pub fn set_up(&self, up: bool) {
        if let Ok(mut guard) = self.link.write() {
            *guard = if up {
                UsbLink::Up
            } else {
                UsbLink::Disconnected
            };
        }
    }

    pub fn set_port(&self, path: &str) {
        if let Ok(mut guard) = self.port.write() {
            *guard = path.to_string();
        }
    }

    pub fn port(&self) -> String {
        self.port.read().map(|g| g.clone()).unwrap_or_default()
    }

    pub fn touch(&self) {
        let secs = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        self.last_unix.store(secs, Ordering::Relaxed);
    }

    pub fn age_secs(&self) -> Option<u64> {
        let then = self.last_unix.load(Ordering::Relaxed);
        if then == 0 {
            return None;
        }
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        Some(now.saturating_sub(then))
    }
}

#[derive(Clone)]
pub struct AppState {
    pub usb: Arc<UsbHub>,
    pub profile_paths: ProfilePaths,
    pub logs: SharedLogBuf,
    pub push_queue_depth: Arc<AtomicUsize>,
    pub update: crate::update::UpdateService,
}

impl AppState {
    pub fn new(profile_paths: ProfilePaths, logs: SharedLogBuf) -> Self {
        let update = crate::update::UpdateService::new(profile_paths.clone(), Some(logs.clone()));
        Self {
            usb: Arc::new(UsbHub::new()),
            profile_paths,
            logs,
            push_queue_depth: Arc::new(AtomicUsize::new(0)),
            update,
        }
    }
}

impl Default for AppState {
    fn default() -> Self {
        let paths = ProfilePaths::default_home();
        let logs = Arc::new(
            tessera_logbuf::LogBuf::with_file(paths.agentd_log(), tessera_logbuf::DEFAULT_CAPACITY)
                .unwrap_or_else(|_| tessera_logbuf::LogBuf::new(tessera_logbuf::DEFAULT_CAPACITY)),
        );
        Self::new(paths, logs)
    }
}

#[derive(Serialize)]
struct MachineStatus {
    id: String,
    display_name: String,
}

#[derive(Serialize)]
struct StatusResponse {
    ok: bool,
    version: &'static str,
    usb: &'static str,
    port: String,
    last_usb_age_s: Option<u64>,
    setup_completed: bool,
    machine: MachineStatus,
}

#[derive(Serialize)]
struct HealthResponse {
    ok: bool,
}

#[derive(Serialize)]
struct MachinesResponse {
    machines: Vec<MachineEntry>,
}

#[derive(Serialize)]
struct MachineEntry {
    machine_id: String,
    display_name: String,
    this_machine: bool,
    /// Short labels of what this profile collects (music, Focus, …).
    #[serde(skip_serializing_if = "Vec::is_empty")]
    capabilities: Vec<&'static str>,
}

#[derive(Serialize)]
struct ScanResponse {
    #[serde(flatten)]
    result: ScanResult,
}

#[derive(Deserialize, Default)]
struct ScanApplyBody {
    display_name: Option<String>,
    setup_completed: Option<bool>,
    found: Option<Vec<ScanItemApply>>,
}

#[derive(Deserialize)]
struct ScanItemApply {
    id: String,
    enabled: bool,
}

fn profile_snapshot(paths: &ProfilePaths) -> Result<(AgentdConfig, MachineStatus), String> {
    let profile = load_or_init_with_paths(paths).map_err(|e| e.to_string())?;
    Ok((
        profile.config,
        MachineStatus {
            id: profile.machine_id,
            display_name: profile.display_name,
        },
    ))
}

fn config_from_scan(
    base: &AgentdConfig,
    found: &[ScanItem],
    overrides: Option<&[ScanItemApply]>,
) -> AgentdConfig {
    // Empty `found: []` must not wipe every capability — treat as "no override".
    if let Some(ov) = overrides.filter(|o| !o.is_empty()) {
        // Merge: only ids present in the body change; unknown keys stay.
        let pick = |id: &str, cur: bool| {
            ov.iter()
                .find(|p| p.id == id)
                .map(|p| p.enabled)
                .unwrap_or(cur)
        };
        let mut cfg = AgentdConfig {
            media: pick("media", base.media),
            focus: pick("focus", base.focus),
            calendar: pick("calendar", base.calendar),
            todoist: pick("todoist", base.todoist),
            aitokens_cursor: pick("cursor", base.aitokens_cursor),
            aitokens_claude: pick("claude", base.aitokens_claude),
            aitokens_codex: pick("codex", base.aitokens_codex),
            aitokens: base.aitokens,
            setup_completed: base.setup_completed,
            auto_update: base.auto_update,
        };
        cfg.sync_aitokens_master();
        return cfg;
    }
    let toggles = ScanConfigEnabled::toggles_from_scan(found);
    let mut cfg = AgentdConfig {
        media: toggles.media,
        focus: toggles.focus,
        aitokens: toggles.aitokens,
        // Auto-scan: enable providers that are present (not missing).
        aitokens_cursor: found
            .iter()
            .find(|f| f.id == "cursor")
            .is_some_and(|f| f.state != tessera_scan::ScanState::Missing),
        aitokens_claude: found
            .iter()
            .find(|f| f.id == "claude")
            .is_some_and(|f| f.state != tessera_scan::ScanState::Missing),
        aitokens_codex: found
            .iter()
            .find(|f| f.id == "codex")
            .is_some_and(|f| f.state != tessera_scan::ScanState::Missing),
        calendar: toggles.calendar,
        todoist: base.todoist,
        setup_completed: base.setup_completed,
        auto_update: base.auto_update,
    };
    cfg.sync_aitokens_master();
    cfg
}

fn stamp_enabled(result: &mut ScanResult, config: &AgentdConfig) {
    for item in &mut result.found {
        item.enabled = match item.id.as_str() {
            "media" => config.media,
            "focus" => config.focus,
            "calendar" => config.calendar,
            "cursor" => config.aitokens && config.aitokens_cursor,
            "claude" => config.aitokens && config.aitokens_claude,
            "codex" => config.aitokens && config.aitokens_codex,
            _ => item.enabled,
        };
    }
}

fn persist_scan(
    paths: &ProfilePaths,
    result: &mut ScanResult,
    body: ScanApplyBody,
) -> Result<(AgentdConfig, MachineStatus), String> {
    let (mut config, machine) = profile_snapshot(paths)?;
    let auto = config_from_scan(&config, &result.found, body.found.as_deref());
    config.media = auto.media;
    config.focus = auto.focus;
    config.aitokens = auto.aitokens;
    config.aitokens_cursor = auto.aitokens_cursor;
    config.aitokens_claude = auto.aitokens_claude;
    config.aitokens_codex = auto.aitokens_codex;
    config.calendar = auto.calendar;
    let setup_was = config.setup_completed;
    config.setup_completed = body.setup_completed.unwrap_or(setup_was);
    write_config(&paths.config_toml(), &config).map_err(|e| e.to_string())?;

    stamp_enabled(result, &config);
    let value = serde_json::to_value(&*result).map_err(|e| e.to_string())?;
    write_scan_json(&paths.scan_json(), &value).map_err(|e| e.to_string())?;

    let mut display_name = machine.display_name.clone();
    if let Some(name) = body.display_name {
        write_machine_display_name(&paths.machine_json(), &name).map_err(|e| e.to_string())?;
        display_name = name.trim().to_string();
    }

    Ok((
        config,
        MachineStatus {
            id: machine.id,
            display_name,
        },
    ))
}

async fn v1_status(
    State(state): State<AppState>,
) -> Result<Json<StatusResponse>, (axum::http::StatusCode, String)> {
    let usb = state.usb.link();
    let (config, machine) = profile_snapshot(&state.profile_paths).map_err(internal)?;
    Ok(Json(StatusResponse {
        ok: true,
        version: env!("CARGO_PKG_VERSION"),
        usb: usb.as_str(),
        port: state.usb.port(),
        last_usb_age_s: state.usb.age_secs(),
        setup_completed: config.setup_completed,
        machine,
    }))
}

async fn v1_health() -> Json<HealthResponse> {
    Json(HealthResponse { ok: true })
}

async fn v1_scan_last(
    State(state): State<AppState>,
) -> Result<Json<ScanResponse>, (axum::http::StatusCode, String)> {
    let last =
        read_scan_json(&state.profile_paths.scan_json()).map_err(|e| internal(e.to_string()))?;
    match last {
        Some(value) => {
            let result: ScanResult =
                serde_json::from_value(value).map_err(|e| internal(e.to_string()))?;
            Ok(Json(ScanResponse { result }))
        }
        None => Ok(Json(ScanResponse {
            result: ScanResult {
                scanned_at: String::new(),
                found: vec![],
            },
        })),
    }
}

async fn v1_scan_post(
    State(state): State<AppState>,
    body: Option<Json<ScanApplyBody>>,
) -> Result<Json<ScanResponse>, (axum::http::StatusCode, String)> {
    let (config, _) = profile_snapshot(&state.profile_paths).map_err(internal)?;
    let enabled = ScanConfigEnabled {
        media: config.media,
        focus: config.focus,
        aitokens: config.aitokens,
        calendar: config.calendar,
    };
    let mut result = run_scan(&ScanRoots::default_home(), enabled);
    let apply = body.map(|Json(b)| b).unwrap_or_default();
    let (_, _) = persist_scan(&state.profile_paths, &mut result, apply).map_err(internal)?;
    Ok(Json(ScanResponse { result }))
}

async fn v1_machines(
    State(state): State<AppState>,
) -> Result<Json<MachinesResponse>, (axum::http::StatusCode, String)> {
    let (config, machine) = profile_snapshot(&state.profile_paths).map_err(internal)?;
    let mut capabilities = Vec::new();
    if config.media {
        capabilities.push("Música");
    }
    if config.focus {
        capabilities.push("Focus");
    }
    if config.calendar {
        capabilities.push("Agenda");
    }
    if config.aitokens_cursor || config.aitokens_claude || config.aitokens_codex {
        capabilities.push("AI Tokens");
    }
    Ok(Json(MachinesResponse {
        machines: vec![MachineEntry {
            machine_id: machine.id,
            display_name: machine.display_name,
            this_machine: true,
            capabilities,
        }],
    }))
}

async fn v1_doctor(
    State(state): State<AppState>,
) -> Result<Json<DoctorReport>, (axum::http::StatusCode, String)> {
    let usb = state.usb.link();
    let (config, _) = profile_snapshot(&state.profile_paths).map_err(internal)?;
    let roots = tessera_scan::ScanRoots::default_home();
    let deps = DoctorDeps::live(
        usb == UsbLink::Up,
        state.push_queue_depth.load(Ordering::Relaxed),
        config,
        &roots,
    );
    Ok(Json(tessera_doctor::run_doctor(&deps)))
}

#[derive(Deserialize)]
struct PreparePermissionBody {
    pane: String,
}

async fn v1_permissions_prepare(
    State(state): State<AppState>,
    Json(body): Json<PreparePermissionBody>,
) -> Result<Json<PreparePermission>, (axum::http::StatusCode, String)> {
    let home = state
        .profile_paths
        .tessera_root
        .parent()
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| dirs_next_home());
    let prep = prepare_permission(&body.pane, &home)
        .map_err(|e| (axum::http::StatusCode::BAD_REQUEST, e))?;
    state.logs.append(format!(
        "{} INFO  perms: prepare pane={} binary={}",
        crate::logutil::stamp(),
        body.pane,
        prep.binary_path
    ));
    Ok(Json(prep))
}

fn dirs_next_home() -> std::path::PathBuf {
    std::env::var_os("HOME")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::PathBuf::from("/"))
}

#[derive(Deserialize, Default)]
struct LogsQuery {
    tail: Option<usize>,
}

#[derive(Serialize)]
struct LogsResponse {
    lines: Vec<String>,
}

async fn v1_logs(State(state): State<AppState>, Query(q): Query<LogsQuery>) -> Json<LogsResponse> {
    let tail = q.tail.unwrap_or(200).clamp(1, 2000);
    let mut lines = state.logs.tail(tail);
    if lines.is_empty() {
        if let Ok(from_disk) = state.logs.load_file_tail(tail) {
            lines = from_disk;
        }
    }
    if lines.is_empty() {
        lines.push(format!(
            "{} INFO  agentd: log buffer empty (tail={tail})",
            iso_log_timestamp()
        ));
    }
    Json(LogsResponse { lines })
}

fn iso_log_timestamp() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    format!("{secs}")
}

fn internal(err: String) -> (axum::http::StatusCode, String) {
    (axum::http::StatusCode::INTERNAL_SERVER_ERROR, err)
}

pub fn router(state: AppState) -> Router {
    // Companion UI (Tauri WKWebView) is cross-origin → need CORS on localhost IPC.
    let cors = CorsLayer::new()
        .allow_origin(Any)
        .allow_methods([Method::GET, Method::POST, Method::OPTIONS])
        .allow_headers(Any);

    Router::new()
        .route("/v1/status", get(v1_status))
        .route("/v1/health", get(v1_health))
        .route("/v1/scan", post(v1_scan_post))
        .route("/v1/scan/last", get(v1_scan_last))
        .route("/v1/machines", get(v1_machines))
        .route("/v1/doctor", get(v1_doctor))
        .route("/v1/permissions/prepare", post(v1_permissions_prepare))
        .route("/v1/panel/state", get(v1_panel_state))
        .route("/v1/panel/settings", post(v1_panel_settings))
        .route("/v1/panel/widget_settings", post(v1_panel_widget_settings))
        .route("/v1/panel/dashboard", post(v1_panel_dashboard))
        .route("/v1/panel/refresh", post(v1_panel_refresh))
        .route("/v1/panel/reboot", post(v1_panel_reboot))
        .route("/v1/panel/token", post(v1_panel_token))
        .route("/v1/panel/weather", post(v1_panel_weather))
        .route(
            "/v1/panel/calendar/selection",
            post(v1_panel_calendar_selection),
        )
        .route(
            "/v1/panel/calendar/add_account",
            post(v1_panel_calendar_add),
        )
        .route("/v1/calendar/client", post(v1_calendar_client))
        .route("/v1/calendar/auth/start", post(v1_calendar_auth_start))
        .route("/v1/calendar/auth", get(v1_calendar_auth))
        .route("/v1/calendar/selection", post(v1_calendar_selection))
        .route("/v1/logs", get(v1_logs))
        .route("/v1/update", get(v1_update_get).post(v1_update_post))
        .layer(cors)
        .with_state(state)
}

fn calendar_paths(state: &AppState) -> tessera_caps_calendar::CalendarPaths {
    let mut paths = tessera_caps_calendar::CalendarPaths::default_home();
    paths.root = state.profile_paths.tessera_root.join("calendar");
    paths
}

#[derive(Deserialize)]
struct CalendarClientBody {
    raw: String,
}

async fn v1_calendar_client(
    State(state): State<AppState>,
    Json(body): Json<CalendarClientBody>,
) -> Result<Json<tessera_caps_calendar::AuthStatus>, (axum::http::StatusCode, String)> {
    let paths = calendar_paths(&state);
    tessera_caps_calendar::import_client_json(&paths, &body.raw).map_err(internal)?;
    Ok(Json(tessera_caps_calendar::auth_status(&paths)))
}

async fn v1_calendar_auth_start(
    State(state): State<AppState>,
    body: String,
) -> Json<tessera_caps_calendar::AuthStatus> {
    let paths = calendar_paths(&state);
    let source = if body.contains("\"tessera\"") {
        tessera_caps_calendar::ClientSource::Tessera
    } else {
        tessera_caps_calendar::ClientSource::Imported
    };
    match tessera_caps_calendar::start_with(&paths, source) {
        Ok(status) => Json(status),
        Err(message) => Json(tessera_caps_calendar::AuthStatus::NeedClient { message }),
    }
}

async fn v1_calendar_auth(State(state): State<AppState>) -> Json<tessera_caps_calendar::LocalSnapshot> {
    let paths = calendar_paths(&state);
    Json(tessera_caps_calendar::current_snapshot(&paths))
}

#[derive(Deserialize)]
struct CalendarSelectionBody {
    ids: Vec<String>,
}

async fn v1_calendar_selection(
    State(state): State<AppState>,
    Json(body): Json<CalendarSelectionBody>,
) -> Result<Json<tessera_caps_calendar::LocalSnapshot>, (axum::http::StatusCode, String)> {
    let paths = calendar_paths(&state);
    tessera_caps_calendar::save_selection(&paths, &body.ids).map_err(internal)?;
    Ok(Json(tessera_caps_calendar::refresh_local(&paths)))
}

async fn v1_update_get(State(state): State<AppState>) -> Json<crate::update::UpdateStatus> {
    Json(state.update.status())
}

#[derive(Deserialize, Default)]
struct UpdateBody {
    /// When set, persist auto_update.
    auto_update: Option<bool>,
    /// When true, fetch manifest now (and apply if Auto).
    #[serde(default)]
    check: bool,
}

async fn v1_update_post(
    State(state): State<AppState>,
    Json(body): Json<UpdateBody>,
) -> Result<Json<crate::update::UpdateStatus>, (axum::http::StatusCode, String)> {
    if let Some(on) = body.auto_update {
        state
            .update
            .set_auto_update(on)
            .map_err(|e| (axum::http::StatusCode::INTERNAL_SERVER_ERROR, e))?;
    }
    if body.check {
        let svc = state.update.clone();
        let status = tokio::task::spawn_blocking(move || svc.check_and_maybe_apply())
            .await
            .map_err(|e| (axum::http::StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;
        return Ok(Json(status));
    }
    Ok(Json(state.update.status()))
}

#[derive(Serialize)]
struct RebootResponse {
    ok: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    reason: Option<&'static str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    method: Option<&'static str>,
}

async fn v1_panel_reboot(State(state): State<AppState>) -> Json<RebootResponse> {
    if state.usb.link() != UsbLink::Up {
        return Json(RebootResponse {
            ok: false,
            reason: Some("usb_down"),
            method: None,
        });
    }
    state
        .usb
        .reboot_result
        .store(REBOOT_PENDING, Ordering::SeqCst);
    state.usb.reboot.store(true, Ordering::SeqCst);
    state.logs.append(format!(
        "{} INFO  usb: restart do painel pedido pela janela",
        crate::logutil::stamp()
    ));
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(8);
    loop {
        let result = state.usb.reboot_result.load(Ordering::SeqCst);
        if result == REBOOT_DROPPED {
            return Json(RebootResponse {
                ok: true,
                reason: None,
                method: Some("reboot"),
            });
        }
        if result == REBOOT_DTR {
            return Json(RebootResponse {
                ok: true,
                reason: None,
                method: Some("dtr"),
            });
        }
        if std::time::Instant::now() >= deadline {
            return Json(RebootResponse {
                ok: false,
                reason: Some("timeout"),
                method: None,
            });
        }
        tokio::time::sleep(std::time::Duration::from_millis(200)).await;
    }
}

async fn v1_panel_state() -> Json<crate::panel::PanelState> {
    Json(crate::panel::fetch_state().await)
}

async fn v1_panel_settings(
    State(state): State<AppState>,
    Json(body): Json<serde_json::Value>,
) -> Result<Json<crate::panel::PanelState>, (axum::http::StatusCode, String)> {
    let keys: Vec<String> = body
        .as_object()
        .map(|o| o.keys().cloned().collect())
        .unwrap_or_default();
    state.logs.append(format!(
        "{} INFO  panel Mac→painel action=settings keys={}",
        crate::logutil::stamp(),
        keys.join(",")
    ));
    crate::panel::patch_settings(body)
        .await
        .map(|s| {
            state.logs.append(format!(
                "{} INFO  panel Mac→painel action=settings result=ok reachable={}",
                crate::logutil::stamp(),
                s.reachable
            ));
            Json(s)
        })
        .map_err(|e| {
            state.logs.append(format!(
                "{} WARN  panel Mac→painel action=settings result=err {}",
                crate::logutil::stamp(),
                e
            ));
            (axum::http::StatusCode::BAD_GATEWAY, e)
        })
}

async fn v1_panel_widget_settings(
    State(state): State<AppState>,
    Json(body): Json<serde_json::Value>,
) -> Result<Json<crate::panel::PanelState>, (axum::http::StatusCode, String)> {
    let id = body
        .get("id")
        .and_then(|v| v.as_str())
        .unwrap_or("?")
        .to_string();
    let key = body
        .get("key")
        .and_then(|v| v.as_str())
        .unwrap_or("?")
        .to_string();
    let value = body
        .get("value")
        .map(|v| v.to_string())
        .unwrap_or_else(|| "?".into());
    state.logs.append(format!(
        "{} INFO  panel Mac→painel · {} · ajuste · {}={}",
        crate::logutil::stamp(),
        id,
        key,
        crate::logutil::clip(&value, 80)
    ));
    crate::panel::post_widget_settings(body)
        .await
        .map(|s| {
            state.logs.append(format!(
                "{} INFO  panel Mac→painel · {} · ajuste ok",
                crate::logutil::stamp(),
                id
            ));
            Json(s)
        })
        .map_err(|e| {
            state.logs.append(format!(
                "{} WARN  panel Mac→painel · {} · ajuste falhou · {}",
                crate::logutil::stamp(),
                id,
                e
            ));
            (axum::http::StatusCode::BAD_GATEWAY, e)
        })
}

async fn v1_panel_dashboard(
    State(state): State<AppState>,
    Json(body): Json<serde_json::Value>,
) -> Result<Json<crate::panel::PanelState>, (axum::http::StatusCode, String)> {
    let summary = body
        .get("widgets")
        .and_then(|w| w.as_array())
        .map(|arr| {
            arr.iter()
                .filter_map(|item| {
                    let id = item.get("id")?.as_str()?;
                    let shown = item.get("shown")?.as_bool().unwrap_or(false);
                    let short = id.rsplit('.').next().unwrap_or(id);
                    Some(format!("{}:{}", short, if shown { "on" } else { "off" }))
                })
                .collect::<Vec<_>>()
                .join(" ")
        })
        .unwrap_or_default();
    state.logs.append(format!(
        "{} INFO  panel Mac→painel action=dashboard {}",
        crate::logutil::stamp(),
        summary
    ));
    crate::panel::post_dashboard(body)
        .await
        .map(|s| {
            state.logs.append(format!(
                "{} INFO  panel Mac→painel action=dashboard result=ok",
                crate::logutil::stamp()
            ));
            Json(s)
        })
        .map_err(|e| {
            state.logs.append(format!(
                "{} WARN  panel Mac→painel action=dashboard result=err {}",
                crate::logutil::stamp(),
                e
            ));
            (axum::http::StatusCode::BAD_GATEWAY, e)
        })
}

async fn v1_panel_refresh(
    State(state): State<AppState>,
) -> Result<Json<serde_json::Value>, (axum::http::StatusCode, String)> {
    state.logs.append(format!(
        "{} INFO  panel Mac→painel action=refresh",
        crate::logutil::stamp()
    ));
    crate::panel::refresh_panel()
        .await
        .map(|_| {
            state.logs.append(format!(
                "{} INFO  panel Mac→painel action=refresh result=ok",
                crate::logutil::stamp()
            ));
            Json(serde_json::json!({ "ok": true }))
        })
        .map_err(|e| {
            state.logs.append(format!(
                "{} WARN  panel Mac→painel action=refresh result=err {}",
                crate::logutil::stamp(),
                e
            ));
            (axum::http::StatusCode::BAD_GATEWAY, e)
        })
}

async fn v1_panel_token(
    State(state): State<AppState>,
    Json(body): Json<serde_json::Value>,
) -> Result<Json<crate::panel::PanelState>, (axum::http::StatusCode, String)> {
    // Prefer Mac-side token for agent HTTPS; still mirror to panel NVS for portal/fallback.
    if let Some(token) = body
        .get("token")
        .or_else(|| body.get("value"))
        .and_then(|v| v.as_str())
    {
        let paths = tessera_caps_todoist::TodoistPaths::default_home();
        match tessera_caps_todoist::TodoistCapability::new(paths).save_token(token) {
            Ok(()) => state.logs.append(format!(
                "{} INFO  todoist · token salvo no Mac (~/.tessera/todoist/)",
                crate::logutil::stamp()
            )),
            Err(err) => state.logs.append(format!(
                "{} WARN  todoist · token Mac: {err}",
                crate::logutil::stamp()
            )),
        }
    }
    state.logs.append(format!(
        "{} INFO  panel Mac→painel · todoist · token atualizado",
        crate::logutil::stamp()
    ));
    crate::panel::post_token(body)
        .await
        .map(Json)
        .map_err(|e| (axum::http::StatusCode::BAD_GATEWAY, e))
}

async fn v1_panel_weather(
    State(state): State<AppState>,
    Json(body): Json<serde_json::Value>,
) -> Result<Json<crate::panel::PanelState>, (axum::http::StatusCode, String)> {
    let city = body
        .get("city")
        .or_else(|| body.get("name"))
        .and_then(|v| v.as_str())
        .unwrap_or("?");
    state.logs.append(format!(
        "{} INFO  panel Mac→painel · weather · local={}",
        crate::logutil::stamp(),
        city
    ));
    crate::panel::post_weather(body)
        .await
        .map(Json)
        .map_err(|e| (axum::http::StatusCode::BAD_GATEWAY, e))
}

async fn v1_panel_calendar_selection(
    State(state): State<AppState>,
    Json(body): Json<serde_json::Value>,
) -> Result<Json<crate::panel::PanelState>, (axum::http::StatusCode, String)> {
    let sel = body.get("selection").and_then(|v| v.as_str()).unwrap_or("");
    state.logs.append(format!(
        "{} INFO  panel Mac→painel · calendar · calendários={}",
        crate::logutil::stamp(),
        crate::logutil::clip(sel, 120)
    ));
    crate::panel::post_calendar_selection(body)
        .await
        .map(Json)
        .map_err(|e| (axum::http::StatusCode::BAD_GATEWAY, e))
}

async fn v1_panel_calendar_add(
    State(state): State<AppState>,
) -> Result<Json<crate::panel::PanelState>, (axum::http::StatusCode, String)> {
    state.logs.append(format!(
        "{} INFO  panel Mac→painel · calendar · adicionar conta",
        crate::logutil::stamp()
    ));
    crate::panel::post_calendar_add_account()
        .await
        .map(Json)
        .map_err(|e| (axum::http::StatusCode::BAD_GATEWAY, e))
}

pub fn ipc_port() -> u16 {
    std::env::var("TESSERA_AGENTD_IPC_PORT")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(DEFAULT_IPC_PORT)
}

pub fn localhost_addr(port: u16) -> SocketAddr {
    SocketAddr::from(([127, 0, 0, 1], port))
}

pub async fn run_ipc_server(state: AppState) -> std::io::Result<()> {
    let port = ipc_port();
    let addr = localhost_addr(port);
    let listener = TcpListener::bind(addr).await?;
    eprintln!("Tessera: IPC http://127.0.0.1:{port}/v1/status");
    axum::serve(listener, router(state)).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use axum::body::Body;
    use axum::http::{Request, StatusCode};
    use tower::ServiceExt;

    use super::*;

    fn test_state(dir: &tempfile::TempDir) -> AppState {
        let paths = ProfilePaths::from_tessera_root(dir.path());
        let logs =
            Arc::new(tessera_logbuf::LogBuf::with_file(paths.agentd_log(), 100).expect("test log"));
        AppState::new(paths, logs)
    }

    #[tokio::test]
    async fn v1_status_json_has_version() {
        let dir = tempfile::tempdir().unwrap();
        let app = router(test_state(&dir));
        let response = app
            .oneshot(
                Request::builder()
                    .uri("/v1/status")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let json: serde_json::Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(json["ok"], true);
        assert_eq!(json["version"], env!("CARGO_PKG_VERSION"));
        assert_eq!(json["usb"], "disconnected");
        assert_eq!(json["port"], "");
        assert!(json["last_usb_age_s"].is_null());
        assert_eq!(json["setup_completed"], false);
        assert!(json["machine"]["id"].is_string());
    }

    #[tokio::test]
    async fn v1_health_ok() {
        let dir = tempfile::tempdir().unwrap();
        let app = router(test_state(&dir));
        let response = app
            .oneshot(
                Request::builder()
                    .uri("/v1/health")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let json: serde_json::Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(json["ok"], true);
    }

    #[tokio::test]
    async fn cors_allows_tauri_origin() {
        let dir = tempfile::tempdir().unwrap();
        let app = router(test_state(&dir));
        let response = app
            .oneshot(
                Request::builder()
                    .uri("/v1/status")
                    .header("origin", "https://tauri.localhost")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let allow = response
            .headers()
            .get("access-control-allow-origin")
            .and_then(|v| v.to_str().ok());
        assert_eq!(allow, Some("*"));
    }

    #[tokio::test]
    async fn post_scan_persists_scan_json() {
        let dir = tempfile::tempdir().unwrap();
        let app = router(test_state(&dir));
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/scan")
                    .header("content-type", "application/json")
                    .body(Body::from("{}"))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let scan_path = ProfilePaths::from_tessera_root(dir.path()).scan_json();
        assert!(scan_path.is_file());
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let json: serde_json::Value = serde_json::from_slice(&body).unwrap();
        assert!(json["found"].is_array());
    }

    #[tokio::test]
    async fn setup_completed_skips_default_false_until_set() {
        let dir = tempfile::tempdir().unwrap();
        let paths = ProfilePaths::from_tessera_root(dir.path());
        let app = router(test_state(&dir));
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/scan")
                    .header("content-type", "application/json")
                    .body(Body::from(r#"{"setup_completed":true}"#))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let cfg = tessera_profile::read_config(&paths.config_toml()).unwrap();
        assert!(cfg.setup_completed);
    }

    #[tokio::test]
    async fn v1_doctor_returns_checks() {
        let app = router(AppState::default());
        let response = app
            .oneshot(
                Request::builder()
                    .uri("/v1/doctor")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let json: serde_json::Value = serde_json::from_slice(&body).unwrap();
        assert!(json["summary"].is_string());
        assert!(json["checks"].as_array().is_some_and(|a| !a.is_empty()));
    }

    #[tokio::test]
    async fn v1_logs_tail() {
        let dir = tempfile::tempdir().unwrap();
        let state = test_state(&dir);
        state.logs.append("test line alpha");
        let app = router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .uri("/v1/logs?tail=10")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let json: serde_json::Value = serde_json::from_slice(&body).unwrap();
        let lines = json["lines"].as_array().unwrap();
        assert!(lines.iter().any(|l| l.as_str() == Some("test line alpha")));
    }

    #[tokio::test]
    async fn ipc_binds_localhost() {
        let listener = TcpListener::bind(localhost_addr(0)).await.unwrap();
        let addr = listener.local_addr().unwrap();
        assert!(addr.ip().is_loopback());
    }
}

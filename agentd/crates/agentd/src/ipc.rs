//! Localhost HTTP IPC for companion UI (127.0.0.1 only).

use std::net::SocketAddr;
use std::sync::{Arc, RwLock};

use axum::extract::State;
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::{Deserialize, Serialize};
use tessara_profile::{
    load_or_init_with_paths, read_scan_json, write_config, write_machine_display_name,
    write_scan_json, AgentdConfig, ProfilePaths,
};
use tessara_scan::{run_scan, ScanConfigEnabled, ScanItem, ScanResult, ScanRoots};
use tokio::net::TcpListener;

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

#[derive(Clone)]
pub struct AppState {
    pub usb: Arc<RwLock<UsbLink>>,
    pub profile_paths: ProfilePaths,
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            usb: Arc::new(RwLock::new(UsbLink::Disconnected)),
            profile_paths: ProfilePaths::default_home(),
        }
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

fn config_from_scan(found: &[ScanItem], overrides: Option<&[ScanItemApply]>) -> AgentdConfig {
    if let Some(ov) = overrides {
        let enabled = |id: &str| {
            ov.iter()
                .find(|p| p.id == id)
                .is_some_and(|p| p.enabled)
        };
        return AgentdConfig {
            media: enabled("media"),
            focus: enabled("focus"),
            calendar: enabled("calendar"),
            aitokens: enabled("cursor") || enabled("claude") || enabled("codex"),
            setup_completed: false,
        };
    }
    let toggles = ScanConfigEnabled::toggles_from_scan(found);
    AgentdConfig {
        media: toggles.media,
        focus: toggles.focus,
        aitokens: toggles.aitokens,
        calendar: toggles.calendar,
        setup_completed: false,
    }
}

fn persist_scan(
    paths: &ProfilePaths,
    result: &ScanResult,
    body: ScanApplyBody,
) -> Result<(AgentdConfig, MachineStatus), String> {
    let value = serde_json::to_value(result).map_err(|e| e.to_string())?;
    write_scan_json(&paths.scan_json(), &value).map_err(|e| e.to_string())?;

    let (mut config, machine) = profile_snapshot(paths)?;
    let auto = config_from_scan(&result.found, body.found.as_deref());
    config.media = auto.media;
    config.focus = auto.focus;
    config.aitokens = auto.aitokens;
    config.calendar = auto.calendar;
    let setup_was = config.setup_completed;
    config.setup_completed = body.setup_completed.unwrap_or(setup_was);
    write_config(&paths.config_toml(), &config).map_err(|e| e.to_string())?;

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

async fn v1_status(State(state): State<AppState>) -> Result<Json<StatusResponse>, (axum::http::StatusCode, String)> {
    let usb = *state.usb.read().expect("usb lock");
    let (config, machine) = profile_snapshot(&state.profile_paths).map_err(internal)?;
    Ok(Json(StatusResponse {
        ok: true,
        version: env!("CARGO_PKG_VERSION"),
        usb: usb.as_str(),
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
    let last = read_scan_json(&state.profile_paths.scan_json()).map_err(|e| internal(e.to_string()))?;
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
    let result = run_scan(&ScanRoots::default_home(), enabled);
    let apply = body.map(|Json(b)| b).unwrap_or_default();
    let (_, _) = persist_scan(&state.profile_paths, &result, apply).map_err(internal)?;
    Ok(Json(ScanResponse { result }))
}

async fn v1_machines(
    State(state): State<AppState>,
) -> Result<Json<MachinesResponse>, (axum::http::StatusCode, String)> {
    let (_, machine) = profile_snapshot(&state.profile_paths).map_err(internal)?;
    Ok(Json(MachinesResponse {
        machines: vec![MachineEntry {
            machine_id: machine.id,
            display_name: machine.display_name,
            this_machine: true,
        }],
    }))
}

fn internal(err: String) -> (axum::http::StatusCode, String) {
    (axum::http::StatusCode::INTERNAL_SERVER_ERROR, err)
}

pub fn router(state: AppState) -> Router {
    Router::new()
        .route("/v1/status", get(v1_status))
        .route("/v1/health", get(v1_health))
        .route("/v1/scan", post(v1_scan_post))
        .route("/v1/scan/last", get(v1_scan_last))
        .route("/v1/machines", get(v1_machines))
        .with_state(state)
}

pub fn ipc_port() -> u16 {
    std::env::var("TESSARA_AGENTD_IPC_PORT")
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
    eprintln!("agentd: IPC http://127.0.0.1:{port}/v1/status");
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
        AppState {
            usb: Arc::new(RwLock::new(UsbLink::Disconnected)),
            profile_paths: ProfilePaths::from_tessera_root(dir.path()),
        }
    }

    #[tokio::test]
    async fn v1_status_json_has_version() {
        let app = router(AppState::default());
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
        assert_eq!(json["setup_completed"], false);
        assert!(json["machine"]["id"].is_string());
    }

    #[tokio::test]
    async fn v1_health_ok() {
        let app = router(AppState::default());
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
        let cfg = tessara_profile::read_config(&paths.config_toml()).unwrap();
        assert!(cfg.setup_completed);
    }

    #[tokio::test]
    async fn ipc_binds_localhost() {
        let listener = TcpListener::bind(localhost_addr(0)).await.unwrap();
        let addr = listener.local_addr().unwrap();
        assert!(addr.ip().is_loopback());
    }
}

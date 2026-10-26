//! Localhost HTTP IPC for companion UI (127.0.0.1 only).

use std::net::SocketAddr;
use std::sync::{Arc, RwLock};

use axum::extract::State;
use axum::routing::get;
use axum::{Json, Router};
use serde::Serialize;
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
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            usb: Arc::new(RwLock::new(UsbLink::Disconnected)),
        }
    }
}

#[derive(Serialize)]
struct StatusResponse {
    ok: bool,
    version: &'static str,
    usb: &'static str,
}

#[derive(Serialize)]
struct HealthResponse {
    ok: bool,
}

async fn v1_status(State(state): State<AppState>) -> Json<StatusResponse> {
    let usb = *state.usb.read().expect("usb lock");
    Json(StatusResponse {
        ok: true,
        version: env!("CARGO_PKG_VERSION"),
        usb: usb.as_str(),
    })
}

async fn v1_health() -> Json<HealthResponse> {
    Json(HealthResponse { ok: true })
}

pub fn router(state: AppState) -> Router {
    Router::new()
        .route("/v1/status", get(v1_status))
        .route("/v1/health", get(v1_health))
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
    async fn ipc_binds_localhost() {
        let listener = TcpListener::bind(localhost_addr(0)).await.unwrap();
        let addr = listener.local_addr().unwrap();
        assert!(addr.ip().is_loopback());
    }
}

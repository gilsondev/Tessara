mod service;

/// Local agentd IPC origin (must match `connect-src` in `tauri.conf.json`).
pub const AGENTD_IPC_ORIGIN: &str = "http://127.0.0.1:19792";

use std::sync::atomic::{AtomicBool, Ordering};

use tauri::{AppHandle, Emitter, Manager, RunEvent};
use tauri_plugin_opener::OpenerExt;

static TRAY_READY: AtomicBool = AtomicBool::new(false);

fn show_main_window(app: &AppHandle) {
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.show();
        let _ = win.unminimize();
        let _ = win.set_focus();
    }
}

fn emit_navigate(app: &AppHandle, screen_id: &str) {
    show_main_window(app);
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.emit("navigate", screen_id);
    }
}

/// Open a macOS Privacy / Login Items deep link. Optionally reveal `reveal_path` in Finder
/// (FDA: user can drag Tessera onto the list if it is missing).
#[tauri::command]
fn open_privacy_settings(
    app: AppHandle,
    url: String,
    reveal_path: Option<String>,
) -> Result<(), String> {
    app.opener()
        .open_url(&url, None::<&str>)
        .map_err(|e| format!("abrir Ajustes: {e}"))?;

    if let Some(path) = reveal_path {
        if !path.is_empty() && std::path::Path::new(&path).exists() {
            let _ = std::process::Command::new("open")
                .args(["-R", &path])
                .status();
        }
    }
    Ok(())
}

fn install_tray(app: &AppHandle) -> tauri::Result<()> {
    use tauri::menu::MenuBuilder;
    use tauri::tray::TrayIconBuilder;

    if TRAY_READY.swap(true, Ordering::SeqCst) {
        return Ok(());
    }

    let tray_menu = MenuBuilder::new(app)
        .text("open", "Abrir Tessera")
        .text("panel", "Ajustar o painel")
        .text("mac", "O que vai ao painel")
        .separator()
        .text("doctor", "Doctor")
        .text("scan", "Varrer este Mac")
        .text("machines", "Meus computadores")
        .text("activity", "Atividade / log")
        .separator()
        .text("portal", "Abrir tessera.local")
        .separator()
        .text("quit", "Sair")
        .build()?;

    // Menu bar: white Tessera mark as template (Light/Dark). Keep title as
    // Tahoe fallback when NSStatusItem lands off-screen (see tray-icon#273).
    let icon = tauri::include_image!("icons/trayTemplate.png").clone();

    // Keep the TrayIcon alive — dropping the last handle removes it from the menu bar.
    let tray = TrayIconBuilder::with_id("tessera-main")
        .icon(icon)
        .icon_as_template(true)
        .title("Tessera")
        .menu(&tray_menu)
        .show_menu_on_left_click(true)
        .tooltip("Tessera")
        .build(app)?;
    let _ = tray.set_visible(true);
    let _ = tray.set_title(Some("Tessera"));
    app.manage(tray);
    eprintln!("Tessera UI: tray instalado na menu bar (mark template)");
    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let app = tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![
            open_privacy_settings,
            service::ipc_token,
            service::agentd_service,
            service::ensure_agentd,
            service::panel_force_reset
        ])
        .setup(|app| {
            #[cfg(desktop)]
            {
                use tauri::LogicalSize;

                // macOS may restore a prior phone-sized frame; pin a desktop default.
                if let Some(win) = app.get_webview_window("main") {
                    let _ = win.set_size(LogicalSize::new(980.0, 720.0));
                    let _ = win.set_min_size(Some(LogicalSize::new(800.0, 560.0)));
                    let _ = win.center();

                    // Close/hide window keeps the tray; only "Sair" quits.
                    let handle = app.handle().clone();
                    win.on_window_event(move |event| {
                        if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                            api.prevent_close();
                            if let Some(w) = handle.get_webview_window("main") {
                                let _ = w.hide();
                            }
                        }
                    });
                }
            }
            Ok(())
        })
        .on_menu_event(|app, event| {
            match event.id().as_ref() {
                "open" => show_main_window(app),
                "panel" => emit_navigate(app, "panel"),
                "mac" => emit_navigate(app, "mac"),
                "machines" => emit_navigate(app, "machines"),
                "doctor" => emit_navigate(app, "doctor"),
                "scan" => emit_navigate(app, "scan"),
                "activity" => emit_navigate(app, "activity"),
                "portal" => {
                    let _ = app.opener().open_url("http://tessera.local", None::<&str>);
                }
                "quit" => {
                    app.exit(0);
                }
                _ => {}
            }
        })
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    app.run(|app_handle, event| {
        // tray-icon on macOS needs the event loop already running (Ready), not setup().
        if let RunEvent::Ready = event {
            if let Err(err) = install_tray(app_handle) {
                eprintln!("Tessera UI: falha ao instalar tray: {err}");
            }
        }
    });
}

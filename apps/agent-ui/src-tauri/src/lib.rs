/// Local agentd IPC origin (must match `connect-src` in `tauri.conf.json`).
pub const AGENTD_IPC_ORIGIN: &str = "http://127.0.0.1:19792";

use tauri::{AppHandle, Emitter, Manager};

// Learn more about Tauri commands at https://tauri.app/develop/calling-rust/
#[tauri::command]
fn greet(name: &str) -> String {
    format!("Hello, {}! You've been greeted from Rust!", name)
}

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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![greet])
        .setup(|app| {
            #[cfg(desktop)]
            {
                use tauri::menu::MenuBuilder;
                use tauri::tray::TrayIconBuilder;

                let tray_menu = MenuBuilder::new(app.handle())
                    .text("open", "Abrir")
                    .text("machines", "Meus computadores")
                    .text("doctor", "Doctor")
                    .text("scan", "Nova varredura")
                    .separator()
                    .text("quit", "Sair")
                    .build()?;

                let icon = tauri::include_image!("icons/32x32.png");

                let _tray = TrayIconBuilder::new()
                    .icon(icon)
                    .menu(&tray_menu)
                    .tooltip("Tessara")
                    .build(app)?;
            }
            Ok(())
        })
        .on_menu_event(|app, event| {
            match event.id().as_ref() {
                "open" => show_main_window(app),
                "machines" => emit_navigate(app, "machines"),
                "doctor" => emit_navigate(app, "doctor"),
                "scan" => emit_navigate(app, "scan"),
                "quit" => {
                    app.exit(0);
                }
                _ => {}
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

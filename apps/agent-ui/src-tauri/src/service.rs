//! launchd controls for the Tessera agent. These run in the menu-bar app so a
//! wedged agentd can still be stopped. The agent process is never asked to kill itself.

use std::os::unix::fs::{symlink, PermissionsExt};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Duration;

use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};

const LABEL: &str = "com.tessera.agentd";
const MANIFEST_URL: &str = "https://gilsondev.github.io/Tessera/latest.json";
const AGENT_VERSION: &str = include_str!("../../../../VERSION");

#[derive(Serialize)]
pub struct ServiceResult {
    pub ok: bool,
    pub message: String,
}

fn fail(message: impl Into<String>) -> ServiceResult {
    ServiceResult {
        ok: false,
        message: message.into(),
    }
}

fn ok(message: impl Into<String>) -> ServiceResult {
    ServiceResult {
        ok: true,
        message: message.into(),
    }
}

fn uid() -> Result<u32, String> {
    let out = Command::new("id")
        .arg("-u")
        .output()
        .map_err(|e| format!("id -u: {e}"))?;
    String::from_utf8_lossy(&out.stdout)
        .trim()
        .parse()
        .map_err(|_| "não consegui ler o uid deste usuário".to_string())
}

fn domain() -> Result<String, String> {
    Ok(format!("gui/{}", uid()?))
}

fn target() -> Result<String, String> {
    Ok(format!("{}/{LABEL}", domain()?))
}

fn plist_path() -> Result<PathBuf, String> {
    let home = std::env::var("HOME").map_err(|_| "HOME não está definido".to_string())?;
    Ok(PathBuf::from(home)
        .join("Library/LaunchAgents")
        .join(format!("{LABEL}.plist")))
}

fn output_text(out: &std::process::Output) -> String {
    let mut text = String::from_utf8_lossy(&out.stderr).trim().to_string();
    if text.is_empty() {
        text = String::from_utf8_lossy(&out.stdout).trim().to_string();
    }
    text
}

fn launchctl(args: &[&str]) -> Result<std::process::Output, String> {
    Command::new("launchctl")
        .args(args)
        .output()
        .map_err(|e| format!("launchctl: {e}"))
}

fn job_text() -> Result<Option<String>, String> {
    let target = target()?;
    let out = launchctl(&["print", &target])?;
    if !out.status.success() {
        return Ok(None);
    }
    Ok(Some(String::from_utf8_lossy(&out.stdout).to_string()))
}

fn status() -> ServiceResult {
    match job_text() {
        Ok(Some(text)) if text.contains("state = running") => {
            let pid = text.lines().find_map(|line| {
                line.trim()
                    .strip_prefix("pid = ")
                    .and_then(|p| p.parse::<u32>().ok())
            });
            match pid {
                Some(pid) => ok(format!("No ar · pid {pid}")),
                None => ok("No ar"),
            }
        }
        Ok(_) => fail("Parado"),
        Err(e) => fail(e),
    }
}

fn bootstrap() -> Result<(), String> {
    let domain = domain()?;
    let plist = plist_path()?;
    if !plist.exists() {
        return Err(
            "Não achei o LaunchAgent do Tessera. Instale o Agent de novo.".to_string(),
        );
    }
    let path = plist.to_string_lossy().to_string();
    let out = launchctl(&["bootstrap", &domain, &path])?;
    if out.status.success() {
        return Ok(());
    }
    let err = output_text(&out);
    if err.contains("already") || err.contains("Service is already loaded") {
        return Ok(());
    }
    Err(if err.is_empty() {
        "launchctl bootstrap falhou".to_string()
    } else {
        err
    })
}

fn restart() -> ServiceResult {
    let target = match target() {
        Ok(t) => t,
        Err(e) => return fail(e),
    };
    if let Err(e) = bootstrap() {
        return fail(e);
    }
    match launchctl(&["kickstart", "-k", &target]) {
        Ok(out) if out.status.success() => ok("Agent reiniciado."),
        Ok(out) => fail(if output_text(&out).is_empty() {
            "Não consegui reiniciar o Agent.".to_string()
        } else {
            output_text(&out)
        }),
        Err(e) => fail(e),
    }
}

fn force_kill() -> ServiceResult {
    let target = match target() {
        Ok(t) => t,
        Err(e) => return fail(e),
    };
    match launchctl(&["kill", "SIGKILL", &target]) {
        Ok(out) if out.status.success() => {
            ok("Processo encerrado. O macOS sobe de novo sozinho.")
        }
        Ok(out) => {
            let err = output_text(&out);
            if err.contains("No such process") || err.contains("not found") {
                fail("O Agent já estava parado.")
            } else if err.is_empty() {
                fail("Não consegui encerrar o Agent.")
            } else {
                fail(err)
            }
        }
        Err(e) => fail(e),
    }
}

fn stop() -> ServiceResult {
    let target = match target() {
        Ok(t) => t,
        Err(e) => return fail(e),
    };
    match launchctl(&["bootout", &target]) {
        Ok(out) if out.status.success() => {
            ok("Agent parado. Ele fica desligado até você tocar em Iniciar.")
        }
        Ok(out) => {
            let err = output_text(&out);
            if err.contains("No such process") || err.contains("not found") {
                ok("Agent já estava parado.")
            } else if err.is_empty() {
                fail("Não consegui parar o Agent.")
            } else {
                fail(err)
            }
        }
        Err(e) => fail(e),
    }
}

fn start() -> ServiceResult {
    match bootstrap() {
        Ok(()) => {
            let target = match target() {
                Ok(t) => t,
                Err(e) => return fail(e),
            };
            let _ = launchctl(&["kickstart", &target]);
            ok("Agent iniciado.")
        }
        Err(e) => fail(e),
    }
}

fn program_from_plist() -> Option<PathBuf> {
    let path = plist_path().ok()?;
    let text = std::fs::read_to_string(path).ok()?;
    let key = text.find("<key>ProgramArguments</key>")?;
    let rest = &text[key..];
    let start = rest.find("<string>")? + "<string>".len();
    let end = rest[start..].find("</string>")? + start;
    let bin = rest[start..end].trim();
    if bin.is_empty() {
        None
    } else {
        Some(PathBuf::from(bin))
    }
}

fn agent_binary() -> Result<PathBuf, String> {
    if let Some(path) = program_from_plist() {
        if path.exists() {
            return Ok(path);
        }
    }
    let home = std::env::var("HOME").map_err(|_| "HOME não está definido".to_string())?;
    let candidates = [
        PathBuf::from(&home).join(".tessera/agentd/bin/Tessera"),
        PathBuf::from(&home).join(".tessera/agentd/bin/agentd"),
        PathBuf::from(&home).join(".tessera/agentd/Tessera.app/Contents/MacOS/Tessera"),
    ];
    candidates
        .into_iter()
        .find(|p| p.exists())
        .ok_or_else(|| "Não achei o binário do Agent para pulsar o cabo.".to_string())
}

fn panel_port() -> Option<String> {
    let dir = std::fs::read_dir("/dev").ok()?;
    let mut names: Vec<String> = dir
        .filter_map(|e| e.ok())
        .map(|e| e.file_name().to_string_lossy().to_string())
        .filter(|name| name.starts_with("cu.usbmodem") && !name.ends_with("1401"))
        .collect();
    names.sort();
    names.into_iter().next()
}

fn force_reset() -> ServiceResult {
    if let Ok(target) = target() {
        let _ = launchctl(&["bootout", &target]);
    }
    std::thread::sleep(Duration::from_millis(400));
    let Some(port) = panel_port() else {
        let started = start();
        return fail(format!(
            "Não achei o cabo USB. Ligue o painel e tente de novo. {}",
            started.message
        ));
    };
    let bin = match agent_binary() {
        Ok(bin) => bin,
        Err(e) => {
            let started = start();
            return fail(format!("{e} {}", started.message));
        }
    };
    let pulse = Command::new(&bin)
        .args(["--pulse-dtr", "--port", &port])
        .output();
    let started = start();
    match pulse {
        Ok(out) if out.status.success() => ok(format!(
            "Reset pelo cabo em {port}. {}",
            started.message
        )),
        Ok(out) => {
            let err = output_text(&out);
            fail(format!(
                "O cabo não aceitou o reset{}. {}",
                if err.is_empty() {
                    String::new()
                } else {
                    format!(" ({err})")
                },
                started.message
            ))
        }
        Err(e) => fail(format!("Não consegui pulsar o cabo: {e}. {}", started.message)),
    }
}

#[derive(Serialize, Clone)]
struct InstallEvent {
    step: &'static str,
    message: String,
}

fn emit_install(app: &AppHandle, step: &'static str, message: impl Into<String>) {
    let _ = app.emit(
        "agentd-install",
        InstallEvent {
            step,
            message: message.into(),
        },
    );
}

fn ipc_up() -> bool {
    let out = Command::new("/usr/bin/curl")
        .args([
            "-fsS",
            "--max-time",
            "1",
            "http://127.0.0.1:19792/v1/status",
        ])
        .output();
    match out {
        Ok(out) if out.status.success() => {
            String::from_utf8_lossy(&out.stdout).contains("\"ok\":true")
        }
        _ => false,
    }
}

fn home_dir() -> Result<PathBuf, String> {
    std::env::var("HOME")
        .map(PathBuf::from)
        .map_err(|_| "HOME não está definido".into())
}

fn installed_agent_bin() -> Result<PathBuf, String> {
    Ok(home_dir()?.join(".tessera/agentd/bin/Tessera"))
}

fn bundled_agent_bin(app: &AppHandle) -> Option<PathBuf> {
    let mut cands = Vec::new();
    if let Ok(dir) = app.path().resource_dir() {
        cands.push(dir.join("tessera-agentd"));
        cands.push(dir.join("agentd"));
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(res) = exe.parent().and_then(|p| p.parent()) {
            cands.push(res.join("Resources/tessera-agentd"));
        }
    }
    cands.into_iter().find(|p| p.is_file())
}

fn trusted_download(url: &str) -> bool {
    url.starts_with("https://github.com/gilsondev/Tessera/releases/download/")
        || url.starts_with("https://github.com/gilsondev/Tessara/releases/download/")
}

fn run_ok(cmd: &mut Command) -> Result<(), String> {
    let out = cmd.output().map_err(|e| e.to_string())?;
    if out.status.success() {
        return Ok(());
    }
    let text = output_text(&out);
    Err(if text.is_empty() {
        format!("comando falhou ({})", out.status)
    } else {
        text
    })
}

fn place_agent_binary(src: &Path) -> Result<PathBuf, String> {
    let dest = installed_agent_bin()?;
    if let Some(dir) = dest.parent() {
        std::fs::create_dir_all(dir).map_err(|e| format!("criar pasta do agente: {e}"))?;
    }
    if src != dest {
        std::fs::copy(src, &dest).map_err(|e| format!("copiar agente: {e}"))?;
    }
    let mut perms = std::fs::metadata(&dest)
        .map_err(|e| format!("ler permissões: {e}"))?
        .permissions();
    perms.set_mode(0o755);
    std::fs::set_permissions(&dest, perms).map_err(|e| format!("chmod: {e}"))?;
    if let Some(dir) = dest.parent() {
        let link = dir.join("agentd");
        let _ = std::fs::remove_file(&link);
        symlink("Tessera", &link).map_err(|e| format!("atalho agentd: {e}"))?;
    }
    wrap_agent_app(&dest)
}

fn agent_plist() -> String {
    let version = AGENT_VERSION.trim();
    format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>Tessera</string>
  <key>CFBundleIdentifier</key><string>dev.tessera.agentd</string>
  <key>CFBundleName</key><string>Tessera Agent</string>
  <key>CFBundleDisplayName</key><string>Tessera Agent</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>{version}</string>
  <key>LSUIElement</key><true/>
</dict></plist>
"#
    )
}

/// Stable TCC identity. Launchd runs this bundle, not the menu-bar app.
fn wrap_agent_app(bin: &Path) -> Result<PathBuf, String> {
    let home = home_dir()?;
    let app = home.join(".tessera/agentd/Tessera Agent.app");
    let exe = app.join("Contents/MacOS/Tessera");
    if let Some(dir) = exe.parent() {
        std::fs::create_dir_all(dir).map_err(|e| format!("criar Tessera Agent.app: {e}"))?;
    }
    std::fs::copy(bin, &exe).map_err(|e| format!("copiar para o app do agente: {e}"))?;
    let mut perms = std::fs::metadata(&exe)
        .map_err(|e| e.to_string())?
        .permissions();
    perms.set_mode(0o755);
    std::fs::set_permissions(&exe, perms).map_err(|e| e.to_string())?;
    std::fs::write(app.join("Contents/Info.plist"), agent_plist())
        .map_err(|e| format!("Info.plist: {e}"))?;
    let _ = Command::new("/usr/bin/codesign")
        .args(["--force", "--sign", "-", "--"])
        .arg(&app)
        .status();
    Ok(exe)
}

fn write_plist(bin: &Path) -> Result<(), String> {
    let home = home_dir()?;
    let plist = plist_path()?;
    if let Some(dir) = plist.parent() {
        std::fs::create_dir_all(dir).map_err(|e| format!("LaunchAgents: {e}"))?;
    }
    let install_root = home.join(".tessera/agentd");
    let log = home.join(".tessera/agentd.log");
    std::fs::create_dir_all(&install_root).map_err(|e| format!("pasta do agente: {e}"))?;
    let body = format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>{label}</string>
  <key>ProgramArguments</key>
  <array>
    <string>{bin}</string>
  </array>
  <key>WorkingDirectory</key><string>{workdir}</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>5</integer>
  <key>StandardOutPath</key><string>{log}</string>
  <key>StandardErrorPath</key><string>{log}</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string>
    <key>HOME</key><string>{home}</string>
  </dict>
</dict>
</plist>
"#,
        label = LABEL,
        bin = bin.display(),
        workdir = install_root.display(),
        log = log.display(),
        home = home.display(),
    );
    std::fs::write(&plist, body).map_err(|e| format!("gravar plist: {e}"))?;
    Ok(())
}

fn download_agent_binary() -> Result<PathBuf, String> {
    let tmp = std::env::temp_dir().join(format!("tessera-agent-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&tmp);
    std::fs::create_dir_all(&tmp).map_err(|e| format!("pasta temporária: {e}"))?;
    let manifest = tmp.join("latest.json");
    run_ok(
        Command::new("/usr/bin/curl")
            .args(["-fsSL", MANIFEST_URL, "-o"])
            .arg(&manifest),
    )
    .map_err(|e| format!("não baixei a lista de versões: {e}"))?;
    let raw = std::fs::read_to_string(&manifest).map_err(|e| e.to_string())?;
    let doc: serde_json::Value =
        serde_json::from_str(&raw).map_err(|e| format!("lista de versões inválida: {e}"))?;
    let url = doc
        .get("agentd_url")
        .and_then(|v| v.as_str())
        .ok_or("latest.json sem agentd_url")?;
    if !trusted_download(url) {
        return Err("o endereço do agente não é o release do Tessera".into());
    }
    let expect = doc
        .pointer("/sha256/agentd")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    let tgz = tmp.join("agentd.tgz");
    run_ok(
        Command::new("/usr/bin/curl")
            .args(["-fsSL", url, "-o"])
            .arg(&tgz),
    )
    .map_err(|e| format!("não baixei o agente: {e}"))?;
    if !expect.is_empty() {
        let out = Command::new("/usr/bin/shasum")
            .args(["-a", "256"])
            .arg(&tgz)
            .output()
            .map_err(|e| format!("shasum: {e}"))?;
        let got = String::from_utf8_lossy(&out.stdout);
        let got = got.split_whitespace().next().unwrap_or("");
        if !out.status.success() || !got.eq_ignore_ascii_case(&expect) {
            return Err("o arquivo baixado não confere com o checksum".into());
        }
    }
    let stage = tmp.join("stage");
    std::fs::create_dir_all(&stage).map_err(|e| format!("pasta do pacote: {e}"))?;
    run_ok(
        Command::new("/usr/bin/tar")
            .args(["-xzf"])
            .arg(&tgz)
            .arg("-C")
            .arg(&stage),
    )
    .map_err(|e| format!("abrir pacote: {e}"))?;
    let src = ["bin/Tessera", "bin/agentd"]
        .into_iter()
        .map(|rel| stage.join(rel))
        .find(|p| p.is_file())
        .ok_or_else(|| "o pacote não tem bin/Tessera".to_string())?;
    let dest = place_agent_binary(&src)?;
    let _ = std::fs::remove_dir_all(&tmp);
    Ok(dest)
}

fn ensure_binary(app: &AppHandle) -> Result<PathBuf, String> {
    let dest = installed_agent_bin()?;
    if dest.is_file() {
        emit_install(app, "fetch", "O agente já está neste Mac.");
        return wrap_agent_app(&dest);
    }
    if let Some(bundled) = bundled_agent_bin(app) {
        emit_install(app, "fetch", "Copiando o agente que veio com o app…");
        return place_agent_binary(&bundled);
    }
    emit_install(app, "fetch", "Baixando o agente do site do Tessera…");
    download_agent_binary()
}

fn launch_installed(bin: &Path) -> Result<(), String> {
    write_plist(bin)?;
    if let Ok(target) = target() {
        let _ = launchctl(&["bootout", &target]);
    }
    bootstrap()?;
    if let Ok(target) = target() {
        let _ = launchctl(&["kickstart", &target]);
    }
    Ok(())
}

fn wait_for_ipc() -> bool {
    for _ in 0..24 {
        if ipc_up() {
            return true;
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    false
}

/// Install the USB agent if this Mac only has the menu-bar app, then start it.
#[tauri::command]
pub fn ensure_agentd(app: AppHandle) -> ServiceResult {
    emit_install(&app, "check", "Conferindo se o agente já responde…");
    if ipc_up() {
        emit_install(&app, "ready", "O agente já está no ar.");
        return ok("O agente já está no ar.");
    }
    let bin = match ensure_binary(&app) {
        Ok(bin) => bin,
        Err(e) => {
            emit_install(&app, "error", e.clone());
            return fail(e);
        }
    };
    emit_install(&app, "register", "Registrando para abrir com o Mac…");
    if let Err(e) = launch_installed(&bin) {
        emit_install(&app, "error", e.clone());
        return fail(e);
    }
    emit_install(&app, "start", "Esperando o agente responder…");
    if wait_for_ipc() {
        emit_install(&app, "ready", "Agente no ar.");
        return ok("Agente no ar.");
    }
    let msg = "O agente foi instalado, mas não respondeu. Veja ~/.tessera/agentd.log".to_string();
    emit_install(&app, "error", msg.clone());
    fail(msg)
}

#[tauri::command]
pub fn agentd_service(action: String) -> ServiceResult {
    match action.as_str() {
        "status" => status(),
        "restart" => restart(),
        "kill" => force_kill(),
        "stop" => stop(),
        "start" => start(),
        _ => fail("Ação desconhecida."),
    }
}

#[tauri::command]
pub fn panel_force_reset() -> ServiceResult {
    force_reset()
}

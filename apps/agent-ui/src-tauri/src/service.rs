//! launchd controls for the Tessera agent. These run in the menu-bar app so a
//! wedged agentd can still be stopped. The agent process is never asked to kill itself.

use std::path::PathBuf;
use std::process::Command;
use std::time::Duration;

use serde::Serialize;

const LABEL: &str = "com.tessera.agentd";

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

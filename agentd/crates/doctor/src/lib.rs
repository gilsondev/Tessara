//! Doctor engine — health checks → stable JSON for companion UI.

use std::path::Path;
use std::process::Command;

use serde::{Deserialize, Serialize};
use tessera_caps_media::MediaCapability;
use tessera_profile::AgentdConfig;
use tessera_scan::{run_scan, ScanConfigEnabled, ScanItem, ScanResult, ScanRoots, ScanState};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DoctorCheck {
    pub id: String,
    pub level: String,
    pub title: String,
    pub detail: String,
    /// Screen id for companion UI (`setup`, …).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub action: Option<String>,
    /// Privacy pane id for deep-link (`full_disk_access`, `automation`, `login_items`).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub settings_pane: Option<String>,
    /// macOS System Settings / Preferences URL for this pane.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub settings_url: Option<String>,
}

fn check(
    id: &str,
    level: &str,
    title: &str,
    detail: impl Into<String>,
    action: Option<&str>,
    pane: Option<&str>,
) -> DoctorCheck {
    DoctorCheck {
        id: id.into(),
        level: level.into(),
        title: title.into(),
        detail: detail.into(),
        action: action.map(str::to_string),
        settings_pane: pane.map(str::to_string),
        settings_url: pane.and_then(settings_url_for).map(str::to_string),
    }
}

/// Deep links that open the exact Privacy row the user must toggle.
pub fn settings_url_for(pane: &str) -> Option<&'static str> {
    match pane {
        // Ventura+ Settings; falls back gracefully on older macOS via `open`.
        "full_disk_access" => Some(
            "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_AllFiles",
        ),
        "automation" => Some(
            "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Automation",
        ),
        "login_items" => {
            Some("x-apple.systempreferences:com.apple.LoginItems-Settings.extension")
        }
        "calendar" => Some(
            "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_AllFiles",
        ),
        _ => None,
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PreparePermission {
    pub ok: bool,
    pub pane: String,
    pub settings_url: String,
    /// Path the user should see / enable in the list (LaunchAgent binary).
    pub binary_path: String,
    pub hint: String,
}

/// Path shown in Privacy / Finder. The LaunchAgent runs `Tessera Agent.app`.
/// Older installs may still have `Tessera.app` or a bare binary.
pub fn agentd_identity_path(home: &Path) -> std::path::PathBuf {
    let root = home.join(".tessera/agentd");
    for name in ["Tessera Agent.app", "Tessera.app"] {
        let app = root.join(name);
        if app.join("Contents/MacOS/Tessera").is_file() {
            return app;
        }
    }
    root.join("bin/Tessera")
}

/// Touch protected APIs so **Tessera** shows up in the Privacy list (toggle still off
/// until the user enables it). Then the UI opens `settings_url`.
pub fn prepare_permission(pane: &str, home: &Path) -> Result<PreparePermission, String> {
    let url = settings_url_for(pane)
        .ok_or_else(|| format!("pane desconhecido: {pane}"))?
        .to_string();
    let binary_path = agentd_identity_path(home).display().to_string();

    match pane {
        "full_disk_access" => {
            // Reading Focus DBs registers this process with TCC (denied → listed).
            // LaunchAgent must run Contents/MacOS/Tessera inside Tessera.app for the brand icon.
            let _ =
                std::fs::File::open(home.join("Library/DoNotDisturb/DB/ModeConfigurations.json"));
            let _ = std::fs::File::open(home.join("Library/DoNotDisturb/DB/Assertions.json"));
            let _ =
                std::fs::File::open(home.join("Library/Application Support/com.apple.TCC/TCC.db"));
        }
        "automation" => {
            // Harmless AppleEvent so Tessera appears under Automation → Shortcuts.
            let _ = Command::new("osascript")
                .args(["-e", "tell application \"System Events\" to get name"])
                .output();
        }
        "login_items" | "calendar" => {}
        _ => {}
    }

    let hint = match pane {
        "full_disk_access" => {
            "Na lista, ligue «Tessera Agent». Ele já aparece aí. O Tessera da pasta Aplicativos é só a janela."
                .into()
        }
        "automation" => {
            "Em Automação, expanda Tessera e marque Shortcuts / System Events.".into()
        }
        "login_items" => "Confirme que Tessera aparece em Itens de Abertura / Em segundo plano.".into(),
        "calendar" => {
            "A agenda sai do app Calendário. Se a checagem falhar, ligue Tessera Agent em Acesso Total ao Disco."
                .into()
        }
        _ => "Ligue a permissão para Tessera e volte ao app.".into(),
    };

    Ok(PreparePermission {
        ok: true,
        pane: pane.into(),
        settings_url: url,
        binary_path,
        hint,
    })
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DoctorReport {
    pub summary: String,
    pub checks: Vec<DoctorCheck>,
}

/// Inputs for `run_doctor` (fake values in unit tests).
#[derive(Debug, Clone)]
pub struct DoctorDeps {
    pub usb_up: bool,
    pub launch_agent_ok: bool,
    pub files_ok: bool,
    pub push_queue_depth: usize,
    pub scan: ScanResult,
    pub config: AgentdConfig,
    pub media_detail: String,
    pub media_level: &'static str,
    pub focus_detail: String,
    pub focus_level: &'static str,
}

impl DoctorDeps {
    pub fn live(
        usb_up: bool,
        push_queue_depth: usize,
        config: AgentdConfig,
        scan_roots: &ScanRoots,
    ) -> Self {
        let enabled = ScanConfigEnabled {
            media: config.media,
            focus: config.focus,
            aitokens: config.aitokens,
            calendar: config.calendar,
        };
        let scan = run_scan(scan_roots, enabled);
        let files_ok = probe_full_disk_access(scan_roots);
        let launch_agent_ok = probe_launch_agent();
        let (media_level, media_detail) = probe_media(config.media, &scan);
        let (focus_level, focus_detail) = probe_focus(config.focus, &scan, files_ok);
        Self {
            usb_up,
            launch_agent_ok,
            files_ok,
            push_queue_depth,
            scan,
            config,
            media_detail,
            media_level,
            focus_detail,
            focus_level,
        }
    }
}

pub fn run_doctor(deps: &DoctorDeps) -> DoctorReport {
    let checks = vec![
        check_usb(deps),
        check_agentd(deps),
        check_files(deps),
        check_music(deps),
        check_focus(deps),
        check_cursor(deps),
        check_claude(deps),
        check_codex(deps),
        check_push(deps),
    ];
    let summary = summarize(&checks);
    DoctorReport { summary, checks }
}

fn summarize(checks: &[DoctorCheck]) -> String {
    let bad = checks.iter().filter(|c| c.level == "bad").count();
    let warn = checks.iter().filter(|c| c.level == "warn").count();
    match (bad, warn) {
        (0, 0) => "Tudo certo".to_string(),
        (1, _) => "1 coisa precisa de você".to_string(),
        (n, _) if n > 1 => format!("{n} coisas precisam de você"),
        (_, 1) => "1 aviso — veja abaixo".to_string(),
        (_, n) => format!("{n} avisos — veja abaixo"),
    }
}

fn check_usb(deps: &DoctorDeps) -> DoctorCheck {
    if deps.usb_up {
        check(
            "usb",
            "ok",
            "Painel no cabo",
            "USB ok · handshake · painel conectado",
            None,
            None,
        )
    } else {
        check(
            "usb",
            "warn",
            "Painel no cabo",
            "Nenhum painel USB agora · conecte o cabo para sync",
            Some("setup-cable"),
            None,
        )
    }
}

fn check_agentd(deps: &DoctorDeps) -> DoctorCheck {
    if deps.launch_agent_ok {
        check(
            "agentd",
            "ok",
            "App em segundo plano",
            "Abre com o Mac e está rodando agora",
            None,
            None,
        )
    } else {
        check(
            "agentd",
            "warn",
            "App em segundo plano",
            "Tessera está rodando, mas não abre sozinho com o Mac (LaunchAgent ausente)",
            Some("setup"),
            Some("login_items"),
        )
    }
}

fn check_files(deps: &DoctorDeps) -> DoctorCheck {
    if deps.files_ok {
        check(
            "files",
            "ok",
            "Acesso aos arquivos",
            "Full Disk Access ok para paths do Tessera",
            None,
            None,
        )
    } else {
        check(
            "files",
            "bad",
            "Acesso aos arquivos",
            "Na lista de Disco completo, ligue «Tessera Agent». Ele já aparece aí.",
            Some("setup"),
            Some("full_disk_access"),
        )
    }
}

fn check_music(deps: &DoctorDeps) -> DoctorCheck {
    check(
        "music",
        deps.media_level,
        "Música",
        deps.media_detail.clone(),
        None,
        None,
    )
}

fn check_focus(deps: &DoctorDeps) -> DoctorCheck {
    let needs_fda = deps.focus_level == "warn" && !deps.files_ok;
    let needs_auto = deps.focus_level == "bad";
    let (action, pane) = if needs_fda {
        (Some("setup"), Some("full_disk_access"))
    } else if needs_auto {
        (Some("setup"), Some("automation"))
    } else {
        (None, None)
    };
    check(
        "focus",
        deps.focus_level,
        "Focus",
        deps.focus_detail.clone(),
        action,
        pane,
    )
}

fn item<'a>(scan: &'a ScanResult, id: &str) -> Option<&'a ScanItem> {
    scan.found.iter().find(|i| i.id == id)
}

fn check_cursor(deps: &DoctorDeps) -> DoctorCheck {
    ai_check(
        "cursor",
        "Cursor",
        deps.config.aitokens,
        item(&deps.scan, "cursor"),
        deps.files_ok,
    )
}

fn check_claude(deps: &DoctorDeps) -> DoctorCheck {
    ai_check(
        "claude",
        "Claude",
        deps.config.aitokens,
        item(&deps.scan, "claude"),
        deps.files_ok,
    )
}

fn check_codex(deps: &DoctorDeps) -> DoctorCheck {
    ai_check(
        "codex",
        "Codex / ChatGPT",
        deps.config.aitokens,
        item(&deps.scan, "codex"),
        deps.files_ok,
    )
}

fn ai_check(
    id: &str,
    title: &str,
    enabled: bool,
    item: Option<&ScanItem>,
    files_ok: bool,
) -> DoctorCheck {
    let Some(item) = item else {
        return check(id, "skip", title, "Varredura indisponível", None, None);
    };
    if !enabled {
        return check(
            id,
            "skip",
            title,
            "Desligado neste Mac — nada a fazer",
            None,
            None,
        );
    }
    match item.state {
        ScanState::Missing => check(
            id,
            "skip",
            title,
            "Não instalado neste Mac — nada a fazer",
            None,
            None,
        ),
        ScanState::Ready => check(id, "ok", title, "Conta logada · cotas lidas", None, None),
        ScanState::NeedsAuth => {
            let blocked_by_fda = !files_ok && (id == "claude" || id == "cursor");
            let (level, detail) = if blocked_by_fda {
                (
                    "warn",
                    "App achado · cotas bloqueadas pela autorização de arquivos",
                )
            } else {
                ("warn", "App achado · falta login ou autorização")
            };
            check(
                id,
                level,
                title,
                detail,
                if blocked_by_fda { Some("setup") } else { None },
                if blocked_by_fda {
                    Some("full_disk_access")
                } else {
                    None
                },
            )
        }
    }
}

fn check_push(deps: &DoctorDeps) -> DoctorCheck {
    if !deps.usb_up {
        check(
            "push",
            "warn",
            "Envio ao painel",
            "Sem USB · fila pausada até reconectar",
            None,
            None,
        )
    } else if deps.push_queue_depth > 32 {
        check(
            "push",
            "bad",
            "Envio ao painel",
            format!(
                "Fila grande ({}) · sync pode estar atrasado",
                deps.push_queue_depth
            ),
            None,
            None,
        )
    } else {
        check(
            "push",
            "ok",
            "Envio ao painel",
            if deps.push_queue_depth == 0 {
                "Fila limpa · push em dia".into()
            } else {
                format!("Fila com {} · dentro do normal", deps.push_queue_depth)
            },
            None,
            None,
        )
    }
}

fn probe_full_disk_access(roots: &ScanRoots) -> bool {
    let mode_db = roots
        .home
        .join("Library/DoNotDisturb/DB/ModeConfigurations.json");
    let assertions = roots.home.join("Library/DoNotDisturb/DB/Assertions.json");
    file_readable(&mode_db) || file_readable(&assertions)
}

fn file_readable(path: &Path) -> bool {
    path.is_file() && std::fs::File::open(path).is_ok()
}

#[cfg(target_os = "macos")]
fn probe_launch_agent() -> bool {
    let uid = std::process::Command::new("id")
        .arg("-u")
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| s.trim().to_string())
        .unwrap_or_else(|| "501".into());
    Command::new("launchctl")
        .arg("print")
        .arg(format!("gui/{uid}/com.tessera.agentd"))
        .output()
        .map(|o| o.status.success())
        .unwrap_or(true)
}

#[cfg(not(target_os = "macos"))]
fn probe_launch_agent() -> bool {
    true
}

fn probe_media(enabled: bool, scan: &ScanResult) -> (&'static str, String) {
    if !enabled {
        return ("skip", "Desligado neste Mac".into());
    }
    let Some(item) = item(scan, "media") else {
        return ("skip", "Varredura indisponível".into());
    };
    if item.state == ScanState::Missing {
        return ("skip", "Nenhum app de música detectado".into());
    }
    let mut cap = MediaCapability::with_default_watcher();
    let line = cap.build_line();
    if line.contains("\t0\t") {
        return (
            "warn",
            "Watcher indisponível · reinstale ou autorize".into(),
        );
    }
    let parts: Vec<&str> = line.split('\t').collect();
    let state = parts.get(3).copied().unwrap_or("stopped");
    let app = parts.get(6).copied().unwrap_or("").trim();
    let label = if app.is_empty() {
        "App de música"
    } else {
        app
    };
    ("ok", format!("{label} detectado · agora: {state}"))
}

fn probe_focus(enabled: bool, scan: &ScanResult, files_ok: bool) -> (&'static str, String) {
    if !enabled {
        return ("skip", "Desligado neste Mac".into());
    }
    let Some(item) = item(scan, "focus") else {
        return ("skip", "Varredura indisponível".into());
    };
    if item.state == ScanState::Missing {
        return ("skip", "Focus do macOS indisponível".into());
    }
    if !files_ok {
        return (
            "warn",
            "Último modo desconhecido · pausado até liberar acesso aos arquivos".into(),
        );
    }
    if item.state == ScanState::NeedsAuth {
        return (
            "bad",
            "Sem acesso aos DBs do Focus · marque Tessera em Privacidade".into(),
        );
    }
    ("ok", "Focus ok · modos legíveis".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tessera_scan::{ScanItem, ScanResult};

    fn fake_deps(usb: bool, files: bool, push: usize) -> DoctorDeps {
        DoctorDeps {
            usb_up: usb,
            launch_agent_ok: true,
            files_ok: files,
            push_queue_depth: push,
            config: AgentdConfig::default(),
            scan: ScanResult {
                scanned_at: "0".into(),
                found: vec![
                    ScanItem {
                        id: "cursor".into(),
                        state: ScanState::Ready,
                        enabled: true,
                    },
                    ScanItem {
                        id: "claude".into(),
                        state: ScanState::NeedsAuth,
                        enabled: true,
                    },
                    ScanItem {
                        id: "codex".into(),
                        state: ScanState::Missing,
                        enabled: false,
                    },
                    ScanItem {
                        id: "media".into(),
                        state: ScanState::Ready,
                        enabled: true,
                    },
                    ScanItem {
                        id: "focus".into(),
                        state: ScanState::Ready,
                        enabled: true,
                    },
                ],
            },
            media_level: "ok",
            media_detail: "Spotify detectado · agora: paused".into(),
            focus_level: if files { "ok" } else { "warn" },
            focus_detail: if files {
                "Focus ok".into()
            } else {
                "pausado até liberar acesso".into()
            },
        }
    }

    #[test]
    fn report_has_all_check_ids() {
        let report = run_doctor(&fake_deps(true, false, 0));
        let ids: Vec<_> = report.checks.iter().map(|c| c.id.as_str()).collect();
        for id in [
            "usb", "agentd", "files", "music", "focus", "cursor", "claude", "codex", "push",
        ] {
            assert!(ids.contains(&id), "missing {id}");
        }
    }

    #[test]
    fn summary_one_bad_when_files_denied() {
        let report = run_doctor(&fake_deps(true, false, 0));
        assert_eq!(report.summary, "1 coisa precisa de você");
        let files = report.checks.iter().find(|c| c.id == "files").unwrap();
        assert_eq!(files.level, "bad");
        assert_eq!(files.settings_pane.as_deref(), Some("full_disk_access"));
        assert!(files
            .settings_url
            .as_ref()
            .is_some_and(|u| u.contains("Privacy_AllFiles")));
    }

    #[test]
    fn prepare_full_disk_returns_url() {
        let dir = tempfile::tempdir().unwrap();
        let prep = prepare_permission("full_disk_access", dir.path()).unwrap();
        assert!(prep.ok);
        assert!(prep.settings_url.contains("Privacy_AllFiles"));
        assert!(prep.binary_path.ends_with(".tessera/agentd/bin/Tessera"));
        assert!(prep.hint.contains("Tessera Agent"));
    }

    #[test]
    fn prepare_full_disk_prefers_agent_app() {
        let dir = tempfile::tempdir().unwrap();
        let macos = dir
            .path()
            .join(".tessera/agentd/Tessera Agent.app/Contents/MacOS");
        std::fs::create_dir_all(&macos).unwrap();
        std::fs::write(macos.join("Tessera"), b"bin").unwrap();
        let prep = prepare_permission("full_disk_access", dir.path()).unwrap();
        assert!(prep.binary_path.ends_with("Tessera Agent.app"));
    }

    #[test]
    fn push_warn_when_usb_down() {
        let report = run_doctor(&fake_deps(false, true, 0));
        assert_eq!(
            report.checks.iter().find(|c| c.id == "push").unwrap().level,
            "warn"
        );
    }

    #[test]
    fn serde_roundtrip() {
        let report = run_doctor(&fake_deps(true, true, 0));
        let json = serde_json::to_string(&report).unwrap();
        let back: DoctorReport = serde_json::from_str(&json).unwrap();
        assert_eq!(back.checks.len(), report.checks.len());
    }
}

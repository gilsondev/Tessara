//! Doctor engine — health checks → stable JSON for companion UI.

use std::path::Path;
use std::process::Command;

use serde::{Deserialize, Serialize};
use tessara_caps_media::MediaCapability;
use tessara_profile::AgentdConfig;
use tessara_scan::{run_scan, ScanConfigEnabled, ScanItem, ScanResult, ScanRoots, ScanState};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DoctorCheck {
    pub id: String,
    pub level: String,
    pub title: String,
    pub detail: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub action: Option<String>,
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
    let bad = checks
        .iter()
        .filter(|c| c.level == "bad")
        .count();
    let warn = checks
        .iter()
        .filter(|c| c.level == "warn")
        .count();
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
        DoctorCheck {
            id: "usb".into(),
            level: "ok".into(),
            title: "Painel no cabo".into(),
            detail: "USB ok · handshake · painel conectado".into(),
            action: None,
        }
    } else {
        DoctorCheck {
            id: "usb".into(),
            level: "warn".into(),
            title: "Painel no cabo".into(),
            detail: "Nenhum painel USB agora · conecte o cabo para sync".into(),
            action: Some("setup".into()),
        }
    }
}

fn check_agentd(deps: &DoctorDeps) -> DoctorCheck {
    if deps.launch_agent_ok {
        DoctorCheck {
            id: "agentd".into(),
            level: "ok".into(),
            title: "App em segundo plano".into(),
            detail: "Abre com o Mac e está rodando agora".into(),
            action: None,
        }
    } else {
        DoctorCheck {
            id: "agentd".into(),
            level: "warn".into(),
            title: "App em segundo plano".into(),
            detail: "agentd rodando, mas LaunchAgent com.tessera.agentd não está carregado".into(),
            action: None,
        }
    }
}

fn check_files(deps: &DoctorDeps) -> DoctorCheck {
    if deps.files_ok {
        DoctorCheck {
            id: "files".into(),
            level: "ok".into(),
            title: "Acesso aos arquivos".into(),
            detail: "Full Disk Access ok para paths do Tessara".into(),
            action: None,
        }
    } else {
        DoctorCheck {
            id: "files".into(),
            level: "bad".into(),
            title: "Acesso aos arquivos".into(),
            detail: "Claude e Focus precisam que você marque o Tessara em Ajustes do Mac.".into(),
            action: Some("setup".into()),
        }
    }
}

fn check_music(deps: &DoctorDeps) -> DoctorCheck {
    DoctorCheck {
        id: "music".into(),
        level: deps.media_level.into(),
        title: "Música".into(),
        detail: deps.media_detail.clone(),
        action: None,
    }
}

fn check_focus(deps: &DoctorDeps) -> DoctorCheck {
    DoctorCheck {
        id: "focus".into(),
        level: deps.focus_level.into(),
        title: "Focus".into(),
        detail: deps.focus_detail.clone(),
        action: if deps.focus_level == "bad" {
            Some("setup".into())
        } else {
            None
        },
    }
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
        return DoctorCheck {
            id: id.into(),
            level: "skip".into(),
            title: title.into(),
            detail: "Varredura indisponível".into(),
            action: None,
        };
    };
    if !enabled {
        return DoctorCheck {
            id: id.into(),
            level: "skip".into(),
            title: title.into(),
            detail: "Desligado neste Mac — nada a fazer".into(),
            action: None,
        };
    }
    match item.state {
        ScanState::Missing => DoctorCheck {
            id: id.into(),
            level: "skip".into(),
            title: title.into(),
            detail: "Não instalado neste Mac — nada a fazer".into(),
            action: None,
        },
        ScanState::Ready => DoctorCheck {
            id: id.into(),
            level: "ok".into(),
            title: title.into(),
            detail: "Conta logada · cotas lidas".into(),
            action: None,
        },
        ScanState::NeedsAuth => {
            let (level, detail) = if !files_ok && (id == "claude" || id == "cursor") {
                (
                    "warn",
                    "App achado · cotas bloqueadas pela autorização de arquivos",
                )
            } else {
                ("warn", "App achado · falta login ou autorização")
            };
            DoctorCheck {
                id: id.into(),
                level: level.into(),
                title: title.into(),
                detail: detail.into(),
                action: if level == "warn" && !files_ok {
                    Some("setup".into())
                } else {
                    None
                },
            }
        }
    }
}

fn check_push(deps: &DoctorDeps) -> DoctorCheck {
    if !deps.usb_up {
        DoctorCheck {
            id: "push".into(),
            level: "warn".into(),
            title: "Envio ao painel".into(),
            detail: "Sem USB · fila pausada até reconectar".into(),
            action: None,
        }
    } else if deps.push_queue_depth > 32 {
        DoctorCheck {
            id: "push".into(),
            level: "bad".into(),
            title: "Envio ao painel".into(),
            detail: format!(
                "Fila grande ({}) · sync pode estar atrasado",
                deps.push_queue_depth
            ),
            action: None,
        }
    } else {
        DoctorCheck {
            id: "push".into(),
            level: "ok".into(),
            title: "Envio ao painel".into(),
            detail: if deps.push_queue_depth == 0 {
                "Fila limpa · push em dia".into()
            } else {
                format!("Fila com {} · dentro do normal", deps.push_queue_depth)
            },
            action: None,
        }
    }
}

fn probe_full_disk_access(roots: &ScanRoots) -> bool {
    let mode_db = roots
        .home
        .join("Library/DoNotDisturb/DB/ModeConfigurations.json");
    let assertions = roots
        .home
        .join("Library/DoNotDisturb/DB/Assertions.json");
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
        return ("warn", "Watcher indisponível · reinstale ou autorize".into());
    }
    let parts: Vec<&str> = line.split('\t').collect();
    let state = parts.get(3).copied().unwrap_or("stopped");
    let app = parts.get(6).copied().unwrap_or("").trim();
    let label = if app.is_empty() { "App de música" } else { app };
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
            "Sem acesso aos DBs do Focus · marque Tessara em Privacidade".into(),
        );
    }
    ("ok", "Focus ok · modos legíveis".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tessara_scan::{ScanItem, ScanResult};

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
        assert_eq!(report.checks.iter().find(|c| c.id == "files").unwrap().level, "bad");
    }

    #[test]
    fn push_warn_when_usb_down() {
        let report = run_doctor(&fake_deps(false, true, 0));
        assert_eq!(report.checks.iter().find(|c| c.id == "push").unwrap().level, "warn");
    }

    #[test]
    fn serde_roundtrip() {
        let report = run_doctor(&fake_deps(true, true, 0));
        let json = serde_json::to_string(&report).unwrap();
        let back: DoctorReport = serde_json::from_str(&json).unwrap();
        assert_eq!(back.checks.len(), report.checks.len());
    }
}

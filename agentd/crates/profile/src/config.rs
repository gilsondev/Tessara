use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use thiserror::Error;
use uuid::Uuid;

use crate::MachineProfile;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct AgentdConfig {
    pub media: bool,
    pub focus: bool,
    /// Master: any AI Tokens push over USB.
    pub aitokens: bool,
    /// Per-provider collect (ignored when `aitokens` is false).
    pub aitokens_cursor: bool,
    pub aitokens_claude: bool,
    pub aitokens_codex: bool,
    pub calendar: bool,
    /// Todoist HTTPS on the Mac; panel receives USB snapshots.
    pub todoist: bool,
    pub setup_completed: bool,
    /// When true, agentd downloads and applies GitHub Releases without prompt.
    pub auto_update: bool,
}

impl Default for AgentdConfig {
    fn default() -> Self {
        Self {
            media: true,
            focus: true,
            aitokens: true,
            aitokens_cursor: true,
            aitokens_claude: true,
            aitokens_codex: true,
            calendar: true,
            todoist: true,
            setup_completed: false,
            auto_update: true,
        }
    }
}

impl AgentdConfig {
    /// Recompute master `aitokens` from per-provider flags.
    pub fn sync_aitokens_master(&mut self) {
        self.aitokens = self.aitokens_cursor || self.aitokens_claude || self.aitokens_codex;
    }
}

#[derive(Debug, Clone)]
pub struct ProfilePaths {
    pub tessera_root: PathBuf,
}

impl ProfilePaths {
    pub fn from_tessera_root(tessera_root: impl Into<PathBuf>) -> Self {
        Self {
            tessera_root: tessera_root.into(),
        }
    }

    pub fn default_home() -> Self {
        Self::from_tessera_root(crate::default_tessera_dir())
    }

    pub fn machine_json(&self) -> PathBuf {
        self.tessera_root.join("machine.json")
    }

    pub fn agentd_dir(&self) -> PathBuf {
        self.tessera_root.join("agentd")
    }

    pub fn config_toml(&self) -> PathBuf {
        self.agentd_dir().join("config.toml")
    }

    pub fn scan_json(&self) -> PathBuf {
        self.agentd_dir().join("scan.json")
    }

    pub fn agentd_log(&self) -> PathBuf {
        self.agentd_dir().join("agentd.log")
    }
}

#[derive(Debug, Error)]
pub enum ProfileError {
    #[error("io: {0}")]
    Io(#[from] io::Error),
    #[error("json: {0}")]
    Json(#[from] serde_json::Error),
    #[error("toml: {0}")]
    Toml(#[from] toml::de::Error),
    #[error("toml serialize: {0}")]
    TomlSer(#[from] toml::ser::Error),
}

#[derive(Debug, Serialize, Deserialize)]
struct MachineJson {
    machine_id: String,
    display_name: String,
}

pub fn load_or_init() -> Result<MachineProfile, ProfileError> {
    load_or_init_with_paths(&ProfilePaths::default_home())
}

pub fn load_or_init_with_paths(paths: &ProfilePaths) -> Result<MachineProfile, ProfileError> {
    fs::create_dir_all(paths.agentd_dir())?;

    let machine = load_or_init_machine(&paths.machine_json())?;
    let config = load_or_init_config(&paths.config_toml())?;

    Ok(MachineProfile {
        machine_id: machine.machine_id,
        display_name: machine.display_name,
        config,
    })
}

fn load_or_init_machine(path: &Path) -> Result<MachineJson, ProfileError> {
    if path.exists() {
        let raw = fs::read_to_string(path)?;
        return Ok(serde_json::from_str(&raw)?);
    }

    let parent = path
        .parent()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "machine.json has no parent"))?;
    fs::create_dir_all(parent)?;

    let machine = MachineJson {
        machine_id: Uuid::new_v4().to_string(),
        display_name: default_display_name(),
    };
    let pretty = serde_json::to_string_pretty(&machine)?;
    fs::write(path, format!("{pretty}\n"))?;
    Ok(machine)
}

fn load_or_init_config(path: &Path) -> Result<AgentdConfig, ProfileError> {
    if path.exists() {
        let raw = fs::read_to_string(path)?;
        return Ok(toml::from_str(&raw)?);
    }

    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }

    let config = AgentdConfig::default();
    write_config(path, &config)?;
    Ok(config)
}

pub fn write_config(path: &Path, config: &AgentdConfig) -> Result<(), ProfileError> {
    let body = toml::to_string_pretty(config)?;
    fs::write(path, format!("{body}\n"))?;
    Ok(())
}

pub fn read_config(path: &Path) -> Result<AgentdConfig, ProfileError> {
    let raw = fs::read_to_string(path)?;
    Ok(toml::from_str(&raw)?)
}

pub fn write_machine_display_name(path: &Path, display_name: &str) -> Result<(), ProfileError> {
    let trimmed = display_name.trim();
    if trimmed.is_empty() {
        return Err(ProfileError::Io(io::Error::new(
            io::ErrorKind::InvalidInput,
            "display_name empty",
        )));
    }
    let machine = if path.exists() {
        let raw = fs::read_to_string(path)?;
        serde_json::from_str::<MachineJson>(&raw)?
    } else {
        load_or_init_machine(path)?
    };
    let updated = MachineJson {
        machine_id: machine.machine_id,
        display_name: trimmed.to_string(),
    };
    let pretty = serde_json::to_string_pretty(&updated)?;
    fs::write(path, format!("{pretty}\n"))?;
    Ok(())
}

pub fn write_scan_json(path: &Path, value: &serde_json::Value) -> Result<(), ProfileError> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let pretty = serde_json::to_string_pretty(value)?;
    fs::write(path, format!("{pretty}\n"))?;
    Ok(())
}

pub fn read_scan_json(path: &Path) -> Result<Option<serde_json::Value>, ProfileError> {
    if !path.is_file() {
        return Ok(None);
    }
    let raw = fs::read_to_string(path)?;
    Ok(Some(serde_json::from_str(&raw)?))
}

fn default_display_name() -> String {
    // Prefer the friendly ComputerName (e.g. "MacBook Pro de Gilson") over DNS hostname.
    #[cfg(target_os = "macos")]
    {
        if let Ok(out) = std::process::Command::new("scutil")
            .args(["--get", "ComputerName"])
            .output()
        {
            if out.status.success() {
                let name = String::from_utf8_lossy(&out.stdout).trim().to_string();
                if !name.is_empty() {
                    return name;
                }
            }
        }
    }
    hostname::get()
        .ok()
        .and_then(|h| h.into_string().ok())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "Este Mac".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agentd_config_defaults_all_true() {
        let c = AgentdConfig::default();
        assert!(c.media && c.focus && c.aitokens && c.calendar && c.todoist);
        assert!(c.aitokens_cursor && c.aitokens_claude && c.aitokens_codex);
    }

    #[test]
    fn legacy_toml_without_provider_flags_keeps_defaults() {
        let c: AgentdConfig = toml::from_str(
            r#"
media = true
focus = true
aitokens = true
calendar = true
setup_completed = true
"#,
        )
        .unwrap();
        assert!(c.setup_completed);
        // Missing per-provider keys → struct Default (all true) via #[serde(default)].
        assert!(c.aitokens_cursor && c.aitokens_claude && c.aitokens_codex);
    }
}

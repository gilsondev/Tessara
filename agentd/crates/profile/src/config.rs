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
    pub aitokens: bool,
    pub calendar: bool,
}

impl Default for AgentdConfig {
    fn default() -> Self {
        Self {
            media: true,
            focus: true,
            aitokens: true,
            calendar: true,
        }
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

fn default_display_name() -> String {
    hostname::get()
        .ok()
        .and_then(|h| h.into_string().ok())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "tessara".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agentd_config_defaults_all_true() {
        let c = AgentdConfig::default();
        assert!(c.media && c.focus && c.aitokens && c.calendar);
    }
}

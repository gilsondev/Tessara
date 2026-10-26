mod config;

use std::path::PathBuf;

pub use config::{
    load_or_init, load_or_init_with_paths, read_config, write_config, AgentdConfig, ProfileError,
    ProfilePaths,
};

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MachineProfile {
    pub machine_id: String,
    pub display_name: String,
    #[serde(skip)]
    pub config: AgentdConfig,
}

pub fn default_tessera_dir() -> PathBuf {
    dirs::home_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join(".tessera")
}

pub fn default_agentd_dir() -> PathBuf {
    default_tessera_dir().join("agentd")
}

mod config;

use std::path::PathBuf;

pub use config::{
    load_or_init, load_or_init_with_paths, read_config, read_scan_json, write_config,
    write_machine_display_name, write_scan_json, AgentdConfig, ProfileError, ProfilePaths,
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
    let home = dirs::home_dir().unwrap_or_else(|| PathBuf::from("."));
    let next = home.join(".tessera");
    // One-shot migrate from the previous home dir name.
    let legacy = home.join(concat!(".", "tes", "sara"));
    if !next.exists() && legacy.exists() {
        let _ = std::fs::rename(&legacy, &next);
    }
    next
}

pub fn default_agentd_dir() -> PathBuf {
    default_tessera_dir().join("agentd")
}

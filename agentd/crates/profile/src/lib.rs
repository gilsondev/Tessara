use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Serialize, Deserialize)]
pub struct MachineProfile {
    pub machine_id: String,
    pub display_name: String,
}

pub fn default_agentd_dir() -> PathBuf {
    dirs::home_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join(".tessera/agentd")
}

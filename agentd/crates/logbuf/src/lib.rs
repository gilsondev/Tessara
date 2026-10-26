//! In-memory ring buffer plus optional append-only log file.

use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};

pub const DEFAULT_CAPACITY: usize = 2000;

#[derive(Debug)]
pub struct LogBuf {
    capacity: usize,
    lines: RwLock<Vec<String>>,
    file: RwLock<Option<PathBuf>>,
}

impl LogBuf {
    pub fn new(capacity: usize) -> Self {
        Self {
            capacity: capacity.max(1),
            lines: RwLock::new(Vec::new()),
            file: RwLock::new(None),
        }
    }

    pub fn with_file(path: impl Into<PathBuf>, capacity: usize) -> std::io::Result<Self> {
        let path = path.into();
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        let buf = Self::new(capacity);
        *buf.file.write().expect("log file lock") = Some(path);
        Ok(buf)
    }

    pub fn append(&self, line: impl Into<String>) {
        let line = line.into();
        {
            let mut guard = self.lines.write().expect("log lines lock");
            guard.push(line.clone());
            if guard.len() > self.capacity {
                let drop = guard.len() - self.capacity;
                guard.drain(0..drop);
            }
        }
        if let Some(path) = self.file.read().expect("log file lock").as_ref() {
            if let Ok(mut f) = OpenOptions::new().create(true).append(true).open(path) {
                let _ = writeln!(f, "{line}");
            }
        }
    }

    pub fn tail(&self, n: usize) -> Vec<String> {
        let guard = self.lines.read().expect("log lines lock");
        let n = n.min(guard.len());
        guard[guard.len() - n..].to_vec()
    }

    pub fn load_file_tail(&self, n: usize) -> std::io::Result<Vec<String>> {
        let path = self
            .file
            .read()
            .expect("log file lock")
            .clone()
            .ok_or_else(|| std::io::Error::new(std::io::ErrorKind::NotFound, "no log file"))?;
        let raw = fs::read_to_string(&path)?;
        let all: Vec<&str> = raw.lines().collect();
        let n = n.min(all.len());
        Ok(all[all.len() - n..]
            .iter()
            .map(|s| (*s).to_string())
            .collect())
    }

    pub fn file_path(&self) -> Option<PathBuf> {
        self.file.read().expect("log file lock").clone()
    }
}

pub type SharedLogBuf = Arc<LogBuf>;

pub fn default_log_path(home: &Path) -> PathBuf {
    home.join(".tessera/agentd/agentd.log")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ring_drops_oldest() {
        let buf = LogBuf::new(3);
        buf.append("a");
        buf.append("b");
        buf.append("c");
        buf.append("d");
        assert_eq!(buf.tail(10), vec!["b", "c", "d"]);
    }

    #[test]
    fn persists_to_file() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("agentd.log");
        let buf = LogBuf::with_file(&path, 100).unwrap();
        buf.append("line one");
        let disk = fs::read_to_string(&path).unwrap();
        assert!(disk.contains("line one"));
    }
}

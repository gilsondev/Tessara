//! Shared process timeout and atomic file replace for the agent.

use std::fs;
use std::io::{self, Read, Write};
use std::path::Path;
use std::process::{Command, Output, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::thread;
use std::time::{Duration, Instant};

static TMP_SEQ: AtomicU64 = AtomicU64::new(1);

/// Output of `cmd`, or `None` when it is still running after `timeout` (the child is killed).
/// Stdout is read while the child runs, so a large write cannot fill the pipe and stall the wait.
pub fn command_output_timeout(cmd: &mut Command, timeout: Duration) -> Option<Output> {
    cmd.stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        cmd.process_group(0);
    }
    let mut child = cmd.spawn().ok()?;
    let mut stdout = child.stdout.take()?;
    let stderr = child.stderr.take();
    let out_reader = thread::spawn(move || {
        let mut buf = Vec::new();
        let _ = stdout.read_to_end(&mut buf);
        buf
    });
    let err_reader = thread::spawn(move || {
        let mut buf = Vec::new();
        if let Some(mut stderr) = stderr {
            let _ = stderr.read_to_end(&mut buf);
        }
        buf
    });
    let started = Instant::now();
    let status = loop {
        match child.try_wait() {
            Ok(Some(status)) => break status,
            Ok(None) if started.elapsed() >= timeout => {
                // Drop the readers. Joining them would wait on a grandchild that still holds the pipe.
                drop(out_reader);
                drop(err_reader);
                stop_process_group(&mut child);
                return None;
            }
            Ok(None) => thread::sleep(Duration::from_millis(20)),
            Err(_) => {
                drop(out_reader);
                drop(err_reader);
                stop_process_group(&mut child);
                return None;
            }
        }
    };
    let stdout = out_reader.join().unwrap_or_default();
    let stderr = err_reader.join().unwrap_or_default();
    Some(Output {
        status,
        stdout,
        stderr,
    })
}

fn stop_process_group(child: &mut std::process::Child) {
    let pid = child.id();
    let _ = Command::new("/bin/kill")
        .args(["-9", &format!("-{pid}")])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
    let _ = child.kill();
    let _ = child.wait();
}

/// Replace `path` via a unique sibling temp file. Two writers never share the same `.tmp`.
/// New files are mode 0600.
pub fn write_atomic(path: &Path, bytes: impl AsRef<[u8]>) -> io::Result<()> {
    write_atomic_mode(path, bytes, 0o600)
}

/// Like `write_atomic`, but an existing file keeps its permission bits.
pub fn write_atomic_preserve(path: &Path, bytes: impl AsRef<[u8]>) -> io::Result<()> {
    let mode = fs::metadata(path).map(|meta| file_mode(&meta)).unwrap_or(0o600);
    write_atomic_mode(path, bytes, mode)
}

fn file_mode(meta: &fs::Metadata) -> u32 {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        meta.permissions().mode() & 0o777
    }
    #[cfg(not(unix))]
    {
        let _ = meta;
        0o600
    }
}

fn write_atomic_mode(path: &Path, bytes: impl AsRef<[u8]>, mode: u32) -> io::Result<()> {
    let name = path.file_name().unwrap_or_default().to_string_lossy();
    let n = TMP_SEQ.fetch_add(1, Ordering::Relaxed);
    let tmp = path.with_file_name(format!("{name}.{}.{n}.tmp", std::process::id()));
    {
        let mut opts = fs::OpenOptions::new();
        opts.write(true).create(true).truncate(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            opts.mode(mode);
        }
        #[cfg(not(unix))]
        let _ = mode;
        let mut file = opts.open(&tmp)?;
        file.write_all(bytes.as_ref())?;
    }
    if let Err(err) = fs::rename(&tmp, path) {
        let _ = fs::remove_file(&tmp);
        return Err(err);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    #[test]
    fn timeout_kills_grandchildren() {
        let started = Instant::now();
        let out = command_output_timeout(
            Command::new("sh").args(["-c", "sleep 10 & sleep 10"]),
            Duration::from_secs(1),
        );
        assert!(out.is_none());
        assert!(started.elapsed() < Duration::from_secs(2));
    }

    #[test]
    fn timeout_kills_a_slow_process() {
        let started = Instant::now();
        let out = command_output_timeout(
            Command::new("sleep").arg("10"),
            Duration::from_secs(1),
        );
        assert!(out.is_none());
        assert!(started.elapsed() < Duration::from_secs(3));
    }

    #[test]
    fn large_stdout_is_returned() {
        let out = command_output_timeout(
            Command::new("head").args(["-c", "200000", "/dev/zero"]),
            Duration::from_secs(2),
        )
        .expect("head");
        assert!(out.status.success());
        assert_eq!(out.stdout.len(), 200_000);
    }

    #[test]
    fn concurrent_writes_stay_complete_json() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("cache.json");
        let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let seen = Arc::new(Mutex::new(0u32));
        let reader = {
            let path = path.clone();
            let stop = stop.clone();
            let seen = seen.clone();
            thread::spawn(move || {
                while !stop.load(Ordering::Relaxed) {
                    if let Ok(raw) = fs::read(&path) {
                        if raw.is_empty() {
                            continue;
                        }
                        serde_json::from_slice::<serde_json::Value>(&raw).unwrap_or_else(|err| {
                            panic!("{err}: {}", String::from_utf8_lossy(&raw))
                        });
                        *seen.lock().unwrap() += 1;
                    }
                    thread::sleep(Duration::from_millis(1));
                }
            })
        };
        let mut writers = Vec::new();
        for id in 0..2 {
            let path = path.clone();
            writers.push(thread::spawn(move || {
                for n in 0..500 {
                    let body = format!(r#"{{"id":{id},"n":{n}}}"#);
                    write_atomic(&path, body).unwrap();
                }
            }));
        }
        for writer in writers {
            writer.join().unwrap();
        }
        stop.store(true, Ordering::Relaxed);
        reader.join().unwrap();
        assert!(*seen.lock().unwrap() > 0);
        let raw = fs::read_to_string(&path).unwrap();
        serde_json::from_str::<serde_json::Value>(&raw).unwrap();
    }
}

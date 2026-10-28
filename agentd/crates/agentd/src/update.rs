//! Self-update: poll Pages `latest.json`, download Releases, install agentd + app.

use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use tessera_logbuf::SharedLogBuf;
use tessera_profile::{read_config, write_config, AgentdConfig, ProfilePaths};

use crate::logutil;

pub const DEFAULT_MANIFEST_URL: &str = "https://gilsondev.github.io/Tessera/latest.json";
const POLL_INTERVAL: Duration = Duration::from_secs(6 * 3600);
const STARTUP_DELAY: Duration = Duration::from_secs(20);

#[derive(Debug, Clone, Deserialize)]
pub struct LatestManifest {
    pub version: String,
    #[serde(default)]
    #[allow(dead_code)]
    pub released_at: String,
    pub agentd_url: String,
    pub app_url: String,
    #[serde(default)]
    pub sha256: Sha256s,
    /// Hex-encoded ed25519 signature of `signed_payload`. Empty is not trusted.
    #[serde(default)]
    pub signature: String,
}

/// Public key for release manifests. All zeros means no key is embedded yet,
/// so a manifest cannot be trusted and an update does not install.
const UPDATE_PUBKEY: [u8; 32] = [0; 32];

pub fn signed_payload(m: &LatestManifest) -> String {
    format!(
        "{}\n{}\n{}\n{}\n{}",
        m.version.trim(),
        m.agentd_url.trim(),
        m.app_url.trim(),
        m.sha256.agentd.trim(),
        m.sha256.app.trim()
    )
}

pub fn manifest_trusted(m: &LatestManifest, pubkey: &[u8]) -> Result<(), String> {
    if m.sha256.agentd.trim().is_empty() || m.sha256.app.trim().is_empty() {
        return Err("manifest sem sha256".into());
    }
    if pubkey.is_empty() || pubkey.iter().all(|b| *b == 0) {
        return Err("chave de update ausente".into());
    }
    let sig = hex::decode(m.signature.trim()).map_err(|_| "assinatura inválida".to_string())?;
    let key = ring::signature::UnparsedPublicKey::new(&ring::signature::ED25519, pubkey);
    key.verify(signed_payload(m).as_bytes(), &sig)
        .map_err(|_| "assinatura inválida".to_string())
}

#[derive(Debug, Clone, Default, Deserialize)]
pub struct Sha256s {
    #[serde(default)]
    pub agentd: String,
    #[serde(default)]
    pub app: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct UpdateStatus {
    pub ok: bool,
    pub current_version: String,
    pub latest_version: Option<String>,
    pub auto_update: bool,
    pub update_available: bool,
    pub applying: bool,
    pub last_error: Option<String>,
    pub last_check_unix: Option<u64>,
    pub manifest_url: String,
}

#[derive(Default)]
struct Inner {
    latest: Option<LatestManifest>,
    applying: bool,
    last_error: Option<String>,
    last_check: Option<Instant>,
}

#[derive(Clone)]
pub struct UpdateService {
    paths: ProfilePaths,
    logs: Option<SharedLogBuf>,
    manifest_url: String,
    inner: Arc<Mutex<Inner>>,
}

impl UpdateService {
    pub fn new(paths: ProfilePaths, logs: Option<SharedLogBuf>) -> Self {
        let manifest_url = std::env::var("TESSERA_UPDATE_URL")
            .unwrap_or_else(|_| DEFAULT_MANIFEST_URL.to_string());
        Self {
            paths,
            logs,
            manifest_url,
            inner: Arc::new(Mutex::new(Inner::default())),
        }
    }

    pub fn status(&self) -> UpdateStatus {
        let cfg = read_config(&self.paths.config_toml()).unwrap_or_default();
        let g = self.inner.lock().expect("update lock");
        let latest = g.latest.as_ref().map(|m| m.version.clone());
        let update_available = latest
            .as_ref()
            .map(|v| version_newer(v, env!("CARGO_PKG_VERSION")))
            .unwrap_or(false);
        UpdateStatus {
            ok: true,
            current_version: env!("CARGO_PKG_VERSION").to_string(),
            latest_version: latest,
            auto_update: cfg.auto_update,
            update_available,
            applying: g.applying,
            last_error: g.last_error.clone(),
            last_check_unix: g.last_check.map(|_| {
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs()
            }),
            manifest_url: self.manifest_url.clone(),
        }
    }

    pub fn set_auto_update(&self, on: bool) -> Result<UpdateStatus, String> {
        let path = self.paths.config_toml();
        let mut cfg = read_config(&path).map_err(|e| e.to_string())?;
        // Read again so a scan that finished while this click was in flight
        // does not lose its capability flags, and this click is the one written.
        if let Ok(fresh) = read_config(&path) {
            cfg = fresh;
        }
        cfg.auto_update = on;
        write_config(&path, &cfg).map_err(|e| e.to_string())?;
        Ok(self.status())
    }

    /// Fetch manifest; if Auto and newer, apply.
    pub fn check_and_maybe_apply(&self) -> UpdateStatus {
        match self.fetch_manifest() {
            Ok(m) => {
                {
                    let mut g = self.inner.lock().expect("update lock");
                    g.latest = Some(m.clone());
                    g.last_error = None;
                    g.last_check = Some(Instant::now());
                }
                let cfg = read_config(&self.paths.config_toml()).unwrap_or_default();
                if cfg.auto_update && version_newer(&m.version, env!("CARGO_PKG_VERSION")) {
                    if let Err(e) = self.apply(&m) {
                        let mut g = self.inner.lock().expect("update lock");
                        g.last_error = Some(e);
                    }
                }
            }
            Err(e) => {
                let mut g = self.inner.lock().expect("update lock");
                g.last_error = Some(e);
                g.last_check = Some(Instant::now());
            }
        }
        self.status()
    }

    fn fetch_manifest(&self) -> Result<LatestManifest, String> {
        logutil::info(
            &self.logs,
            format!("update: consultando {}", self.manifest_url),
        );
        let body = ureq::get(&self.manifest_url)
            .timeout(Duration::from_secs(20))
            .call()
            .map_err(|e| format!("manifest http: {e}"))?
            .into_string()
            .map_err(|e| format!("manifest body: {e}"))?;
        serde_json::from_str(&body).map_err(|e| format!("manifest json: {e}"))
    }

    fn apply(&self, m: &LatestManifest) -> Result<(), String> {
        {
            let mut g = self.inner.lock().expect("update lock");
            if g.applying {
                return Err("já aplicando update".into());
            }
            g.applying = true;
        }
        let result = self.apply_inner(m);
        {
            let mut g = self.inner.lock().expect("update lock");
            g.applying = false;
            if let Err(ref e) = result {
                g.last_error = Some(e.clone());
            }
        }
        result
    }

    fn apply_inner(&self, m: &LatestManifest) -> Result<(), String> {
        manifest_trusted(m, &UPDATE_PUBKEY)?;
        logutil::info(
            &self.logs,
            format!(
                "update: baixando {} → {}",
                env!("CARGO_PKG_VERSION"),
                m.version
            ),
        );
        let tmp = std::env::temp_dir().join(format!("tessera-update-{}", m.version));
        let _ = fs::remove_dir_all(&tmp);
        fs::create_dir_all(&tmp).map_err(|e| e.to_string())?;

        let agentd_tgz = tmp.join("agentd.tgz");
        let app_zip = tmp.join("Tessera.app.zip");
        download(&m.agentd_url, &agentd_tgz)?;
        download(&m.app_url, &app_zip)?;

        verify_sha256(&agentd_tgz, &m.sha256.agentd)?;
        verify_sha256(&app_zip, &m.sha256.app)?;

        let stage = tmp.join("stage");
        fs::create_dir_all(&stage).map_err(|e| e.to_string())?;
        run_ok(
            "tar",
            &[
                "-C",
                stage.to_str().unwrap(),
                "-xzf",
                agentd_tgz.to_str().unwrap(),
            ],
        )?;

        let home = std::env::var("HOME").map_err(|_| "HOME ausente".to_string())?;
        let install_root = PathBuf::from(&home).join(".tessera/agentd");
        let daemon_bin = tessera_doctor::agentd_install_binary(Path::new(&home));
        let daemon_app = daemon_bin
            .parent()
            .and_then(|p| p.parent())
            .and_then(|p| p.parent())
            .ok_or_else(|| "caminho do agent inválido".to_string())?
            .to_path_buf();
        let staged_bin = if stage.join("bin/Tessera").is_file() {
            stage.join("bin/Tessera")
        } else if stage.join("bin/agentd").is_file() {
            stage.join("bin/agentd")
        } else {
            return Err("agentd.tgz sem bin/Tessera".into());
        };
        let stage_app = install_root.join(".Tessera Agent.app.staging");
        let _ = fs::remove_dir_all(&stage_app);
        install_bundled_agent(&staged_bin, &stage_app)?;
        verify_codesign(&stage_app)?;
        swap_dir(&stage_app, &daemon_app)?;
        let bin_dir = install_root.join("bin");
        fs::create_dir_all(&bin_dir).map_err(|e| e.to_string())?;
        let _ = fs::remove_file(bin_dir.join("Tessera"));
        let _ = std::os::unix::fs::symlink(
            PathBuf::from("../Tessera Agent.app/Contents/MacOS/Tessera"),
            bin_dir.join("Tessera"),
        );

        let unzip_dir = tmp.join("app_unzip");
        let _ = fs::remove_dir_all(&unzip_dir);
        fs::create_dir_all(&unzip_dir).map_err(|e| e.to_string())?;
        run_ok(
            "ditto",
            &[
                "-x",
                "-k",
                app_zip.to_str().unwrap(),
                unzip_dir.to_str().unwrap(),
            ],
        )?;
        let app_src = find_app(&unzip_dir).ok_or_else(|| "zip sem Tessera.app".to_string())?;
        let app_dst = PathBuf::from("/Applications/Tessera.app");
        let app_stage = PathBuf::from("/Applications/.Tessera.app.staging");
        let _ = fs::remove_dir_all(&app_stage);
        run_ok(
            "cp",
            &["-R", app_src.to_str().unwrap(), app_stage.to_str().unwrap()],
        )?;
        verify_codesign(&app_stage)?;
        swap_dir(&app_stage, &app_dst)?;

        // Preserve auto_update / setup flags across binary replace.
        let _ = read_config(&self.paths.config_toml()).map(|c: AgentdConfig| {
            let _ = write_config(&self.paths.config_toml(), &c);
        });

        logutil::info(&self.logs, "update: instalado — reiniciando LaunchAgent");
        let uid = libc_uid();
        let label = format!("gui/{uid}/com.tessera.agentd");
        let _ = tessera_proc::command_output_timeout(
            Command::new("launchctl").args(["kickstart", "-k", &label]),
            Duration::from_secs(5),
        );

        let _ = fs::remove_dir_all(&tmp);
        Ok(())
    }
}

fn install_bundled_agent(bin: &Path, app: &Path) -> Result<(), String> {
    let exe = app.join("Contents/MacOS/Tessera");
    install_file(bin, &exe)?;
    let plist = r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>Tessera</string>
  <key>CFBundleIdentifier</key><string>dev.tessera.agentd</string>
  <key>CFBundleName</key><string>Tessera Agent</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSUIElement</key><true/>
</dict></plist>
"#;
    fs::write(app.join("Contents/Info.plist"), plist).map_err(|e| e.to_string())?;
    run_ok(
        "codesign",
        &["--force", "--sign", "-", app.to_str().unwrap_or("")],
    )
}

fn verify_codesign(path: &Path) -> Result<(), String> {
    run_ok(
        "codesign",
        &["--verify", "--strict", path.to_str().unwrap_or("")],
    )
}

fn swap_dir(staged: &Path, dst: &Path) -> Result<(), String> {
    let parent = dst
        .parent()
        .ok_or_else(|| "destino sem pasta".to_string())?;
    let mut backup_name = dst
        .file_name()
        .ok_or_else(|| "destino sem nome".to_string())?
        .to_os_string();
    backup_name.push(".bak");
    let backup = parent.join(backup_name);
    let _ = fs::remove_dir_all(&backup);
    if dst.exists() {
        fs::rename(dst, &backup).map_err(|e| e.to_string())?;
    }
    if let Err(e) = fs::rename(staged, dst) {
        if backup.exists() {
            let _ = fs::rename(&backup, dst);
        }
        return Err(e.to_string());
    }
    let _ = fs::remove_dir_all(&backup);
    Ok(())
}

/// Copy an older `bin/Tessera` or `Tessera.app` into the LaunchAgent path.
pub fn migrate_daemon_binary(home: &Path) {
    let dest = tessera_doctor::agentd_install_binary(home);
    if dest.is_file() {
        return;
    }
    let root = home.join(".tessera/agentd");
    let candidates = [
        root.join("Tessera.app/Contents/MacOS/Tessera"),
        root.join("bin/Tessera"),
        root.join("bin/agentd"),
    ];
    for src in candidates {
        if !src.is_file() {
            continue;
        }
        let app = dest
            .parent()
            .and_then(|p| p.parent())
            .and_then(|p| p.parent());
        let Some(app) = app else { return };
        if install_bundled_agent(&src, app).is_ok() {
            return;
        }
    }
}

fn libc_uid() -> u32 {
    // Avoid libc crate: `id -u`
    tessera_proc::command_output_timeout(Command::new("id").arg("-u"), Duration::from_secs(2))
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .and_then(|s| s.trim().parse().ok())
        .unwrap_or(501)
}

fn find_app(root: &Path) -> Option<PathBuf> {
    if root.join("Tessera.app").is_dir() {
        return Some(root.join("Tessera.app"));
    }
    let rd = fs::read_dir(root).ok()?;
    for e in rd.flatten() {
        let p = e.path();
        if p.extension().and_then(|x| x.to_str()) == Some("app") {
            return Some(p);
        }
        if p.is_dir() {
            if let Some(f) = find_app(&p) {
                return Some(f);
            }
        }
    }
    None
}

fn download(url: &str, dest: &Path) -> Result<(), String> {
    if url.contains("github.com") && url.ends_with("/latest") {
        return Err(
            "manifest ainda aponta para /releases/latest — espere um release com artefatos".into(),
        );
    }
    let mut resp = ureq::get(url)
        .timeout(Duration::from_secs(120))
        .call()
        .map_err(|e| format!("download {url}: {e}"))?
        .into_reader();
    let mut buf = Vec::new();
    resp.read_to_end(&mut buf)
        .map_err(|e| format!("read {url}: {e}"))?;
    fs::write(dest, buf).map_err(|e| e.to_string())?;
    Ok(())
}

fn verify_sha256(path: &Path, expect: &str) -> Result<(), String> {
    let data = fs::read(path).map_err(|e| e.to_string())?;
    let mut hasher = Sha256::new();
    hasher.update(&data);
    let got = hex::encode(hasher.finalize());
    if got.eq_ignore_ascii_case(expect.trim()) {
        Ok(())
    } else {
        Err(format!(
            "sha256 mismatch {}: got {got}, want {}",
            path.display(),
            expect.trim()
        ))
    }
}

fn install_file(src: &Path, dst: &Path) -> Result<(), String> {
    if let Some(parent) = dst.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    fs::copy(src, dst).map_err(|e| e.to_string())?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut perms = fs::metadata(dst).map_err(|e| e.to_string())?.permissions();
        perms.set_mode(0o755);
        fs::set_permissions(dst, perms).map_err(|e| e.to_string())?;
    }
    Ok(())
}

fn run_ok(cmd: &str, args: &[&str]) -> Result<(), String> {
    let out = tessera_proc::command_output_timeout(
        Command::new(cmd).args(args),
        Duration::from_secs(120),
    )
    .ok_or_else(|| format!("{cmd} excedeu o tempo"))?;
    if out.status.success() {
        Ok(())
    } else {
        Err(format!("{cmd} {:?} exit {}", args, out.status))
    }
}

/// Semver-ish: true if `remote` is strictly newer than `local`.
pub fn version_newer(remote: &str, local: &str) -> bool {
    let r = parse_ver(remote);
    let l = parse_ver(local);
    r > l
}

fn parse_ver(s: &str) -> (u64, u64, u64) {
    let mut parts = s.trim().trim_start_matches('v').split('.');
    let major = parts.next().and_then(|x| x.parse().ok()).unwrap_or(0);
    let minor = parts.next().and_then(|x| x.parse().ok()).unwrap_or(0);
    let patch = parts
        .next()
        .and_then(|x| x.split('-').next())
        .and_then(|x| x.parse().ok())
        .unwrap_or(0);
    (major, minor, patch)
}

pub fn spawn_poller(svc: UpdateService) {
    std::thread::Builder::new()
        .name("tessera-update".into())
        .spawn(move || {
            std::thread::sleep(STARTUP_DELAY);
            loop {
                let _ = svc.check_and_maybe_apply();
                std::thread::sleep(POLL_INTERVAL);
            }
        })
        .ok();
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample(sha_agentd: &str, sha_app: &str, signature: &str) -> LatestManifest {
        LatestManifest {
            version: "1.2.3".into(),
            released_at: String::new(),
            agentd_url: "https://github.com/gilsondev/Tessera/releases/download/agent-v1.2.3/agentd.tgz".into(),
            app_url: "https://github.com/gilsondev/Tessera/releases/download/agent-v1.2.3/Tessera.app.zip".into(),
            sha256: Sha256s {
                agentd: sha_agentd.into(),
                app: sha_app.into(),
            },
            signature: signature.into(),
        }
    }

    #[test]
    fn manifest_without_sha_or_bad_signature_is_refused() {
        let rng = ring::rand::SystemRandom::new();
        let pkcs8 = ring::signature::Ed25519KeyPair::generate_pkcs8(&rng).unwrap();
        let key = ring::signature::Ed25519KeyPair::from_pkcs8(pkcs8.as_ref()).unwrap();
        let pubkey = ring::signature::KeyPair::public_key(&key).as_ref().to_vec();
        let unsigned = sample("", "abc", "");
        assert!(manifest_trusted(&unsigned, &pubkey).is_err());
        let mut manifest = sample("aaa", "bbb", "");
        let sig = hex::encode(key.sign(signed_payload(&manifest).as_bytes()));
        manifest.signature = sig;
        assert!(manifest_trusted(&manifest, &pubkey).is_ok());
        let mut chars = manifest.signature.into_bytes();
        chars[10] = if chars[10] == b'a' { b'b' } else { b'a' };
        manifest.signature = String::from_utf8(chars).unwrap();
        assert!(manifest_trusted(&manifest, &pubkey).is_err());
        assert!(manifest_trusted(&sample("aaa", "bbb", ""), &UPDATE_PUBKEY).is_err());
    }

    #[test]
    fn version_compare() {
        assert!(version_newer("0.1.1", "0.1.0"));
        assert!(!version_newer("0.1.0", "0.1.0"));
        assert!(!version_newer("0.1.0", "0.2.0"));
        assert!(version_newer("1.0.0", "0.9.9"));
    }
}

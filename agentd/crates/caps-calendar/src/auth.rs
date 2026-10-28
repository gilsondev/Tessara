//! Google Desktop OAuth on this Mac: loopback redirect, PKCE, account file.
//! The client id comes from `client.json`. This crate does not ship a secret.

use std::fs;
use std::io::{ErrorKind, Read, Write};
use std::net::TcpListener;
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};

use crate::CalendarPaths;

const AUTH_URI: &str = "https://accounts.google.com/o/oauth2/v2/auth";
const TOKEN_URI: &str = "https://oauth2.googleapis.com/token";
const SCOPE: &str = "https://www.googleapis.com/auth/calendar.readonly";

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CalendarRow {
    pub id: String,
    pub account: String,
    pub name: String,
    pub color: String,
    pub selected: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "phase", rename_all = "snake_case")]
pub enum AuthStatus {
    NeedClient { message: String },
    Pending { message: String },
    Ready { message: String, account: String },
    Failed { message: String },
}

struct ClientCreds {
    client_id: String,
    client_secret: String,
}

pub fn import_client_json(paths: &CalendarPaths, raw: &str) -> Result<(), String> {
    let creds = parse_client(raw)?;
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    let stored = json!({
        "client_id": creds.client_id,
        "client_secret": creds.client_secret,
        "auth_uri": AUTH_URI,
        "token_uri": TOKEN_URI,
    });
    fs::write(
        paths.client_json(),
        serde_json::to_string_pretty(&stored).unwrap(),
    )
    .map_err(|e| e.to_string())
}

pub fn auth_status(paths: &CalendarPaths) -> AuthStatus {
    let path = paths.root.join("auth_status.json");
    if let Ok(raw) = fs::read_to_string(&path) {
        if let Ok(status) = serde_json::from_str::<AuthStatus>(&raw) {
            return status;
        }
    }
    if !paths.client_json().is_file() {
        return AuthStatus::NeedClient {
            message: need_client_message(),
        };
    }
    if has_account(paths) {
        return AuthStatus::Ready {
            message: "Conta ligada.".into(),
            account: String::new(),
        };
    }
    AuthStatus::NeedClient {
        message: "Escolha Adicionar conta para abrir o Google neste Mac.".into(),
    }
}

#[derive(Clone, Copy)]
pub enum ClientSource {
    /// Cliente publicado do Tessera. O usuário só entra no navegador.
    Tessera,
    /// JSON que o usuário escolheu.
    Imported,
}

pub fn has_tessera_client(paths: &CalendarPaths) -> bool {
    read_tessera_client(paths).is_some()
}

pub fn start_account_link(paths: &CalendarPaths) -> Result<AuthStatus, String> {
    start_with(paths, ClientSource::Imported)
}

pub fn start_with(paths: &CalendarPaths, source: ClientSource) -> Result<AuthStatus, String> {
    let creds = load_creds(paths, source)?;
    write_status(
        paths,
        &AuthStatus::Pending {
            message: "O Google abriu no navegador. Entre na conta e autorize. O retorno volta sozinho para este Mac.".into(),
        },
    )?;
    let paths = paths.clone();
    std::thread::spawn(move || {
        if let Err(err) = link_blocking(&paths, &creds) {
            let _ = write_status(&paths, &AuthStatus::Failed { message: err });
        }
    });
    auth_status_ok(paths_pending())
}

fn paths_pending() -> AuthStatus {
    AuthStatus::Pending {
        message: "O Google abriu no navegador. Entre na conta e autorize. O retorno volta sozinho para este Mac.".into(),
    }
}

fn auth_status_ok(status: AuthStatus) -> Result<AuthStatus, String> {
    Ok(status)
}

fn link_blocking(paths: &CalendarPaths, creds: &ClientCreds) -> Result<(), String> {
    let listener = TcpListener::bind("127.0.0.1:0").map_err(|e| format!("porta local: {e}"))?;
    listener.set_nonblocking(false).map_err(|e| e.to_string())?;
    let port = listener.local_addr().map_err(|e| e.to_string())?.port();
    let redirect = format!("http://127.0.0.1:{port}/");
    let verifier = random_verifier()?;
    let challenge = code_challenge_s256(&verifier);
    let url = format!(
        "{AUTH_URI}?client_id={}&redirect_uri={}&response_type=code&scope={}&code_challenge={}&code_challenge_method=S256&access_type=offline&prompt=consent",
        pct(&creds.client_id),
        pct(&redirect),
        pct(SCOPE),
        pct(&challenge),
    );
    open_browser(&url)?;
    let code = read_auth_code(&listener)?;
    write_status(
        paths,
        &AuthStatus::Pending {
            message: "Login recebido. Guardando a conta e os calendários…".into(),
        },
    )?;
    let token = exchange_code(creds, &code, &redirect, &verifier)?;
    let access = token
        .get("access_token")
        .and_then(|v| v.as_str())
        .ok_or("Google não devolveu access_token")?
        .to_string();
    let refresh = token
        .get("refresh_token")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    if refresh.is_empty() {
        return Err("Google não devolveu refresh_token. Tente de novo.".into());
    }
    let rows = fetch_calendar_list(&access)?;
    let account = rows
        .iter()
        .find(|row| row.selected)
        .map(|row| row.account.clone())
        .or_else(|| rows.first().map(|row| row.id.clone()))
        .unwrap_or_else(|| "google".into());
    save_account(paths, &account, creds, &refresh, &access)?;
    let selected: Vec<String> = rows
        .iter()
        .filter(|row| row.selected)
        .map(|row| row.id.clone())
        .collect();
    let selected = if selected.is_empty() {
        rows.iter().take(1).map(|row| row.id.clone()).collect()
    } else {
        selected
    };
    save_selection(paths, &selected)?;
    write_catalog(paths, &rows, &selected)?;
    write_status(
        paths,
        &AuthStatus::Ready {
            message: "Conta ligada. Marque os calendários que o painel pode mostrar.".into(),
            account,
        },
    )
}

pub fn list_catalog(paths: &CalendarPaths) -> Vec<CalendarRow> {
    let selected = load_selection(paths);
    let Ok(raw) = fs::read_to_string(paths.root.join("catalog.json")) else {
        return Vec::new();
    };
    let Ok(mut rows) = serde_json::from_str::<Vec<CalendarRow>>(&raw) else {
        return Vec::new();
    };
    if !selected.is_empty() {
        for row in &mut rows {
            row.selected = selected.iter().any(|id| id == &row.id);
        }
    }
    rows
}

pub fn save_selection(paths: &CalendarPaths, ids: &[String]) -> Result<(), String> {
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    let raw = serde_json::to_string(ids).map_err(|e| e.to_string())?;
    fs::write(paths.selection_json(), raw).map_err(|e| e.to_string())?;
    if let Some(mut rows) = fs::read_to_string(paths.root.join("catalog.json"))
        .ok()
        .and_then(|raw| serde_json::from_str::<Vec<CalendarRow>>(&raw).ok())
    {
        for row in &mut rows {
            row.selected = ids.iter().any(|id| id == &row.id);
        }
        let _ = write_catalog(paths, &rows, ids);
    }
    Ok(())
}

fn write_catalog(
    paths: &CalendarPaths,
    rows: &[CalendarRow],
    selected: &[String],
) -> Result<(), String> {
    let mut copy = rows.to_vec();
    for row in &mut copy {
        row.selected = selected.iter().any(|id| id == &row.id);
    }
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    fs::write(
        paths.root.join("catalog.json"),
        serde_json::to_string_pretty(&copy).map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())
}

fn load_selection(paths: &CalendarPaths) -> Vec<String> {
    fs::read_to_string(paths.selection_json())
        .ok()
        .and_then(|raw| serde_json::from_str(&raw).ok())
        .unwrap_or_default()
}

fn save_account(
    paths: &CalendarPaths,
    account: &str,
    creds: &ClientCreds,
    refresh: &str,
    access: &str,
) -> Result<(), String> {
    let dir = paths.accounts_dir();
    fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let safe: String = account
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '@' || c == '.' || c == '-' {
                c
            } else {
                '_'
            }
        })
        .collect();
    let expiry = (chrono::Utc::now() + chrono::Duration::minutes(45)).to_rfc3339();
    let body = json!({
        "account": account,
        "client_id": creds.client_id,
        "client_secret": creds.client_secret,
        "refresh_token": refresh,
        "token": access,
        "token_uri": TOKEN_URI,
        "expiry": expiry,
    });
    fs::write(
        dir.join(format!("{safe}.json")),
        serde_json::to_string_pretty(&body).unwrap(),
    )
    .map_err(|e| e.to_string())
}

fn fetch_calendar_list(access: &str) -> Result<Vec<CalendarRow>, String> {
    let resp = ureq::get("https://www.googleapis.com/calendar/v3/users/me/calendarList")
        .set("Authorization", &format!("Bearer {access}"))
        .timeout(Duration::from_secs(20))
        .call()
        .map_err(|e| format!("lista de calendários: {e}"))?;
    let body: Value = resp.into_json().map_err(|e| e.to_string())?;
    let items = body
        .get("items")
        .and_then(|v| v.as_array())
        .cloned()
        .unwrap_or_default();
    let mut rows = Vec::new();
    for item in items {
        let id = item
            .get("id")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();
        if id.is_empty() {
            continue;
        }
        let primary = item
            .get("primary")
            .and_then(|v| v.as_bool())
            .unwrap_or(false);
        let account = if id.contains('@') {
            id.clone()
        } else {
            String::new()
        };
        rows.push(CalendarRow {
            name: item
                .get("summary")
                .and_then(|v| v.as_str())
                .unwrap_or(&id)
                .to_string(),
            color: item
                .get("backgroundColor")
                .and_then(|v| v.as_str())
                .unwrap_or("#1A73E8")
                .trim_start_matches('#')
                .to_string(),
            selected: primary,
            account,
            id,
        });
    }
    if rows.iter().all(|row| row.account.is_empty()) {
        if let Some(first) = rows.first_mut() {
            first.account = first.id.clone();
        }
    } else if let Some(email) = rows
        .iter()
        .find(|row| row.id.contains('@'))
        .map(|row| row.id.clone())
    {
        for row in &mut rows {
            if row.account.is_empty() {
                row.account = email.clone();
            }
        }
    }
    Ok(rows)
}

fn exchange_code(
    creds: &ClientCreds,
    code: &str,
    redirect: &str,
    verifier: &str,
) -> Result<Value, String> {
    let form = [
        ("code", code),
        ("client_id", creds.client_id.as_str()),
        ("client_secret", creds.client_secret.as_str()),
        ("redirect_uri", redirect),
        ("grant_type", "authorization_code"),
        ("code_verifier", verifier),
    ];
    let resp = ureq::post(TOKEN_URI)
        .timeout(Duration::from_secs(20))
        .send_form(&form)
        .map_err(|e| match e {
            ureq::Error::Status(code, resp) => {
                let body = resp.into_string().unwrap_or_default();
                format!("troca do código ({code}): {body}")
            }
            other => format!("troca do código: {other}"),
        })?;
    resp.into_json().map_err(|e| e.to_string())
}

fn read_auth_code(listener: &TcpListener) -> Result<String, String> {
    listener.set_nonblocking(true).map_err(|e| e.to_string())?;
    let deadline = Instant::now() + Duration::from_secs(180);
    loop {
        if Instant::now() > deadline {
            return Err(
                "O login no navegador não voltou. Use Adicionar conta de novo.".into(),
            );
        }
        match listener.accept() {
            Ok((mut stream, _)) => {
                stream.set_read_timeout(Some(Duration::from_secs(3))).ok();
                let mut buf = [0u8; 8192];
                let n = stream.read(&mut buf).unwrap_or(0);
                let req = String::from_utf8_lossy(&buf[..n]);
                let path = req
                    .lines()
                    .next()
                    .unwrap_or("")
                    .split_whitespace()
                    .nth(1)
                    .unwrap_or("");
                if let Some(err) = query_param(path, "error") {
                    let _ = stream.write_all(&done_page(
                        "Autorização cancelada. Volte ao Tessera e tente de novo.",
                    ));
                    return Err(format!("Google recusou: {err}"));
                }
                if let Some(code) = query_param(path, "code") {
                    let _ = stream.write_all(&done_page(
                        "Conta ligada. Pode fechar esta janela e voltar ao Tessera.",
                    ));
                    return Ok(code);
                }
                let _ = stream.write_all(b"HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
            }
            Err(err) if err.kind() == ErrorKind::WouldBlock => {
                std::thread::sleep(Duration::from_millis(150));
            }
            Err(err) => return Err(format!("o navegador não voltou: {err}")),
        }
    }
}

fn query_param(path: &str, key: &str) -> Option<String> {
    let query = path.split('?').nth(1)?;
    query.split('&').find_map(|pair| {
        let (name, val) = pair.split_once('=')?;
        (name == key).then(|| pct_decode(val))
    })
}

fn done_page(text: &str) -> Vec<u8> {
    format!(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<!doctype html><meta charset=utf-8><title>Tessera</title><p>{text}</p>"
    )
    .into_bytes()
}

fn open_browser(url: &str) -> Result<(), String> {
    let status = std::process::Command::new("open")
        .arg(url)
        .status()
        .map_err(|e| format!("abrir o navegador: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err("não abri o navegador".into())
    }
}

fn parse_client(raw: &str) -> Result<ClientCreds, String> {
    let value: Value = serde_json::from_str(raw).map_err(|_| need_client_message())?;
    let node = value
        .get("installed")
        .or(value.get("web"))
        .unwrap_or(&value);
    let client_id = node
        .get("client_id")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let client_secret = node
        .get("client_secret")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    if client_id.is_empty() || client_secret.is_empty() {
        return Err(need_client_message());
    }
    Ok(ClientCreds {
        client_id,
        client_secret,
    })
}

fn load_creds(paths: &CalendarPaths, source: ClientSource) -> Result<ClientCreds, String> {
    let raw = match source {
        ClientSource::Tessera => read_tessera_client(paths).ok_or_else(need_tessera_message)?,
        ClientSource::Imported => {
            fs::read_to_string(paths.client_json()).map_err(|_| need_client_message())?
        }
    };
    parse_client(&raw).map_err(|_| match source {
        ClientSource::Tessera => need_tessera_message(),
        ClientSource::Imported => need_client_message(),
    })
}

fn read_tessera_client(paths: &CalendarPaths) -> Option<String> {
    if let Some(path) = std::env::var_os("TESSERA_GOOGLE_OAUTH_JSON") {
        if let Ok(raw) = fs::read_to_string(path) {
            if !raw.trim().is_empty() {
                return Some(raw);
            }
        }
    }
    for path in [
        paths.root.join("tessera-client.json"),
        std::env::current_exe()
            .ok()
            .and_then(|exe| exe.parent().map(|dir| dir.join("../Resources/google-oauth.json")))
            .unwrap_or_else(|| paths.root.join("missing")),
    ] {
        if let Ok(raw) = fs::read_to_string(&path) {
            if !raw.trim().is_empty() {
                return Some(raw);
            }
        }
    }
    None
}

pub fn mark_login_expired(paths: &CalendarPaths) {
    let _ = write_status(
        paths,
        &AuthStatus::Failed {
            message: "A entrada no Google expirou. Abra o Tessera e entre de novo.".into(),
        },
    );
}

fn need_tessera_message() -> String {
    "O login direto ainda não está disponível neste app. Use o JSON.".into()
}

fn need_client_message() -> String {
    "Falta o JSON do Google. No Cloud Console, crie um cliente do tipo App para computador, baixe o arquivo e escolha-o aqui. Isso é uma vez por Mac.".into()
}

fn has_account(paths: &CalendarPaths) -> bool {
    fs::read_dir(paths.accounts_dir())
        .ok()
        .is_some_and(|entries| {
            entries
                .flatten()
                .any(|entry| entry.path().extension().is_some_and(|ext| ext == "json"))
        })
}

fn write_status(paths: &CalendarPaths, status: &AuthStatus) -> Result<(), String> {
    fs::create_dir_all(&paths.root).map_err(|e| e.to_string())?;
    fs::write(
        paths.root.join("auth_status.json"),
        serde_json::to_string(status).map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())
}

fn random_verifier() -> Result<String, String> {
    let mut bytes = [0u8; 32];
    let mut file = fs::File::open("/dev/urandom").map_err(|e| e.to_string())?;
    file.read_exact(&mut bytes).map_err(|e| e.to_string())?;
    Ok(base64url(&bytes))
}

pub(crate) fn code_challenge_s256(verifier: &str) -> String {
    let digest = Sha256::digest(verifier.as_bytes());
    base64url(&digest)
}

fn base64url(bytes: &[u8]) -> String {
    const TABLE: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    let mut out = String::new();
    let mut i = 0;
    while i + 3 <= bytes.len() {
        let n = ((bytes[i] as u32) << 16) | ((bytes[i + 1] as u32) << 8) | bytes[i + 2] as u32;
        out.push(TABLE[((n >> 18) & 63) as usize] as char);
        out.push(TABLE[((n >> 12) & 63) as usize] as char);
        out.push(TABLE[((n >> 6) & 63) as usize] as char);
        out.push(TABLE[(n & 63) as usize] as char);
        i += 3;
    }
    if i < bytes.len() {
        let b0 = bytes[i] as u32;
        let b1 = if i + 1 < bytes.len() {
            bytes[i + 1] as u32
        } else {
            0
        };
        let n = (b0 << 16) | (b1 << 8);
        out.push(TABLE[((n >> 18) & 63) as usize] as char);
        out.push(TABLE[((n >> 12) & 63) as usize] as char);
        if i + 1 < bytes.len() {
            out.push(TABLE[((n >> 6) & 63) as usize] as char);
        }
    }
    out
}

fn pct(value: &str) -> String {
    let mut out = String::new();
    for b in value.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char)
            }
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

fn pct_decode(value: &str) -> String {
    let bytes = value.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            if let Ok(v) =
                u8::from_str_radix(std::str::from_utf8(&bytes[i + 1..i + 3]).unwrap_or(""), 16)
            {
                out.push(v);
                i += 3;
                continue;
            }
        }
        out.push(if bytes[i] == b'+' { b' ' } else { bytes[i] });
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_google_desktop_client_file() {
        let temp = tempfile::tempdir().unwrap();
        let paths = CalendarPaths::isolated(temp.path().join("calendar"));
        import_client_json(
            &paths,
            r#"{"installed":{"client_id":"abc.apps.googleusercontent.com","client_secret":"secret"}}"#,
        )
        .unwrap();
        let raw = fs::read_to_string(paths.client_json()).unwrap();
        assert!(raw.contains("abc.apps.googleusercontent.com"));
        assert!(raw.contains("secret"));
    }

    #[test]
    fn rejects_client_without_id() {
        let temp = tempfile::tempdir().unwrap();
        let paths = CalendarPaths::isolated(temp.path().join("calendar"));
        let err = import_client_json(&paths, r#"{"installed":{}}"#).unwrap_err();
        assert!(err.contains("JSON do Google"));
    }

    #[test]
    fn pkce_challenge_is_stable() {
        let challenge =
            code_challenge_s256("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ012");
        assert!(!challenge.contains('+'));
        assert!(!challenge.contains('/'));
        assert!(!challenge.contains('='));
        assert!(challenge.len() >= 40);
    }
}

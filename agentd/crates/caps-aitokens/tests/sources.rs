use std::fs;

use tessera_caps_aitokens::{CursorCredentials, CursorResolver, CursorSource};

#[test]
fn cursor_resolver_uses_auth_json_before_environment() {
    let temp = tempfile::tempdir_in(std::env::current_dir().unwrap()).unwrap();
    let cursor_dir = temp.path().join(".cursor");
    fs::create_dir_all(&cursor_dir).unwrap();
    fs::write(
        cursor_dir.join("auth.json"),
        r#"{"accessToken":"auth-access","refreshToken":"auth-refresh"}"#,
    )
    .unwrap();

    let resolver = CursorResolver::with_paths(
        temp.path().join("missing-state.vscdb"),
        vec![cursor_dir.join("auth.json")],
    );
    let session = resolver
        .resolve(CursorCredentials {
            token: Some("env-token".into()),
            api_key: None,
        })
        .unwrap();

    assert_eq!(session.source, CursorSource::AuthJson);
    assert_eq!(session.access.as_deref(), Some("auth-access"));
    assert_eq!(session.refresh.as_deref(), Some("auth-refresh"));
}

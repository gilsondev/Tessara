use std::fs;

use tessara_profile::{load_or_init_with_paths, read_config, write_config, ProfilePaths};

#[test]
fn config_roundtrip() {
    let dir = tempfile::tempdir().unwrap();
    let paths = ProfilePaths::from_tessera_root(dir.path());

    let profile = load_or_init_with_paths(&paths).unwrap();
    assert!(profile.config.media);
    assert!(profile.config.focus);
    assert!(profile.config.aitokens);
    assert!(profile.config.calendar);

    let cfg_path = paths.config_toml();
    assert!(cfg_path.is_file());

    let mut updated = read_config(&cfg_path).unwrap();
    updated.media = false;
    write_config(&cfg_path, &updated).unwrap();

    let again = read_config(&cfg_path).unwrap();
    assert!(!again.media);
    assert!(again.focus);
}

#[test]
fn first_run_writes_machine_json_with_uuid() {
    let dir = tempfile::tempdir().unwrap();
    let paths = ProfilePaths::from_tessera_root(dir.path());

    let profile = load_or_init_with_paths(&paths).unwrap();
    assert!(!profile.machine_id.is_empty());
    assert!(!profile.display_name.is_empty());

    let machine_path = paths.machine_json();
    assert!(machine_path.is_file());
    let raw = fs::read_to_string(&machine_path).unwrap();
    assert!(raw.contains(&profile.machine_id));

    let second = load_or_init_with_paths(&paths).unwrap();
    assert_eq!(second.machine_id, profile.machine_id);
    assert_eq!(second.display_name, profile.display_name);
}

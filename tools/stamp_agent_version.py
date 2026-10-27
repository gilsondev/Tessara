#!/usr/bin/env python3
"""Stamp Tessera Agent manifests. The committed source of truth is VERSION."""

import argparse
import json
import re
import sys
from pathlib import Path

SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
OWN_LOCK = {"agentd"}


def die(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(2)


def stamp_workspace_cargo(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    parts = text.split("[workspace.package]", 1)
    if len(parts) != 2:
        die(f"{path} missing [workspace.package]")
    head, rest = parts
    section, sep, after = rest.partition("\n[")
    section, n = re.subn(r'^version = "[^"]+"', f'version = "{version}"', section, count=1, flags=re.M)
    if n != 1:
        die(f"{path} missing workspace version")
    path.write_text(head + "[workspace.package]" + section + sep + after, encoding="utf-8")


def stamp_package_cargo(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    updated, n = re.subn(r'^version = "[^"]+"', f'version = "{version}"', text, count=1, flags=re.M)
    if n != 1:
        die(f"{path} missing version")
    path.write_text(updated, encoding="utf-8")


def stamp_lock(path: Path, version: str, names: set[str]) -> None:
    parts = path.read_text(encoding="utf-8").split("[[package]]\n")
    out = [parts[0]]
    for block in parts[1:]:
        match = re.search(r'^name = "([^"]+)"', block, re.M)
        name = match.group(1) if match else ""
        if name in names or name.startswith("tessera-"):
            block, n = re.subn(
                r'^version = "[^"]+"',
                f'version = "{version}"',
                block,
                count=1,
                flags=re.M,
            )
            if n != 1:
                die(f"{path} package {name} missing version")
        out.append(block)
    path.write_text("[[package]]\n".join(out), encoding="utf-8")


def stamp_json_version(path: Path, version: str, dotted: str) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    cursor = data
    keys = dotted.split(".") if dotted else []
    for key in keys[:-1]:
        cursor = cursor[key]
    if keys:
        cursor[keys[-1]] = version
    else:
        data["version"] = version
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    version = args.version
    if not SEMVER.match(version):
        die(f"invalid version: {version}")
    root = args.root
    stamp_workspace_cargo(root / "agentd" / "Cargo.toml", version)
    stamp_lock(root / "agentd" / "Cargo.lock", version, OWN_LOCK)
    stamp_package_cargo(root / "apps" / "agent-ui" / "src-tauri" / "Cargo.toml", version)
    stamp_lock(root / "apps" / "agent-ui" / "src-tauri" / "Cargo.lock", version, {"agent-ui"})
    stamp_json_version(root / "apps" / "agent-ui" / "package.json", version, "")
    lock_path = root / "apps" / "agent-ui" / "package-lock.json"
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    lock["version"] = version
    lock["packages"][""]["version"] = version
    lock_path.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    stamp_json_version(root / "apps" / "agent-ui" / "src-tauri" / "tauri.conf.json", version, "")


if __name__ == "__main__":
    main()

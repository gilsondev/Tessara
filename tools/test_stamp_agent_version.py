import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAMP = ROOT / "tools" / "stamp_agent_version.py"


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def fixture(root: Path) -> None:
    write(
        root / "agentd" / "Cargo.toml",
        textwrap.dedent(
            """\
            [workspace.package]
            version = "0.1.5"

            [workspace.dependencies]
            serde = { version = "1" }
            """
        ),
    )
    write(
        root / "agentd" / "Cargo.lock",
        textwrap.dedent(
            """\
            [[package]]
            name = "agentd"
            version = "0.1.5"

            [[package]]
            name = "libudev-sys"
            version = "0.1.4"

            [[package]]
            name = "tessera-caps-calendar"
            version = "0.1.5"
            """
        ),
    )
    write(root / "apps" / "agent-ui" / "package.json", '{ "name": "agent-ui", "version": "0.1.5" }\n')
    write(
        root / "apps" / "agent-ui" / "package-lock.json",
        '{ "name": "agent-ui", "version": "0.1.5", "packages": { "": { "name": "agent-ui", "version": "0.1.5" } } }\n',
    )
    write(
        root / "apps" / "agent-ui" / "src-tauri" / "tauri.conf.json",
        '{ "productName": "Tessera", "version": "0.1.5" }\n',
    )
    write(
        root / "apps" / "agent-ui" / "src-tauri" / "Cargo.toml",
        '[package]\nname = "agent-ui"\nversion = "0.1.5"\n',
    )
    write(
        root / "apps" / "agent-ui" / "src-tauri" / "Cargo.lock",
        textwrap.dedent(
            """\
            [[package]]
            name = "agent-ui"
            version = "0.1.5"

            [[package]]
            name = "tao-macros"
            version = "0.1.4"
            """
        ),
    )


class StampTest(unittest.TestCase):
    def test_stamps_agent_manifests_and_leaves_third_party(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fixture(root)
            subprocess.run(
                [sys.executable, str(STAMP), "--root", str(root), "--version", "9.9.9"],
                check=True,
            )
            cargo = (root / "agentd" / "Cargo.toml").read_text(encoding="utf-8")
            self.assertIn('version = "9.9.9"', cargo)
            self.assertIn('version = "1"', cargo)
            lock = (root / "agentd" / "Cargo.lock").read_text(encoding="utf-8")
            self.assertIn('name = "agentd"\nversion = "9.9.9"', lock)
            self.assertIn('name = "tessera-caps-calendar"\nversion = "9.9.9"', lock)
            self.assertIn('name = "libudev-sys"\nversion = "0.1.4"', lock)
            ui_lock = (root / "apps" / "agent-ui" / "src-tauri" / "Cargo.lock").read_text(encoding="utf-8")
            self.assertIn('name = "agent-ui"\nversion = "9.9.9"', ui_lock)
            self.assertIn('name = "tao-macros"\nversion = "0.1.4"', ui_lock)
            self.assertIn('"version": "9.9.9"', (root / "apps" / "agent-ui" / "package.json").read_text(encoding="utf-8"))
            lock_json = (root / "apps" / "agent-ui" / "package-lock.json").read_text(encoding="utf-8")
            self.assertEqual(lock_json.count('"version": "9.9.9"'), 2)
            conf = (root / "apps" / "agent-ui" / "src-tauri" / "tauri.conf.json").read_text(encoding="utf-8")
            self.assertIn('"version": "9.9.9"', conf)

    def test_rejects_a_tag_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fixture(root)
            done = subprocess.run(
                [sys.executable, str(STAMP), "--root", str(root), "--version", "v9.9.9"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(done.returncode, 2)
            self.assertIn("0.1.5", (root / "agentd" / "Cargo.toml").read_text(encoding="utf-8"))

    def test_sources_do_not_hardcode_a_plist_version(self):
        install = (ROOT / "site" / "public" / "install.command").read_text(encoding="utf-8")
        cutover = (ROOT / "tools" / "cutover_agent_v2.sh").read_text(encoding="utf-8")
        service = (ROOT / "apps" / "agent-ui" / "src-tauri" / "src" / "service.rs").read_text(encoding="utf-8")
        self.assertNotIn("<string>0.1</string>", install)
        self.assertIn("$VER", install)
        self.assertNotIn("<string>0.1.0</string>", cutover)
        self.assertIn('VERSION', cutover)
        self.assertNotIn('<string>0.1</string>', service)
        self.assertIn('include_str!("../../../../VERSION")', service)


if __name__ == "__main__":
    unittest.main()

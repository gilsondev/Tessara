# Versão do agente no CI — plano

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O próximo número do Tessera Agent sai dos commits em `main`, e o app publicado usa esse número sem ninguém editar manifests na mão.

**Architecture:** `VERSION` na raiz é a única cópia commitada. O release-please abre o pull request de release e, no merge, cria a tag `agent-vX.Y.Z`. O workflow `agent-release.yml` recusa tag fora de `main`, carimba os manifests só no runner e publica o app. `latest.json` continua sendo escrito pelo CI, em cima de `main`, sem levar o restante da árvore junto.

**Tech Stack:** Python 3 do runner, GitHub Actions, release-please-action v4, Cargo workspace, Tauri 2.

## Global Constraints

- A tag publicada continua `agent-vX.Y.Z` com `X.Y.Z` só de dígitos. Sem pré-release.
- Não criar outra tag `agent-v0.1.5`. Essa release já existe.
- Não editar `site/public/latest.json` na mão. O job `publish-manifest` é o único escritor.
- Não subir a versão de pacotes de terceiros que por acaso também são `0.1.4` (`libudev-sys` em `agentd/Cargo.lock`, `tao-macros` em `apps/agent-ui/src-tauri/Cargo.lock`).
- Firmware, simulador e widgets ficam fora. Este plano não versiona o painel.
- Commits e docs novos em inglês, como em `CONTRIBUTING.md`.

---

### Task 1: Carimbo da versão

**Files:**
- Create: `VERSION`
- Create: `tools/stamp_agent_version.py`
- Create: `tools/test_stamp_agent_version.py`

**Interfaces:**
- Consumes: nada
- Produces: `python3 tools/stamp_agent_version.py --root <dir> --version X.Y.Z` reescreve só os manifests do agente dentro de `<dir>`. Sai 0 quando o número é `^[0-9]+\.[0-9]+\.[0-9]+$`. Sai 2 quando não é. Não imprime o número em outro formato.

- [ ] **Step 1: Write the failing test**

Criar `tools/test_stamp_agent_version.py`:

```python
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


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `python3 tools/test_stamp_agent_version.py`

Expected: FAIL with `No such file` for `tools/stamp_agent_version.py`.

- [ ] **Step 3: Write the stamper**

Criar `VERSION` com exatamente:

```text
0.1.5
```

Criar `tools/stamp_agent_version.py`:

```python
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
        die(f"{path} sem [workspace.package]")
    head, tail = parts
    tail, n = re.subn(r'^version = "[^"]+"', f'version = "{version}"', tail, count=1, flags=re.M)
    if n != 1:
        die(f"{path} sem version do workspace")
    path.write_text(head + "[workspace.package]" + tail, encoding="utf-8")


def stamp_package_cargo(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    updated, n = re.subn(r'^version = "[^"]+"', f'version = "{version}"', text, count=1, flags=re.M)
    if n != 1:
        die(f"{path} sem version")
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
                die(f"{path} pacote {name} sem version")
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
        die(f"versão inválida: {version}")
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
```

- [ ] **Step 4: Run the test and confirm it passes**

Run: `python3 tools/test_stamp_agent_version.py`

Expected: `OK` e 2 testes.

- [ ] **Step 5: Commit**

```bash
git add VERSION tools/stamp_agent_version.py tools/test_stamp_agent_version.py
git commit -m "feat(agent): stamp release manifests from one version"
```

---

### Task 2: Quem instala lê essa versão

**Files:**
- Modify: `site/public/install.command:91-103`
- Modify: `tools/cutover_agent_v2.sh:127-141`
- Modify: `apps/agent-ui/src-tauri/src/service.rs:404-415`
- Modify: `tools/test_stamp_agent_version.py`

**Interfaces:**
- Consumes: arquivo `VERSION` com uma linha `X.Y.Z`
- Produces: o plist de `Tessera Agent.app` usa essa linha. O instalador do site usa o campo `version` de `latest.json`, que o CI já grava.

- [ ] **Step 1: Extend the test**

Acrescentar em `tools/test_stamp_agent_version.py` um teste que lê os três arquivos do repositório real (não do fixture):

```python
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
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `python3 tools/test_stamp_agent_version.py`

Expected: FAIL em `test_sources_do_not_hardcode_a_plist_version`.

- [ ] **Step 3: Read the version at the edges**

Em `site/public/install.command`, trocar o heredoc com aspas simples por um heredoc sem aspas para `$VER` expandir. O `VER` já foi lido de `latest.json` nas linhas 41.

```bash
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>Tessera</string>
  <key>CFBundleIdentifier</key><string>dev.tessera.agentd</string>
  <key>CFBundleName</key><string>Tessera Agent</string>
  <key>CFBundleDisplayName</key><string>Tessera Agent</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VER</string>
  <key>LSUIElement</key><true/>
</dict></plist>
PLIST
```

Em `tools/cutover_agent_v2.sh`, antes do `cat` do Info.plist, ler a raiz do repositório. O script já conhece o checkout: usar o diretório do próprio script.

```bash
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AGENT_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"
```

Trocar o heredoc `'PLIST'` por `PLIST` e a linha da versão por:

```xml
  <key>CFBundleShortVersionString</key><string>$AGENT_VERSION</string>
```

Em `apps/agent-ui/src-tauri/src/service.rs`, ao montar o plist de `wrap_agent_app`, interpolar a constante em vez da string `0.1`:

```rust
const AGENT_VERSION: &str = include_str!("../../../../VERSION");

fn agent_plist() -> String {
    let version = AGENT_VERSION.trim();
    format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>Tessera</string>
  <key>CFBundleIdentifier</key><string>dev.tessera.agentd</string>
  <key>CFBundleName</key><string>Tessera Agent</string>
  <key>CFBundleDisplayName</key><string>Tessera Agent</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>{version}</string>
  <key>LSUIElement</key><true/>
</dict></plist>
"#
    )
}
```

`wrap_agent_app` passa a gravar `agent_plist()` no lugar do literal.

- [ ] **Step 4: Run the test and confirm it passes**

Run: `python3 tools/test_stamp_agent_version.py`

Expected: `OK` e 3 testes.

- [ ] **Step 5: Commit**

```bash
git add site/public/install.command tools/cutover_agent_v2.sh apps/agent-ui/src-tauri/src/service.rs tools/test_stamp_agent_version.py
git commit -m "fix(agent): read the agent version from VERSION and latest.json"
```

---

### Task 3: O release carimba e não reescreve o main

**Files:**
- Modify: `.github/workflows/agent-release.yml`
- Modify: `.github/workflows/agent-build.yml`

**Interfaces:**
- Consumes: `tools/stamp_agent_version.py`, tag `agent-vX.Y.Z` ou input `version` do `workflow_dispatch`
- Produces: o `.app` e o `agentd.tgz` saem com `X.Y.Z`. `publish-manifest` só commita `site/public/latest.json` em `main`.

- [ ] **Step 1: Refuse a tag that is not on main**

Em `agent-release.yml`, logo depois do `actions/checkout@v4` do job `build-mac`:

```yaml
      - name: Tag must be on main
        if: startsWith(github.ref, 'refs/tags/agent-v')
        run: |
          set -euo pipefail
          git fetch origin main
          git merge-base --is-ancestor HEAD origin/main
```

Isso falha o job antes de criar Release. `workflow_dispatch` não entra nesse `if` e continua gerando rascunho.

- [ ] **Step 2: Stamp before the builds**

Depois do step `Resolve version` e antes de `Test agentd`:

```yaml
      - name: Stamp manifests from the release version
        env:
          VER: ${{ steps.ver.outputs.version }}
        run: |
          set -euo pipefail
          FILE="$(tr -d '[:space:]' < VERSION)"
          if [ "$FILE" != "$VER" ]; then
            echo "VERSION ($FILE) diferente da tag ou do input ($VER)" >&2
            exit 1
          fi
          python3 tools/stamp_agent_version.py --root . --version "$VER"
```

O `npm run tauri build` que já existe passa a ver o `tauri.conf.json` carimbado. Não acrescentar outro override.

- [ ] **Step 3: Publish latest.json from main, not from the tag checkout**

No job `publish-manifest`, o checkout ganha `ref: main`:

```yaml
      - uses: actions/checkout@v4
        with:
          ref: main
```

O restante do job permanece: baixa o artifact `latest-json` para `site/public`, commita só esse arquivo e faz `git push origin HEAD:main`. Como o checkout já é `main`, o push não leva commits de uma branch de feature.

- [ ] **Step 4: Stamp the smoke build the same way, without publishing**

Em `agent-build.yml`, depois do checkout e antes de `Test agentd`:

```yaml
      - name: Stamp manifests from VERSION
        run: python3 tools/stamp_agent_version.py --root . --version "$(tr -d '[:space:]' < VERSION)"
```

Esse workflow não cria tag, Release nem `latest.json`.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/agent-release.yml .github/workflows/agent-build.yml
git commit -m "ci(agent): stamp the release on main and keep latest.json isolated"
```

---

### Task 4: O número deixa de ser escolhido na mão

**Files:**
- Create: `release-please-config.json`
- Create: `.release-please-manifest.json`
- Create: `CHANGELOG.md`
- Create: `.github/workflows/release-please.yml`
- Modify: `CONTRIBUTING.md`
- Modify: `docs/guides/agent-v2-dev.md:32-38`

**Interfaces:**
- Consumes: commits `feat:` e `fix:` em `main` depois do `bootstrap-sha`
- Produces: pull request `chore: release X.Y.Z` que altera `VERSION` e `CHANGELOG.md`. O merge cria a tag `agent-vX.Y.Z`, que dispara `agent-release.yml`.

- [ ] **Step 1: Confirm 0.1.5 is already on main**

Run: `git fetch origin main && git merge-base --is-ancestor 84a14a0 origin/main && echo on-main || echo not-on-main`

Expected: `on-main`. Se sair `not-on-main`, abrir o PR de `fix/claude-quota` para `main` e fazer o merge antes de seguir. Não criar tag nova nesse merge.

- [ ] **Step 2: Add release-please**

Criar `release-please-config.json`:

```json
{
  "$schema": "https://raw.githubusercontent.com/googleapis/release-please/main/schemas/config.json",
  "packages": {
    ".": {
      "release-type": "simple",
      "component": "agent",
      "include-component-in-tag": true,
      "include-v-in-tag": true,
      "tag-separator": "-",
      "changelog-path": "CHANGELOG.md",
      "extra-files": ["VERSION"],
      "bootstrap-sha": "84a14a0"
    }
  },
  "changelog-sections": [
    { "type": "feat", "section": "Features" },
    { "type": "fix", "section": "Bug fixes" },
    { "type": "chore", "section": "Miscellaneous", "hidden": true }
  ]
}
```

Se o merge de `84a14a0` tiver criado um commit diferente em `main`, usar o SHA desse merge no `bootstrap-sha`. O SHA precisa ser ancestral de `main`.

Criar `.release-please-manifest.json`:

```json
{
  ".": "0.1.5"
}
```

Criar `CHANGELOG.md` com a release que já saiu, para o próximo PR não reescrever o passado:

```markdown
# Changelog

## [0.1.5](https://github.com/gilsondev/Tessera/releases/tag/agent-v0.1.5) (2026-10-27)

### Features

* Mac calendar, first-run companion, and the 0.1.5 agent app.
```

Criar `.github/workflows/release-please.yml`:

```yaml
name: Release please

on:
  push:
    branches: [main]

permissions:
  contents: write
  pull-requests: write

jobs:
  release-please:
    runs-on: ubuntu-latest
    steps:
      - uses: googleapis/release-please-action@v4
        with:
          config-file: release-please-config.json
          manifest-file: .release-please-manifest.json
```

- [ ] **Step 3: Replace the hand-tag instructions**

Em `docs/guides/agent-v2-dev.md`, substituir a seção `## Release (CI → Pages)` por:

```markdown
## Release (CI → Pages)

Não crie a tag `agent-v*` na mão e não edite `version` nos manifests.

1. Commits em `main` usam `feat:` ou `fix:`.
2. O workflow `release-please.yml` abre um pull request `chore: release X.Y.Z` que atualiza `VERSION` e `CHANGELOG.md`.
3. O merge cria a tag `agent-vX.Y.Z`.
4. `agent-release.yml` carimba os manifests no runner, publica o GitHub Release e grava `site/public/latest.json` em `main`.
5. `pages.yml` publica o site.

Build contínuo, sem publicar: `agent-build.yml`.
```

Em `CONTRIBUTING.md`, na seção `## Pull requests`, acrescentar um item:

```markdown
6. Agent version: do not bump `VERSION`, Cargo, npm, or `tauri.conf.json` in a feature PR. `feat:` and `fix:` on `main` are what open the release PR.
```

- [ ] **Step 4: Check the config names the tag with the agent prefix**

Run: `python3 -c "import json; c=json.load(open('release-please-config.json')); p=c['packages']['.']; assert p['component']=='agent'; assert p['include-component-in-tag'] is True; assert p['include-v-in-tag'] is True; assert p['bootstrap-sha']"`

Expected: o comando termina sem `AssertionError`.

- [ ] **Step 5: Commit**

```bash
git add release-please-config.json .release-please-manifest.json CHANGELOG.md .github/workflows/release-please.yml CONTRIBUTING.md docs/guides/agent-v2-dev.md
git commit -m "ci(agent): open the next release from conventional commits"
```

Depois do merge em `main`, o primeiro push dispara o release-please. Se houver `feat:` ou `fix:` depois do `bootstrap-sha`, o PR de release aparece com a tag `agent-vX.Y.Z` no corpo. Se o corpo mostrar uma tag `vX.Y.Z` sem o prefixo `agent-`, a configuração do componente está errada: parar e corrigir antes de fazer o merge desse PR. Sem esse prefixo, `agent-release.yml` não roda.

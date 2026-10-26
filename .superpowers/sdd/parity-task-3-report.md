# Parity Task 3 report
Status: AITOKENS wire codec, golden fixture, Cursor credential resolution, Claude status/cache reader, Codex auth detection, USB query and 30s push implemented.
Commit: `feat(agentd): AITOKENS parity (Cursor, Claude, Codex)`.
Tests: `cargo test --workspace` passed.
Concerns: Cursor token refresh/live usage API and Codex WHAM usage request remain intentionally unavailable; no quota is synthesized.
Report path: `.superpowers/sdd/parity-task-3-report.md`

Follow-up: `CursorResolver` now appears in `collect_sources`; resolved auth is encoded as unavailable with its source until live quota is ported. Claude multi-surface `a · b` merging remains documented in `agentd/README.md`.

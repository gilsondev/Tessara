"""Linhas USB Tessera (tab-separadas, \\n)."""

from __future__ import annotations


def agent_hello(ok: bool, detail: str = "") -> str:
    if ok:
        return "AGENT\tv1\t1\n"
    reason = (detail or "fail").replace("\t", " ")[:40]
    return f"AGENT\tv1\t0\t{reason}\n"

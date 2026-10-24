from . import claude, codex, cursor


def encode_source(src: dict) -> str:
    return "\t".join(
        [
            src["id"],
            "1" if src.get("available") else "0",
            src.get("kind") or "none",
            str(int(src.get("value") or 0)),
            str(src.get("display") or "—").replace("\t", " "),
            str(src.get("reset") or "").replace("\t", " "),
        ]
    )


def build_line() -> str:
    sources = [codex.read(), claude.read(), cursor.read()]
    ok = any(s.get("available") for s in sources)
    body = "\t".join(encode_source(s) for s in sources)
    return "AITOKENS\tv1\t" + ("1" if ok else "0") + "\t" + body


def any_available() -> bool:
    return any(s.get("available") for s in (codex.read(), claude.read(), cursor.read()))

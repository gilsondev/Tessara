const LATEST = new URL("latest.json", import.meta.env.BASE_URL).href;

async function loadLatest() {
  const note = document.getElementById("version-note");
  const dl = document.getElementById("download");
  const foot = document.getElementById("footer-ver");
  try {
    const r = await fetch(LATEST, { cache: "no-store" });
    if (!r.ok) throw new Error(String(r.status));
    const j = await r.json();
    const ver = j.version || "?";
    if (note) {
      note.textContent = j.released_at
        ? `Versão ${ver} · ${String(j.released_at).slice(0, 10)}`
        : `Versão ${ver}`;
    }
    if (foot) foot.textContent = `Agent ${ver}`;
    const href = j.app_url || j.dmg_url || j.agentd_url;
    if (dl && href) {
      dl.href = href;
      dl.textContent = `Baixar Agent ${ver}`;
    }
  } catch {
    if (note) note.textContent = "Release mais recente no GitHub.";
    if (foot) foot.textContent = "";
  }
}

void loadLatest();

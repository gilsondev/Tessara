const LATEST = new URL("latest.json", document.baseURI).href;

async function loadLatest() {
  const notes = document.querySelectorAll("[data-version]");
  const downloads = document.querySelectorAll("[data-download]");
  const foot = document.getElementById("footer-ver");
  try {
    const r = await fetch(LATEST, { cache: "no-store" });
    if (!r.ok) throw new Error(String(r.status));
    const j = await r.json();
    const ver = j.version || "?";
    const label = j.released_at
      ? `Versão ${ver} · ${String(j.released_at).slice(0, 10)}`
      : `Versão ${ver}`;
    notes.forEach((note) => {
      note.textContent = label;
    });
    if (foot) foot.textContent = `Agent ${ver}`;
    const href = j.app_url || j.dmg_url || j.agentd_url;
    if (href) {
      downloads.forEach((dl) => {
        dl.href = href;
        dl.textContent = `Baixar Agent ${ver}`;
      });
    }
  } catch {
    notes.forEach((note) => {
      note.textContent = "Release mais recente no GitHub.";
    });
    if (foot) foot.textContent = "";
  }
}

function wireCopy() {
  const btn = document.getElementById("copy-cmd");
  const cmd = document.getElementById("install-cmd");
  if (!btn || !cmd) return;
  btn.addEventListener("click", async () => {
    const text = cmd.textContent || "";
    try {
      await navigator.clipboard.writeText(text);
      btn.textContent = "Copiado";
      setTimeout(() => {
        btn.textContent = "Copiar";
      }, 1600);
    } catch {
      btn.textContent = "Selecione e ⌘C";
    }
  });
}

function currentTheme() {
  return document.documentElement.dataset.theme === "dark" ? "dark" : "light";
}

function applyShots(theme) {
  const base = new URL("shots/", document.baseURI);
  document.querySelectorAll("[data-shot]").forEach((img) => {
    img.src = new URL(`${theme}/${img.dataset.shot}.png`, base).href;
  });
}

function wireTheme() {
  const btn = document.getElementById("theme-toggle");
  const paint = (theme) => {
    document.documentElement.dataset.theme = theme;
    localStorage.setItem("tessera-theme", theme);
    applyShots(theme);
    if (btn) {
      const dark = theme === "dark";
      btn.textContent = dark ? "Claro" : "Escuro";
      btn.setAttribute("aria-pressed", dark ? "true" : "false");
    }
  };
  paint(currentTheme());
  if (!btn) return;
  btn.addEventListener("click", () => {
    paint(currentTheme() === "dark" ? "light" : "dark");
  });
}

function wireReels() {
  document.querySelectorAll("[data-reel]").forEach((reel) => {
    const track = reel.querySelector("[data-reel-track]");
    const prev = reel.querySelector("[data-reel-prev]");
    const next = reel.querySelector("[data-reel-next]");
    if (!track) return;
    const distance = () => {
      if (reel.dataset.reel === "page") return Math.max(track.clientWidth * 0.85, 240);
      const item = track.firstElementChild;
      if (!item) return track.clientWidth;
      const gap = parseFloat(getComputedStyle(track).columnGap) || 0;
      return item.getBoundingClientRect().width + gap;
    };
    const sync = () => {
      const max = track.scrollWidth - track.clientWidth - 1;
      reel.classList.toggle("reel-idle", max <= 4);
      if (prev) prev.disabled = track.scrollLeft <= 1;
      if (next) next.disabled = track.scrollLeft >= max;
    };
    prev?.addEventListener("click", () => {
      track.scrollBy({ left: -distance(), behavior: "smooth" });
    });
    next?.addEventListener("click", () => {
      track.scrollBy({ left: distance(), behavior: "smooth" });
    });
    track.addEventListener("scroll", sync, { passive: true });
    if (typeof ResizeObserver !== "undefined") new ResizeObserver(sync).observe(track);
    sync();
  });
}

void loadLatest();
wireCopy();
wireTheme();
wireReels();

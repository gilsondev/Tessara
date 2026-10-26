const screens = [
  { id: "welcome", title: "Bem-vindo", label: "Welcome", tab: null },
  { id: "scan", title: "Varredura", label: "Varredura", tab: null },
  { id: "scan-result", title: "O que achamos", label: "Achados", tab: null },
  { id: "setup", title: "Autorizações", label: "Autorizar", tab: null },
  { id: "setup-cable", title: "Ligar o painel", label: "Cabo", tab: null },
  { id: "home", title: "Início", label: "Início", tab: "home" },
  { id: "panel", title: "Painel", label: "Painel", tab: "panel" },
  { id: "mac", title: "No Mac", label: "No Mac", tab: "mac" },
  { id: "machines", title: "Computadores", label: "PCs", tab: "machines" },
  { id: "activity", title: "Atividade", label: "Atividade", tab: "activity" },
  { id: "doctor", title: "Doctor", label: "Doctor", tab: "activity" },
  { id: "more", title: "Mais", label: "Mais", tab: "more" },
  { id: "problem", title: "Desconectado", label: "Problema", tab: "home" },
];

const title = document.getElementById("win-title");
const tabs = document.getElementById("tabs");

function go(id) {
  const meta = screens.find((s) => s.id === id) || screens.find((s) => s.id === "welcome");
  document.querySelectorAll(".screen").forEach((el) => {
    el.classList.toggle("on", el.dataset.screen === id);
  });
  tabs.querySelectorAll("button").forEach((b) => {
    b.classList.toggle("on", meta.tab && b.dataset.go === meta.tab);
  });
  if (title) title.textContent = meta.title;
  document.title = `Tessara — ${meta.title}`;
  history.replaceState(null, "", "#" + id);
}

document.body.addEventListener("click", (e) => {
  const modeSet = e.target.closest("[data-mode-set]");
  if (modeSet) {
    const screen = modeSet.closest(".screen");
    if (screen) {
      screen.dataset.mode = modeSet.dataset.modeSet;
      const group = modeSet.parentElement;
      if (group) {
        group.querySelectorAll("[data-mode-set]").forEach((b) => b.classList.remove("on"));
        modeSet.classList.add("on");
      }
    }
    return;
  }
  const chip = e.target.closest(".chips button");
  if (chip && chip.parentElement.classList.contains("chips") && !chip.dataset.modeSet) {
    chip.parentElement.querySelectorAll("button").forEach((b) => b.classList.remove("on"));
    chip.classList.add("on");
  }
  const t = e.target.closest("[data-go]");
  if (t) {
    e.preventDefault();
    go(t.dataset.go);
    if (t.dataset.modeOpen) {
      const screen = document.querySelector(`.screen[data-screen="${t.dataset.go}"]`);
      if (screen && screen.dataset.mode !== undefined) {
        screen.dataset.mode = t.dataset.modeOpen;
        screen.querySelectorAll("[data-mode-set]").forEach((b) => {
          b.classList.toggle("on", b.dataset.modeSet === t.dataset.modeOpen);
        });
      }
    }
  }
  const tog = e.target.closest(".toggle");
  if (tog) {
    tog.classList.toggle("on");
    tog.setAttribute("aria-pressed", tog.classList.contains("on"));
  }
});

const hash = location.hash.replace("#", "");
if (hash && screens.some((s) => s.id === hash)) go(hash);

if (!location.hash.replace("#", "")) go("welcome");

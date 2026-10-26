import { fetchStatus, AGENTD_PORT, isAgentdOnline, isUsbUp } from "./ipc.js";

const STATUS_POLL_MS = 2000;
let statusPollId = null;

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

function visibleScreenId() {
  const on = document.querySelector(".screen.on");
  return on?.dataset.screen ?? null;
}

function setBannerVariant(banner, variant) {
  banner.classList.remove("ok", "warn", "err");
  banner.classList.add(variant);
}

function setCheckRow(row, { rowClass, mark, sub, pillText, pillClass }) {
  row.classList.remove("ok", "bad", "warn", "skip");
  row.classList.add(rowClass);
  const markEl = row.querySelector(".mark");
  if (markEl) markEl.textContent = mark;
  const subEl = row.querySelector("[data-live$='-sub']");
  if (subEl && sub != null) subEl.textContent = sub;
  const pill = row.querySelector("[data-live$='-pill']") || row.querySelector(".pill");
  if (pill) {
    pill.classList.remove("warn", "neutral");
    if (pillClass) pill.classList.add(pillClass);
    pill.innerHTML = `<i></i> ${pillText}`;
  }
}

function applyAgentdOffline() {
  const homeBanner = document.querySelector('[data-live="home-status-banner"]');
  if (homeBanner) {
    setBannerVariant(homeBanner, "err");
    homeBanner.querySelector('[data-live="home-status-title"]').textContent = "Abra o agentd";
    homeBanner.querySelector('[data-live="home-status-sub"]').textContent =
      `Não achamos o serviço em 127.0.0.1:${AGENTD_PORT}. Rode mise run agentd-run neste Mac.`;
  }
  const homeActions = document.querySelector('[data-live="home-agentd-actions"]');
  if (homeActions) homeActions.hidden = false;

  const docBanner = document.querySelector('[data-live="doctor-status-banner"]');
  if (docBanner) {
    setBannerVariant(docBanner, "err");
    docBanner.querySelector('[data-live="doctor-status-title"]').textContent = "agentd offline";
    docBanner.querySelector('[data-live="doctor-status-sub"]').textContent =
      "Sem o agentd não dá para checar cabo, sync nem cotas.";
  }
  const usb = document.querySelector('[data-live="doctor-usb-check"]');
  if (usb) {
    setCheckRow(usb, {
      rowClass: "bad",
      mark: "!",
      sub: "agentd offline — não dá para ler o USB agora.",
      pillText: "Offline",
      pillClass: "warn",
    });
  }
  const agentd = document.querySelector('[data-live="doctor-agentd-check"]');
  if (agentd) {
    setCheckRow(agentd, {
      rowClass: "bad",
      mark: "!",
      sub: "Inicie o agentd (mise run agentd-run) e volte aqui.",
      pillText: "Ação",
      pillClass: "warn",
    });
  }
}

function applyAgentdOnline(status) {
  const usbUp = isUsbUp(status);
  const version = status.version ?? "—";

  const homeBanner = document.querySelector('[data-live="home-status-banner"]');
  if (homeBanner) {
    setBannerVariant(homeBanner, usbUp ? "ok" : "warn");
    homeBanner.querySelector('[data-live="home-status-title"]').textContent = usbUp
      ? "Painel ligado a este Mac"
      : "agentd rodando";
    homeBanner.querySelector('[data-live="home-status-sub"]').textContent = usbUp
      ? "Mac do trabalho · música, Focus e cotas atualizando."
      : "Painel sem cabo · USB disconnected.";
  }
  const homeActions = document.querySelector('[data-live="home-agentd-actions"]');
  if (homeActions) homeActions.hidden = true;

  const docBanner = document.querySelector('[data-live="doctor-status-banner"]');
  if (docBanner) {
    if (usbUp) {
      setBannerVariant(docBanner, "ok");
      docBanner.querySelector('[data-live="doctor-status-title"]').textContent = "Tudo certo no cabo";
      docBanner.querySelector('[data-live="doctor-status-sub"]').textContent =
        `agentd ${version} · USB up · demais itens ainda são estáticos no protótipo.`;
    } else {
      setBannerVariant(docBanner, "warn");
      docBanner.querySelector('[data-live="doctor-status-title"]').textContent = "agentd rodando";
      docBanner.querySelector('[data-live="doctor-status-sub"]').textContent =
        "Painel sem cabo · USB disconnected.";
    }
  }
  const usb = document.querySelector('[data-live="doctor-usb-check"]');
  if (usb) {
    if (usbUp) {
      setCheckRow(usb, {
        rowClass: "ok",
        mark: "✓",
        sub: `USB up · agentd ${version}`,
        pillText: "Ok",
        pillClass: null,
      });
    } else {
      setCheckRow(usb, {
        rowClass: "warn",
        mark: "~",
        sub: "agentd ok · nenhum painel no USB agora.",
        pillText: "Aguardando",
        pillClass: "warn",
      });
    }
  }
  const agentd = document.querySelector('[data-live="doctor-agentd-check"]');
  if (agentd) {
    setCheckRow(agentd, {
      rowClass: "ok",
      mark: "✓",
      sub: `agentd ${version} respondeu agora · porta ${AGENTD_PORT}.`,
      pillText: "Ok",
      pillClass: null,
    });
  }
}

async function refreshLiveStatus() {
  if (visibleScreenId() !== "home" && visibleScreenId() !== "doctor") return;
  try {
    const status = await fetchStatus();
    if (!isAgentdOnline(status)) {
      applyAgentdOffline();
      return;
    }
    applyAgentdOnline(status);
  } catch {
    applyAgentdOffline();
  }
}

function ensureStatusPoll() {
  const id = visibleScreenId();
  const want = id === "home" || id === "doctor";
  if (want && statusPollId == null) {
    refreshLiveStatus();
    statusPollId = setInterval(refreshLiveStatus, STATUS_POLL_MS);
  } else if (!want && statusPollId != null) {
    clearInterval(statusPollId);
    statusPollId = null;
  }
}

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
  ensureStatusPoll();
}

document.body.addEventListener("click", (e) => {
  const retry = e.target.closest("[data-action='retry-agentd']");
  if (retry) {
    e.preventDefault();
    refreshLiveStatus();
    return;
  }
  const help = e.target.closest("[data-action='open-agentd-help']");
  if (help) {
    e.preventDefault();
    window.alert(
      "No terminal, na pasta do Tessara:\n\nmise run agentd-run\n\nDepois volte ao Início — atualizamos a cada 2 s.",
    );
    return;
  }
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
else ensureStatusPoll();

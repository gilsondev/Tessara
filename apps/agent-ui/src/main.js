import {
  fetchStatus,
  fetchDoctor,
  fetchLogs,
  postScan,
  AGENTD_PORT,
  isAgentdOnline,
  isUsbUp,
} from "./ipc.js";

const STATUS_POLL_MS = 2000;
const LOG_POLL_MS = 4000;
let statusPollId = null;
let logPollId = null;

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

function activityMode() {
  const screen = document.querySelector('.screen[data-screen="activity"]');
  return screen?.dataset.mode ?? "simple";
}

function setDemoBadge(selector, demo) {
  const el = document.querySelector(selector);
  if (el) el.hidden = !demo;
}

function setBannerVariant(banner, variant) {
  banner.classList.remove("ok", "warn", "err");
  banner.classList.add(variant);
}

function levelVisual(level) {
  const l = String(level ?? "ok").toLowerCase();
  if (l === "bad" || l === "error" || l === "fail") {
    return { rowClass: "bad", mark: "!", pillText: "Ação", pillClass: "warn" };
  }
  if (l === "warn" || l === "partial") {
    return { rowClass: "warn", mark: "~", pillText: "Parcial", pillClass: "warn" };
  }
  if (l === "skip" || l === "na" || l === "n/a") {
    return { rowClass: "skip", mark: "–", pillText: "N/A", pillClass: "neutral" };
  }
  return { rowClass: "ok", mark: "✓", pillText: "Ok", pillClass: null };
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function renderDoctorChecks(checks) {
  const list = document.querySelector('[data-live="doctor-checks"]');
  if (!list || !Array.isArray(checks)) return;

  list.innerHTML = checks
    .map((check) => {
      const vis = levelVisual(check.level);
      const actionBtn =
        check.action === "setup"
          ? `<button class="btn ghost fix" type="button" data-go="setup">Abrir Ajustes</button>`
          : check.action
            ? `<button class="btn ghost fix" type="button" data-go="${escapeHtml(check.action)}">Resolver</button>`
            : "";
      const pillExtra = vis.pillClass ? ` ${vis.pillClass}` : "";
      return `<li class="${vis.rowClass}" data-check-id="${escapeHtml(check.id ?? "")}">
              <span class="mark" aria-hidden="true">${vis.mark}</span>
              <div class="copy">
                <b>${escapeHtml(check.title ?? "")}</b>
                <span data-live="doctor-check-sub">${escapeHtml(check.detail ?? "")}</span>
                ${actionBtn}
              </div>
              <span class="pill${pillExtra}"><i></i> ${vis.pillText}</span>
            </li>`;
    })
    .join("");
}

function applyDoctorBanner(report) {
  const docBanner = document.querySelector('[data-live="doctor-status-banner"]');
  if (!docBanner || !report) return;

  const checks = report.checks ?? [];
  const needsYou = checks.filter((c) => {
    const l = String(c.level ?? "").toLowerCase();
    return l === "bad" || l === "error" || l === "fail";
  }).length;
  const partial = checks.filter((c) => String(c.level ?? "").toLowerCase() === "warn").length;

  let variant = "ok";
  if (needsYou > 0) variant = "warn";
  else if (partial > 0) variant = "warn";

  setBannerVariant(docBanner, variant);
  docBanner.querySelector('[data-live="doctor-status-title"]').textContent =
    report.summary ?? (needsYou ? `${needsYou} coisa(s) precisam de você` : "Tudo certo");
  docBanner.querySelector('[data-live="doctor-status-sub"]').textContent =
    needsYou || partial
      ? "Mac do trabalho · checado agora · veja os itens abaixo."
      : "Mac do trabalho · checado agora · o resto está ok.";
}

function patchDoctorCheck(id, patch) {
  const row = document.querySelector(`[data-check-id="${id}"]`);
  if (!row) return;
  setCheckRow(row, patch);
}

function setCheckRow(row, { rowClass, mark, sub, pillText, pillClass }) {
  row.classList.remove("ok", "bad", "warn", "skip");
  row.classList.add(rowClass);
  const markEl = row.querySelector(".mark");
  if (markEl) markEl.textContent = mark;
  const subEl = row.querySelector("[data-live='doctor-check-sub']") || row.querySelector("[data-live$='-sub']");
  if (subEl && sub != null) subEl.textContent = sub;
  const pill = row.querySelector(".pill");
  if (pill) {
    pill.classList.remove("warn", "neutral");
    if (pillClass) pill.classList.add(pillClass);
    pill.innerHTML = `<i></i> ${pillText}`;
  }
}

function applyAgentdOfflineToDoctor() {
  const docBanner = document.querySelector('[data-live="doctor-status-banner"]');
  if (docBanner) {
    setBannerVariant(docBanner, "err");
    docBanner.querySelector('[data-live="doctor-status-title"]').textContent = "agentd offline";
    docBanner.querySelector('[data-live="doctor-status-sub"]').textContent =
      "Sem o agentd não dá para checar cabo, sync nem cotas.";
  }
  patchDoctorCheck("usb", {
    rowClass: "bad",
    mark: "!",
    sub: "agentd offline — não dá para ler o USB agora.",
    pillText: "Offline",
    pillClass: "warn",
  });
  patchDoctorCheck("agentd", {
    rowClass: "bad",
    mark: "!",
    sub: "Inicie o agentd (mise run agentd-run) e volte aqui.",
    pillText: "Ação",
    pillClass: "warn",
  });
}

function applyAgentdOnlineToDoctor(status) {
  const usbUp = isUsbUp(status);
  const version = status.version ?? "—";

  if (usbUp) {
    patchDoctorCheck("usb", {
      rowClass: "ok",
      mark: "✓",
      sub: `USB up · agentd ${version}`,
      pillText: "Ok",
      pillClass: null,
    });
  } else {
    patchDoctorCheck("usb", {
      rowClass: "warn",
      mark: "~",
      sub: "agentd ok · nenhum painel no USB agora.",
      pillText: "Aguardando",
      pillClass: "warn",
    });
  }
  patchDoctorCheck("agentd", {
    rowClass: "ok",
    mark: "✓",
    sub: `agentd ${version} respondeu agora · porta ${AGENTD_PORT}.`,
    pillText: "Ok",
    pillClass: null,
  });
}

async function refreshDoctorScreen() {
  const { report, demo } = await fetchDoctor();
  setDemoBadge('[data-live="doctor-demo-badge"]', demo);
  renderDoctorChecks(report.checks);
  applyDoctorBanner(report);
  try {
    const status = await fetchStatus();
    if (!isAgentdOnline(status)) {
      applyAgentdOfflineToDoctor();
      return;
    }
    applyAgentdOnlineToDoctor(status);
  } catch {
    applyAgentdOfflineToDoctor();
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
}

function applyAgentdOnline(status) {
  const usbUp = isUsbUp(status);

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
}

async function refreshLiveStatus() {
  if (visibleScreenId() !== "home") return;
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

async function refreshActivityLog() {
  if (visibleScreenId() !== "activity" || activityMode() !== "advanced") return;
  const pre = document.querySelector('[data-live="activity-log"]');
  if (!pre) return;
  const { lines, demo } = await fetchLogs(200);
  setDemoBadge('[data-live="activity-demo-badge"]', demo);
  pre.textContent = lines.join("\n");
}

function ensurePolls() {
  if (statusPollId != null) {
    clearInterval(statusPollId);
    statusPollId = null;
  }
  if (logPollId != null) {
    clearInterval(logPollId);
    logPollId = null;
  }

  const id = visibleScreenId();
  if (id === "home") {
    refreshLiveStatus();
    statusPollId = setInterval(refreshLiveStatus, STATUS_POLL_MS);
  } else if (id === "doctor") {
    refreshDoctorScreen();
    statusPollId = setInterval(refreshDoctorScreen, STATUS_POLL_MS);
  }

  if (id === "activity" && activityMode() === "advanced") {
    refreshActivityLog();
    logPollId = setInterval(refreshActivityLog, LOG_POLL_MS);
  }
}

async function runScanIfNeeded(id) {
  if (id !== "scan") return;
  try {
    await postScan({});
  } catch {
    /* fixtures / offline — UI stays usable */
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
  ensurePolls();
  void runScanIfNeeded(id);
}

document.body.addEventListener("click", (e) => {
  const retry = e.target.closest("[data-action='retry-agentd']");
  if (retry) {
    e.preventDefault();
    if (visibleScreenId() === "doctor") refreshDoctorScreen();
    else refreshLiveStatus();
    return;
  }
  const copyLog = e.target.closest("[data-action='copy-log']");
  if (copyLog) {
    e.preventDefault();
    const pre = document.querySelector('[data-live="activity-log"]');
    if (pre?.textContent) {
      navigator.clipboard?.writeText(pre.textContent).catch(() => {});
    }
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
      if (screen.dataset.screen === "activity") {
        if (logPollId != null) {
          clearInterval(logPollId);
          logPollId = null;
        }
        ensurePolls();
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
        if (logPollId != null) {
          clearInterval(logPollId);
          logPollId = null;
        }
        ensurePolls();
      }
    }
  }
  const tog = e.target.closest(".toggle");
  if (tog) {
    tog.classList.toggle("on");
    tog.setAttribute("aria-pressed", tog.classList.contains("on"));
  }
});

async function bootScreen() {
  const hash = location.hash.replace("#", "");
  if (hash && screens.some((s) => s.id === hash)) {
    go(hash);
    return;
  }
  try {
    const status = await fetchStatus();
    if (status?.setup_completed) {
      go("home");
      return;
    }
  } catch {
    /* agentd offline — show welcome */
  }
  go("welcome");
}

void bootScreen();

if (window.__TAURI__?.event?.listen) {
  window.__TAURI__.event.listen("navigate", (ev) => {
    const id = ev.payload;
    if (typeof id === "string" && screens.some((s) => s.id === id)) {
      go(id);
    }
  });
}

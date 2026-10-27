import {
  fetchStatus,
  fetchDoctor,
  fetchLogs,
  fetchMachines,
  fetchScanLast,
  fetchPanelState,
  postPanelSettings,
  postWidgetSettings,
  postPanelDashboard,
  postPanelRefresh,
  postPanelToken,
  postPanelWeather,
  postCalendarSelection,
  postCalendarAddAccount,
  fetchUpdate,
  postUpdate,
  postScan,
  openPrivacySettings,
  postPanelReboot,
  agentdService,
  panelForceReset,
  AGENTD_PORT,
  isAgentdOnline,
  isUsbUp,
} from "./ipc.js";

/** @type {object|null} */
let lastPanelState = null;
/** Selected widget id on Widgets tab. */
let selectedWidgetId = "";

const WIDGET_NAV_SUB = {
  "org.tessera.greeting": "Boas-vindas",
  "org.tessera.date": "Formato da data",
  "org.tessera.clock": "Fuso e hora",
  "org.tessera.todoist": "Listas e intervalo",
  "org.tessera.weather": "Cidade",
  "org.tessera.aitokens": "Fontes e ordem",
  "org.tessera.calendar": "Contas e calendários",
  "org.tessera.media": "Agora tocando",
  "org.tessera.mac": "Focus do macOS",
};

/** @type {{name:string,latitude:number,longitude:number}|null} */
let weatherPick = null;
let weatherSuggestTimer = 0;

const STATUS_POLL_MS = 2000;
const LOG_POLL_MS = 4000;
let statusPollId = null;
let logPollId = null;
/** Last /v1/status — profile fields come from here, never from HTML placeholders. */
let lastStatus = null;
/** Last scan result while configuring this Mac. */
let lastScan = null;

const SCAN_META = {
  cursor: { title: "Cursor", detail: "Cotas do Cursor neste Mac" },
  claude: { title: "Claude", detail: "Cotas do Claude neste Mac" },
  codex: { title: "Codex / ChatGPT", detail: "Cotas Codex / ChatGPT neste Mac" },
  media: { title: "Música", detail: "Spotify ou Apple Music neste Mac" },
  focus: { title: "Focus", detail: "Modos de concentração do macOS" },
  calendar: { title: "Agenda", detail: "Próximos eventos (Google Calendar)" },
};

const screens = [
  { id: "welcome", title: "Bem-vindo", label: "Welcome", tab: null },
  { id: "scan", title: "Varredura", label: "Varredura", tab: null },
  { id: "scan-result", title: "O que achamos", label: "Achados", tab: null },
  { id: "setup", title: "Autorizações", label: "Autorizar", tab: null },
  { id: "setup-cable", title: "Ligar o painel", label: "Cabo", tab: null },
  { id: "home", title: "Início", label: "Início", tab: "home" },
  { id: "panel", title: "Painel", label: "Painel", tab: "panel" },
  { id: "widgets", title: "Widgets", label: "Widgets", tab: "widgets" },
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

function settingsButton(check, { primary = false } = {}) {
  const pane = check.settings_pane;
  if (!pane) {
    if (check.action) {
      return `<button class="btn ghost fix" type="button" data-go="${escapeHtml(check.action)}">Resolver</button>`;
    }
    return "";
  }
  const cls = primary ? "btn primary fix" : "btn ghost fix";
  return `<button class="${cls}" type="button" data-open-settings="${escapeHtml(pane)}">Abrir Ajustes</button>`;
}

function renderDoctorChecks(checks) {
  const list = document.querySelector('[data-live="doctor-checks"]');
  if (!list || !Array.isArray(checks)) return;

  list.innerHTML = checks
    .map((check) => {
      const vis = levelVisual(check.level);
      const actionBtn = settingsButton(check);
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

/** Setup screen: only checks that still need a Privacy toggle. */
function renderSetupSteps(checks) {
  const list = document.querySelector('[data-live="setup-steps"]');
  if (!list) return;
  const needed = (checks ?? []).filter((c) => {
    const l = String(c.level ?? "").toLowerCase();
    return c.settings_pane && (l === "bad" || l === "warn" || l === "error" || l === "fail");
  });
  if (!needed.length) {
    list.innerHTML = `<li class="done">
              <span class="n">✓</span>
              <div class="copy">
                <b>Autorizações ok</b>
                <span>Nada pendente neste Mac agora</span>
              </div>
              <span class="pill"><i></i> Pronto</span>
            </li>`;
    return;
  }
  list.innerHTML = needed
    .map((check, i) => {
      const vis = levelVisual(check.level);
      const now = i === 0 ? " now" : "";
      return `<li class="${vis.rowClass}${now}" data-check-id="${escapeHtml(check.id ?? "")}">
              <span class="n">${i + 1}</span>
              <div class="copy">
                <b>${escapeHtml(check.title ?? "")}</b>
                <span>${escapeHtml(check.detail ?? "")}</span>
                ${settingsButton(check, { primary: i === 0 })}
              </div>
              <span class="pill ${vis.pillClass || "warn"}"><i></i> ${vis.pillText}</span>
            </li>`;
    })
    .join("");
}

async function refreshSetupScreen() {
  if (visibleScreenId() !== "setup") return;
  const { report } = await fetchDoctor();
  renderSetupSteps(report?.checks);
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
  const name = machineLabel(lastStatus);
  docBanner.querySelector('[data-live="doctor-status-sub"]').textContent =
    needsYou || partial
      ? `${name} · checado agora · veja os itens abaixo.`
      : `${name} · checado agora · o resto está ok.`;
}

function machineLabel(status) {
  const name = status?.machine?.display_name?.trim();
  return name || "Sem nome ainda";
}

function machineInitial(name) {
  const ch = String(name || "?").trim().charAt(0);
  return (ch || "?").toUpperCase();
}

function applyMachineName(status) {
  if (!status?.machine) return;
  lastStatus = status;
  const name = machineLabel(status);
  const set = (sel, text) => {
    const el = document.querySelector(sel);
    if (el) el.textContent = text;
  };
  set('[data-live="home-machine-name"]', name);
  set('[data-live="home-machine-sub"]', "Perfil deste Mac");
  set('[data-live="scan-machine-name"]', name);
  set('[data-live="scan-machine-label"]', name);
}

function applyUsbPill(kind, label) {
  const pill = document.querySelector('[data-live="usb-pill"]');
  const text = document.querySelector('[data-live="usb-pill-label"]');
  if (text) text.textContent = label;
  if (!pill) return;
  pill.classList.remove("ok", "warn", "err");
  if (kind) pill.classList.add(kind);
}

function scanStateUi(state) {
  const s = String(state ?? "").toLowerCase();
  if (s === "ready") return { needClass: "ok", needText: "Pronto", canEnable: true, defaultOn: true };
  if (s === "needs_auth") {
    return { needClass: "", needText: "Pede autorização", canEnable: true, defaultOn: true };
  }
  return { needClass: "skip", needText: "Não achamos", canEnable: false, defaultOn: false };
}

function renderScanFound(found) {
  const list = document.querySelector('[data-live="scan-found"]');
  if (!list) return;
  if (!Array.isArray(found) || !found.length) {
    list.innerHTML = `<li><div class="copy"><span>Nada encontrado ainda — rode a varredura.</span></div></li>`;
    return;
  }
  list.innerHTML = found
    .map((item) => {
      const meta = SCAN_META[item.id] || { title: item.id, detail: "" };
      const ui = scanStateUi(item.state);
      const on = item.enabled !== false && ui.defaultOn;
      const disabled = ui.canEnable ? "" : " disabled style=\"opacity:.45\"";
      const pressed = on && ui.canEnable ? "true" : "false";
      const togOn = on && ui.canEnable ? " on" : "";
      return `<li data-scan-id="${escapeHtml(item.id)}">
              <div class="copy">
                <b>${escapeHtml(meta.title)}</b>
                <span>${escapeHtml(meta.detail)}</span>
                <span class="need${ui.needClass ? ` ${ui.needClass}` : ""}">${ui.needText}</span>
              </div>
              <button class="toggle${togOn}" type="button" aria-pressed="${pressed}" data-scan-toggle="${escapeHtml(item.id)}"${disabled}></button>
            </li>`;
    })
    .join("");

  const needsAuth = found.filter((f) => String(f.state).toLowerCase() === "needs_auth");
  const banner = document.querySelector('[data-live="scan-auth-banner"]');
  if (banner) {
    if (needsAuth.length) {
      banner.hidden = false;
      const titles = needsAuth
        .map((f) => SCAN_META[f.id]?.title || f.id)
        .join(", ");
      const titleEl = banner.querySelector('[data-live="scan-auth-title"]');
      const subEl = banner.querySelector('[data-live="scan-auth-sub"]');
      if (titleEl) {
        titleEl.textContent =
          needsAuth.length === 1
            ? "1 autorização em falta"
            : `${needsAuth.length} autorizações em falta`;
      }
      if (subEl) {
        subEl.textContent = `${titles} — marque Tessera em Ajustes do Mac.`;
      }
    } else {
      banner.hidden = true;
    }
  }
}

function collectScanToggles() {
  return Array.from(document.querySelectorAll("[data-scan-toggle]")).map((btn) => ({
    id: btn.dataset.scanToggle,
    enabled: btn.classList.contains("on") && !btn.disabled,
  }));
}

async function persistScanChoices({ setupCompleted, displayName } = {}) {
  const toggles = collectScanToggles();
  const body = {};
  // Never send found:[] — that used to disable every capability on the daemon.
  if (toggles.length) body.found = toggles;
  if (setupCompleted != null) body.setup_completed = setupCompleted;
  if (displayName) body.display_name = displayName;
  const result = await postScan(body);
  lastScan = result;
  renderScanFound(result.found);
  try {
    const status = await fetchStatus();
    applyMachineName(status);
  } catch {
    /* ignore */
  }
  return result;
}

async function renderMachinesList() {
  const root = document.querySelector('[data-live="machines-list"]');
  if (!root) return;
  try {
    const { machines } = await fetchMachines();
    const list = Array.isArray(machines) ? machines : [];
    if (!list.length) {
      root.innerHTML = `<div class="card"><p class="sub">Nenhum perfil ainda — comece pela varredura.</p></div>`;
      return;
    }
    root.innerHTML = list
      .map((m) => {
        const name = m.display_name?.trim() || "Sem nome ainda";
        const initial = machineInitial(name);
        const pill = m.this_machine
          ? `<span class="pill"><i></i> Este Mac</span>`
          : `<span class="pill neutral"><i></i> Outro</span>`;
        const caps = Array.isArray(m.capabilities)
          ? m.capabilities.filter(Boolean).join(" · ")
          : "";
        const sub = m.this_machine
          ? caps
            ? `Neste Mac · ${caps}`
            : "Perfil do companion neste Mac (não é inventário do painel)"
          : caps
            ? `Outro Mac · ${caps}`
            : "Perfil do companion em outro computador";
        return `<div class="card">
              <div class="machine">
                <div class="avatar work" aria-hidden="true">${escapeHtml(initial)}</div>
                <div class="copy">
                  <h3>${escapeHtml(name)}</h3>
                  <p class="sub">${escapeHtml(sub)}</p>
                </div>
                <div class="meta">${pill}</div>
              </div>
              ${
                m.this_machine
                  ? `<div class="actions" style="margin-top:14px">
                <button class="btn ghost" type="button" data-go="mac">Coleta neste Mac</button>
                <button class="btn ghost" type="button" data-go="scan">Varrer de novo</button>
              </div>`
                  : `<p class="note" style="margin-top:12px">Só leitura aqui — para mudar, abra o Tessera naquele Mac.</p>`
              }
            </div>`;
      })
      .join("");
  } catch {
    root.innerHTML = `<div class="card"><p class="sub">Companion offline — não dá para ler os perfis agora.</p></div>`;
  }
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
  try {
    const status = await fetchStatus();
    lastStatus = status;
    applyMachineName(status);
    applyDoctorBanner(report);
    if (!isAgentdOnline(status)) {
      applyAgentdOfflineToDoctor();
      return;
    }
    applyAgentdOnlineToDoctor(status);
  } catch {
    applyDoctorBanner(report);
    applyAgentdOfflineToDoctor();
  }
}

function setHomePill(sel, level, label) {
  const el = document.querySelector(sel);
  if (!el) return;
  el.classList.remove("neutral", "warn");
  const l = String(level ?? "").toLowerCase();
  if (l === "ok") {
    /* default green pill */
  } else if (l === "warn" || l === "bad" || l === "error" || l === "fail") {
    el.classList.add("warn");
  } else {
    el.classList.add("neutral");
  }
  el.innerHTML = `<i></i> ${escapeHtml(label)}`;
}

function applyHomeCapsFromDoctor(report) {
  const checks = report?.checks ?? [];
  const byId = (id) => checks.find((c) => c.id === id);

  const music = byId("music");
  const mediaSub = document.querySelector('[data-live="home-media-sub"]');
  if (mediaSub) {
    mediaSub.textContent = music?.detail || "Sem dados ainda";
  }
  if (music) {
    const l = String(music.level ?? "").toLowerCase();
    const label =
      l === "ok" ? "Ok" : l === "skip" ? "Off" : l === "warn" ? "Atenção" : "—";
    setHomePill('[data-live="home-media-pill"]', l, label);
  }

  const focus = byId("focus");
  const focusSub = document.querySelector('[data-live="home-focus-sub"]');
  if (focusSub) {
    focusSub.textContent = focus?.detail || "Sem dados ainda";
  }
  if (focus) {
    const l = String(focus.level ?? "").toLowerCase();
    const label =
      l === "ok" ? "Ok" : l === "skip" ? "Off" : l === "warn" ? "Atenção" : "—";
    setHomePill('[data-live="home-focus-pill"]', l, label);
  }
}

function formatUsbAge(secs) {
  const n = Number(secs);
  if (!Number.isFinite(n)) return "sem troca ainda";
  if (n < 5) return "agora";
  if (n < 60) return `há ${n} s`;
  const min = Math.round(n / 60);
  if (min < 60) return `há ${min} min`;
  return `há ${Math.round(min / 60)} h`;
}

function setDaemonResult(text) {
  const el = document.querySelector('[data-live="daemon-result"]');
  if (!el) return;
  if (!text) {
    el.hidden = true;
    el.textContent = "";
    return;
  }
  el.hidden = false;
  el.textContent = text;
}

function controlError(err) {
  const msg = String(err?.message || err || "");
  if (/not allowed by ACL/i.test(msg)) {
    return "Este app ainda não pode controlar o Agent.";
  }
  return msg || "Não consegui.";
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function showAgentButtons(running) {
  const restart = document.querySelector("[data-action='agentd-restart']");
  const stop = document.querySelector("[data-action='agentd-stop']");
  const start = document.querySelector("[data-action='agentd-start']");
  if (restart) restart.hidden = !running;
  if (stop) stop.hidden = !running;
  if (start) start.hidden = running;
}

async function refreshDaemonCard(status) {
  const summary = document.querySelector('[data-live="daemon-summary"]');
  const usb = document.querySelector('[data-live="daemon-usb"]');
  let running = false;
  if (summary) {
    try {
      const report = await agentdService("status");
      running = report?.ok === true;
      summary.textContent = running ? "No ar" : "Parado";
    } catch (err) {
      summary.textContent = controlError(err);
    }
  }
  showAgentButtons(running);
  if (!usb) return;
  if (!status) {
    usb.textContent = "Sem informação do cabo";
    return;
  }
  if (isUsbUp(status)) {
    const age = Number(status.last_usb_age_s);
    const stale = Number.isFinite(age) && age > 60;
    usb.textContent = stale ? `Sem resposta ${formatUsbAge(age)}` : "Conectado";
    return;
  }
  usb.textContent = "Sem cabo";
}

async function waitUntilUsbUp(ms) {
  const start = Date.now();
  while (Date.now() - start < ms) {
    try {
      const status = await fetchStatus();
      lastStatus = status;
      if (isUsbUp(status)) return true;
    } catch {
      /* agent still coming back */
    }
    await sleep(700);
  }
  return false;
}

async function waitForAgent(ms) {
  const start = Date.now();
  while (Date.now() - start < ms) {
    try {
      const status = await fetchStatus();
      lastStatus = status;
      if (isAgentdOnline(status)) return true;
    } catch {
      /* still down */
    }
    await sleep(700);
  }
  return false;
}

async function controlAgent(action) {
  const pending = {
    restart: "Reiniciando o Agent…",
    stop: "Parando o Agent…",
    start: "Iniciando o Agent…",
  };
  setDaemonResult(pending[action] || "Executando…");
  try {
    const reply = await agentdService(action);
    if (action === "stop") {
      setDaemonResult(reply?.ok ? "Agent parado." : reply?.message || "Não consegui parar o Agent.");
      return;
    }
    if (!reply?.ok) {
      setDaemonResult(reply?.message || "Não consegui.");
      return;
    }
    const online = await waitForAgent(12000);
    setDaemonResult(online ? "Agent no ar." : reply?.message || "O Agent não voltou.");
  } catch (err) {
    setDaemonResult(controlError(err));
  } finally {
    refreshLiveStatus();
  }
}

async function rebootPanel() {
  setDaemonResult("Reiniciando o painel…");
  try {
    let reply = null;
    if (isAgentdOnline(lastStatus) && isUsbUp(lastStatus)) {
      reply = await postPanelReboot();
    }
    if (!reply?.ok) {
      const forced = await panelForceReset();
      const back = forced?.ok ? await waitUntilUsbUp(20000) : false;
      setDaemonResult(
        back
          ? "Painel voltou no cabo."
          : forced?.message || "Não consegui reiniciar o painel.",
      );
      return;
    }
    if (reply.method === "dtr") {
      setDaemonResult("A tela não caiu. O painel ignorou o restart.");
      return;
    }
    const back = await waitUntilUsbUp(20000);
    setDaemonResult(
      back ? "Painel reiniciou e voltou no cabo." : "O painel caiu e o cabo ainda não voltou.",
    );
  } catch (err) {
    setDaemonResult(controlError(err));
  } finally {
    refreshLiveStatus();
  }
}

function applyAgentdOffline() {
  applyUsbPill("err", "Painel: desconhecido");
  const homeBanner = document.querySelector('[data-live="home-status-banner"]');
  if (homeBanner) {
    homeBanner.hidden = false;
    setBannerVariant(homeBanner, "err");
    homeBanner.querySelector('[data-live="home-status-title"]').textContent =
      "Companion desligado";
    homeBanner.querySelector('[data-live="home-status-sub"]').textContent =
      "Sem o Tessera em segundo plano não dá para saber se o painel está no cabo.";
  }
  const homeActions = document.querySelector('[data-live="home-agentd-actions"]');
  if (homeActions) homeActions.hidden = false;
  void refreshDaemonCard(null);
}

function applyAgentdOnline(status) {
  const usbUp = isUsbUp(status);
  const age = Number(status?.last_usb_age_s);
  const stale = usbUp && Number.isFinite(age) && age > 60;

  applyUsbPill(
    stale ? "warn" : usbUp ? "ok" : "warn",
    stale
      ? "Painel: sem resposta"
      : usbUp
        ? "Painel: conectado (USB)"
        : "Painel: desconectado",
  );

  const greet = document.querySelector('[data-live="home-greeting"]');
  if (greet) {
    greet.textContent = stale
      ? "O cabo parou de responder."
      : usbUp
        ? "Tudo certo por aqui."
        : "Falta o cabo USB.";
  }
  const greetSub = document.querySelector('[data-live="home-greeting-sub"]');
  if (greetSub) {
    greetSub.textContent = stale
      ? `Última troca ${formatUsbAge(age)}. O processo pode estar preso.`
      : usbUp
        ? "Música, Focus e cotas sincronizam com o painel."
        : "Companion ok — conecte o USB para sincronizar.";
  }

  // Pill já mostra o USB; banner só quando precisa de ação.
  const homeBanner = document.querySelector('[data-live="home-status-banner"]');
  if (homeBanner) {
    if (stale) {
      homeBanner.hidden = false;
      setBannerVariant(homeBanner, "warn");
      homeBanner.querySelector('[data-live="home-status-title"]').textContent =
        "O Agent parou de falar com o painel";
      homeBanner.querySelector('[data-live="home-status-sub"]').textContent =
        "Reinicie o Agent ou o painel. O processo ainda pode aparecer no ar.";
    } else if (usbUp) {
      homeBanner.hidden = true;
    } else {
      homeBanner.hidden = false;
      setBannerVariant(homeBanner, "warn");
      homeBanner.querySelector('[data-live="home-status-title"]').textContent =
        "Painel sem cabo USB";
      homeBanner.querySelector('[data-live="home-status-sub"]').textContent =
        "Conecte o cabo para sincronizar música, Focus e cotas.";
    }
  }
  applyMachineName(status);
  const homeActions = document.querySelector('[data-live="home-agentd-actions"]');
  if (homeActions) homeActions.hidden = true;
  void refreshDaemonCard(status);
  void refreshHomeCaps();
}

async function refreshHomeCaps() {
  if (visibleScreenId() !== "home") return;
  try {
    const { report } = await fetchDoctor();
    applyHomeCapsFromDoctor(report);
  } catch {
    /* ignore */
  }
}

function setCapToggle(id, on, detail) {
  const btn = document.querySelector(`[data-cap-toggle="${id}"]`);
  if (btn) {
    btn.classList.toggle("on", !!on);
    btn.setAttribute("aria-pressed", on ? "true" : "false");
  }
  const sub = document.querySelector(`[data-live="mac-${id}-sub"]`);
  if (sub && detail) sub.textContent = detail;
}

function paintUpdateStatus(st) {
  const sub = document.querySelector('[data-live="update-sub"]');
  const tog = document.querySelector("[data-update-auto]");
  if (tog) {
    const on = !!st?.auto_update;
    tog.classList.toggle("on", on);
    tog.setAttribute("aria-pressed", on ? "true" : "false");
  }
  if (!sub) return;
  const cur = st?.current_version || "?";
  if (st?.applying) {
    sub.textContent = `Atualizando para ${st.latest_version || "…"}…`;
  } else if (st?.update_available) {
    sub.textContent = st.auto_update
      ? `v${cur} → ${st.latest_version} (aplicando sozinho)`
      : `Nova: ${st.latest_version} (atual: v${cur})`;
  } else if (st?.last_error) {
    sub.textContent = `v${cur} · ${st.last_error}`;
  } else {
    sub.textContent = `v${cur} · você está na versão mais recente`;
  }
}

async function refreshUpdateRow() {
  if (visibleScreenId() !== "more") return;
  try {
    const st = await fetchUpdate();
    paintUpdateStatus(st);
  } catch {
    const sub = document.querySelector('[data-live="update-sub"]');
    if (sub) sub.textContent = "Companion offline — não dá para checar updates.";
  }
}

async function refreshMacScreen() {
  if (visibleScreenId() !== "mac") return;
  try {
    const [scan, { report }] = await Promise.all([fetchScanLast(), fetchDoctor()]);
    const found = scan?.found ?? [];
    const item = (id) => found.find((f) => f.id === id);
    const media = item("media");
    const focus = item("focus");
    const calendar = item("calendar");

    const musicCheck = report?.checks?.find((c) => c.id === "music");
    const focusCheck = report?.checks?.find((c) => c.id === "focus");

    setCapToggle(
      "media",
      !!media?.enabled && media.state !== "missing",
      musicCheck?.detail ||
        (media?.state === "ready"
          ? "Spotify ou Apple Music detectado"
          : "Nenhum app de música encontrado"),
    );
    setCapToggle(
      "focus",
      !!focus?.enabled && focus.state !== "missing",
      focusCheck?.detail || "Modo de concentração do macOS",
    );
    setCapToggle(
      "calendar",
      !!calendar?.enabled && calendar.state !== "missing",
      "Próximos eventos (Google Calendar)",
    );

    for (const id of ["cursor", "claude", "codex"]) {
      const it = item(id);
      const ready = it && String(it.state).toLowerCase() !== "missing";
      const label =
        id === "cursor"
          ? "Cotas Cursor"
          : id === "claude"
            ? "Cotas Claude"
            : "Cotas Codex / ChatGPT";
      setCapToggle(id, !!(it?.enabled && ready), ready ? label : "Não achamos neste Mac");
      const btn = document.querySelector(`[data-cap-toggle="${id}"]`);
      if (btn) btn.disabled = !ready;
    }
  } catch {
    /* keep last */
  }
}

function drawPanelWidgetsBoard(state) {
  const board = document.querySelector('[data-live="panel-widgets-board"]');
  const note = document.querySelector('[data-live="panel-widgets-note"]');
  if (!board) return;
  const widgets = Array.isArray(state?.widgets) ? state.widgets : [];
  if (!state?.reachable || !state?.ok) {
    board.innerHTML = `<li class="empty"><span class="board-name">Painel offline — não dá para mudar a home.</span></li>`;
    if (note) note.textContent = "Ligue o Wi‑Fi do painel para ativar ou ocultar widgets.";
    return;
  }
  if (!widgets.length) {
    board.innerHTML = `<li class="empty"><span class="board-name">Nenhum widget registrado.</span></li>`;
    return;
  }
  const shownN = widgets.filter((w) => w.shown).length;
  if (note) {
    note.textContent = `${shownN} de ${widgets.length} na home. Ajustes de cada um: aba Widgets.`;
  }
  board.innerHTML = widgets
    .map((w, i) => {
      const off = w.shown ? "" : " off";
      const eyeOn = w.shown ? " on" : "";
      const eyeLabel = w.shown ? "Na home" : "Oculto";
      return `<li class="${off.trim()}" data-widget-id="${escapeHtml(w.id)}">
        <span class="board-idx">${i + 1}</span>
        <span class="board-name">${escapeHtml(w.title || w.id)}</span>
        <button type="button" class="board-eye${eyeOn}" data-widget-toggle="${escapeHtml(w.id)}" aria-pressed="${w.shown ? "true" : "false"}">${eyeLabel}</button>
      </li>`;
    })
    .join("");
}

function wsValue(state, widgetId, key, fallback) {
  const ws = state?.widget_settings?.[widgetId];
  if (ws && ws[key] != null) return ws[key];
  const ds = state?.device_settings;
  if (ds && ds[key] != null) return ds[key];
  return fallback;
}

function datePreview(fmt) {
  const d = new Date();
  const w = ["dom", "seg", "ter", "qua", "qui", "sex", "sáb"][d.getDay()];
  const W = ["Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"][d.getDay()];
  const m = ["jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"][
    d.getMonth()
  ];
  const M = [
    "janeiro",
    "fevereiro",
    "março",
    "abril",
    "maio",
    "junho",
    "julho",
    "agosto",
    "setembro",
    "outubro",
    "novembro",
    "dezembro",
  ][d.getMonth()];
  const dd = d.getDate();
  const mm = d.getMonth() + 1;
  const yy = d.getFullYear();
  const p2 = (n) => String(n).padStart(2, "0");
  if (fmt === "dmy") return `${p2(dd)}/${p2(mm)}/${yy}`;
  if (fmt === "ymd") return `${yy}-${p2(mm)}-${p2(dd)}`;
  if (fmt === "mdy") return `${p2(mm)}/${p2(dd)}/${yy}`;
  if (fmt === "long_pt") return `${W}, ${dd} de ${M}`;
  return `${w.charAt(0).toUpperCase()}${w.slice(1)}, ${dd} de ${m}`;
}

function timePreview(fmt, tzKey) {
  const zones = {
    sao_paulo: "America/Sao_Paulo",
    utc: "UTC",
    new_york: "America/New_York",
    los_angeles: "America/Los_Angeles",
    london: "Europe/London",
    lisbon: "Europe/Lisbon",
    buenos_aires: "America/Argentina/Buenos_Aires",
    mexico_city: "America/Mexico_City",
  };
  const tz = zones[tzKey] || "America/Sao_Paulo";
  try {
    return new Intl.DateTimeFormat("pt-BR", {
      timeZone: tz,
      hour: "2-digit",
      minute: "2-digit",
      second: fmt === "hms" ? "2-digit" : undefined,
      hour12: false,
    }).format(new Date());
  } catch {
    const d = new Date();
    const p2 = (n) => String(n).padStart(2, "0");
    const base = `${p2(d.getHours())}:${p2(d.getMinutes())}`;
    return fmt === "hms" ? `${base}:${p2(d.getSeconds())}` : base;
  }
}

function updateDateTimePreviews(state) {
  const fmt = wsValue(state, "org.tessera.date", "date_format", "short_pt");
  const dateEl = document.querySelector('[data-live="widgets-date-preview"]');
  if (dateEl) dateEl.textContent = `Exemplo agora: ${datePreview(String(fmt))}`;
  const tz = wsValue(state, "org.tessera.clock", "timezone", "sao_paulo");
  const tf = wsValue(state, "org.tessera.clock", "time_format", "hm");
  const timeEl = document.querySelector('[data-live="widgets-time-preview"]');
  if (timeEl) timeEl.textContent = `Exemplo agora: ${timePreview(String(tf), String(tz))}`;
}

function paintWsChips(state) {
  document.querySelectorAll("[data-ws-chips]").forEach((root) => {
    const widgetId = root.dataset.wsChips;
    const key = root.dataset.wsKey;
    let opts = [];
    try {
      opts = JSON.parse(root.dataset.wsOpts || "[]");
    } catch {
      opts = [];
    }
    if (!opts.length) return;
    const cur = wsValue(state, widgetId, key, opts[0][0]);
    const strMode = opts.every((o) => typeof o[0] === "string" && Number.isNaN(Number(o[0])));
    let best = 0;
    if (strMode) {
      opts.forEach((o, i) => {
        if (String(cur) === String(o[0])) best = i;
      });
    } else {
      const n = Number(cur) || 0;
      let gap = Infinity;
      opts.forEach((o, i) => {
        const g = Math.abs(Number(o[0]) - n);
        if (g < gap) {
          gap = g;
          best = i;
        }
      });
    }
    root.innerHTML = "";
    opts.forEach((o, i) => {
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = key === "date_format" ? datePreview(String(o[0])) : o[1];
      if (i === best) b.className = "on";
      b.dataset.wsSet = JSON.stringify({ id: widgetId, key, value: o[0] });
      root.appendChild(b);
    });
  });
}

function paintAiToggles(state) {
  const ds = state?.device_settings || {};
  const map = [
    ["show_aitokens_codex", "codex"],
    ["show_aitokens_claude", "claude"],
    ["show_aitokens_cursor", "cursor"],
  ];
  map.forEach(([key, capId]) => {
    const on = typeof ds[key] === "boolean" ? ds[key] : true;
    const btn = document.querySelector(`[data-panel-ai="${key}"]`);
    if (btn) {
      btn.classList.toggle("on", on);
      btn.setAttribute("aria-pressed", on ? "true" : "false");
    }
    const cap = document.querySelector(`[data-cap-toggle="${capId}"]`);
    if (cap && !cap.disabled) {
      cap.classList.toggle("on", on);
      cap.setAttribute("aria-pressed", on ? "true" : "false");
    }
  });
}

function paintCalendarTile(state) {
  const widgetId = "org.tessera.calendar";
  const ws = state?.widget_settings?.[widgetId] || {};
  const size = String(ws.home_tile_size || "M").toUpperCase();
  const chips = document.querySelector(`[data-tile-size="${widgetId}"]`);
  if (!chips) return;
  chips.querySelectorAll("[data-tile-value]").forEach((b) => {
    b.classList.toggle("on", b.dataset.tileValue === size);
    b.disabled = !(state?.reachable && state?.ok);
  });
}

function paintCalendarList(state) {
  const box = document.querySelector('[data-live="widgets-calendar-list"]');
  if (!box) return;
  const cal = state?.calendar || {};
  const rows = (cal.calendars || []).filter(
    (r) => r && r.id && String(r.account || "").includes("@"),
  );
  if (!rows.length) {
    box.innerHTML = `<p class="note">Nenhum calendário ainda. Adicione uma conta Google.</p>`;
    return;
  }
  const by = {};
  const order = [];
  rows.forEach((r) => {
    const a = r.account;
    if (!by[a]) {
      by[a] = [];
      order.push(a);
    }
    by[a].push(r);
  });
  box.innerHTML = order
    .map((acc) => {
      const items = by[acc]
        .map((r) => {
          const color = String(r.color || "1A73E8").replace(/^#/, "");
          return `<label class="cap" style="margin-top:8px">
            <div class="copy" style="display:flex;align-items:center;gap:8px">
              <i style="display:inline-block;width:10px;height:10px;border-radius:50%;background:#${escapeHtml(color)};flex:none"></i>
              <h3 style="margin:0;font-size:14px">${escapeHtml(r.name || r.id)}</h3>
            </div>
            <input type="checkbox" data-cal-id="${escapeHtml(r.id)}" ${r.selected ? "checked" : ""} />
          </label>`;
        })
        .join("");
      return `<div class="agenda-acct"><h4>${escapeHtml(acc)}</h4>${items}</div>`;
    })
    .join("");
}

async function saveAgendaSelection() {
  const ids = [
    ...document.querySelectorAll('[data-live="widgets-calendar-list"] input[data-cal-id]:checked'),
  ].map((i) => i.dataset.calId);
  const state = await postCalendarSelection(ids.join(","));
  applyWidgetsScreen(state);
}

function setSelectedWidget(id) {
  selectedWidgetId = id || "";
  const screen = document.querySelector('.screen[data-screen="widgets"]');
  if (screen) screen.dataset.widgetPage = selectedWidgetId;
  document.querySelectorAll("[data-live='widgets-nav'] .nav").forEach((b) => {
    b.classList.toggle("on", b.dataset.widgetNav === selectedWidgetId);
  });
  document.querySelectorAll(".widget-page").forEach((page) => {
    page.classList.toggle("on", page.dataset.widgetPageId === selectedWidgetId);
  });
}

function applyWidgetsScreen(state) {
  lastPanelState = state;
  const reachable = !!(state?.reachable && state?.ok);
  const offline = document.querySelector('[data-live="widgets-offline-banner"]');
  const empty = document.querySelector('[data-live="widgets-empty-banner"]');
  const shell = document.querySelector('[data-live="widgets-shell"]');
  const nav = document.querySelector('[data-live="widgets-nav"]');
  if (offline) offline.hidden = reachable;
  const widgets = Array.isArray(state?.widgets) ? state.widgets : [];
  const active = reachable ? widgets.filter((w) => w.shown) : [];
  if (empty) empty.hidden = !(reachable && active.length === 0);
  if (shell) shell.hidden = !reachable || active.length === 0;
  if (!nav) return;

  if (!reachable || !active.length) {
    nav.innerHTML = "";
    setSelectedWidget("");
    return;
  }

  nav.innerHTML = active
    .map((w) => {
      const sub = WIDGET_NAV_SUB[w.id] || "Ajustes";
      return `<button type="button" class="nav" data-widget-nav="${escapeHtml(w.id)}">
        <b>${escapeHtml(w.title || w.id)}</b><span>${escapeHtml(sub)}</span>
      </button>`;
    })
    .join("");

  const stillThere = active.some((w) => w.id === selectedWidgetId);
  if (!stillThere) {
    const narrow =
      typeof window.matchMedia === "function" &&
      window.matchMedia("(max-width: 719px)").matches;
    selectedWidgetId = narrow ? "" : active[0].id;
  }
  setSelectedWidget(selectedWidgetId);

  paintWsChips(state);
  paintAiToggles(state);
  paintCalendarTile(state);
  paintCalendarList(state);
  updateDateTimePreviews(state);

  const weatherNote = document.querySelector('[data-live="widgets-weather-note"]');
  const weatherInput = document.querySelector("[data-weather-city]");
  if (weatherNote) {
    const city = state.weather?.city;
    weatherNote.textContent = city
      ? state.weather?.ready
        ? `Previsão fresca · ${city}`
        : `Cidade: ${city} · aguardando previsão`
      : "Defina a cidade para ver o clima na home.";
  }
  if (weatherInput && !weatherInput.value && state.weather?.city) {
    weatherInput.value = state.weather.city;
  }
  const todoNote = document.querySelector('[data-live="widgets-todoist-token"]');
  if (todoNote) {
    todoNote.textContent = state.token_set
      ? "Token configurado neste painel. Cole um novo abaixo só para trocar."
      : "Cole o token da API do Todoist (Ajustes → Integrações → Desenvolvedor).";
  }
  const calNote = document.querySelector('[data-live="widgets-calendar-note"]');
  if (calNote) {
    const cal = state.calendar || {};
    if (cal.auth_pending) calNote.textContent = "Autorize no browser do Mac.";
    else if (cal.linked)
      calNote.textContent = cal.ready
        ? "Contas ligadas · eventos ok."
        : "Contas ligadas · aguardando eventos.";
    else calNote.textContent = "Nenhuma conta ainda — toque em Adicionar conta.";
  }
  const mediaStatus = document.querySelector('[data-live="widgets-media-status"]');
  if (mediaStatus) {
    mediaStatus.textContent =
      "Sem ajustes de layout — liga/desliga a coleta na aba No Mac. O tile mostra o que toca agora.";
  }
  const focusStatus = document.querySelector('[data-live="widgets-focus-status"]');
  if (focusStatus) {
    focusStatus.textContent =
      "Sem opções de aparência — a coleta do Focus fica na aba No Mac.";
  }
}

async function refreshWidgetsScreen() {
  if (visibleScreenId() !== "widgets") return;
  try {
    const state = await fetchPanelState();
    applyWidgetsScreen(state);
  } catch (err) {
    applyWidgetsScreen({
      ok: false,
      reachable: false,
      error: String(err?.message ?? err),
      widgets: [],
    });
  }
}

async function persistMacCaps() {
  const on = (id) =>
    document.querySelector(`[data-cap-toggle="${id}"]`)?.classList.contains("on");
  const body = {
    found: [
      { id: "media", enabled: !!on("media") },
      { id: "focus", enabled: !!on("focus") },
      { id: "calendar", enabled: !!on("calendar") },
      { id: "cursor", enabled: !!on("cursor") },
      { id: "claude", enabled: !!on("claude") },
      { id: "codex", enabled: !!on("codex") },
    ],
  };
  await postScan(body);
  // Cotas No Mac ↔ fontes no widget AI Tokens (painel).
  try {
    await postPanelSettings({
      show_aitokens_cursor: !!on("cursor"),
      show_aitokens_claude: !!on("claude"),
      show_aitokens_codex: !!on("codex"),
    });
  } catch {
    /* painel offline — coleta Mac já salva */
  }
  await refreshMacScreen();
  await refreshHomeCaps();
  if (visibleScreenId() === "widgets") void refreshWidgetsScreen();
}

function renderActivityChecks(checks) {
  const list = document.querySelector('[data-live="activity-checks"]');
  if (!list) return;
  if (!Array.isArray(checks) || !checks.length) {
    list.innerHTML = `<li><div class="copy"><span>Nada para mostrar ainda.</span></div></li>`;
    return;
  }
  list.innerHTML = checks
    .map((check) => {
      const vis = levelVisual(check.level);
      const pillExtra = vis.pillClass ? ` ${vis.pillClass}` : "";
      return `<li class="${vis.rowClass}">
              <span class="mark" aria-hidden="true">${vis.mark}</span>
              <div class="copy">
                <b>${escapeHtml(check.title ?? "")}</b>
                <span>${escapeHtml(check.detail ?? "")}</span>
              </div>
              <span class="pill${pillExtra}"><i></i> ${vis.pillText}</span>
            </li>`;
    })
    .join("");
}

function friendlyActivityLine(raw) {
  const line = String(raw ?? "");
  const m = line.match(/^(\d{2}:\d{2}:\d{2})\s+(INFO|WARN)\s+(.*)$/);
  if (!m) return { when: "", text: line };
  let text = m[3];
  // Legacy: usb Mac->painel widget=… action=… RAW…
  const legacy = text.match(
    /^usb (Mac->painel|painel->Mac) widget=(\S+) action=(\S+)\s*(.*)$/,
  );
  if (legacy) {
    const dir = legacy[1] === "Mac->painel" ? "Mac → painel" : "Painel → Mac";
    const rest = (legacy[4] || "").trim();
    text = rest
      ? `${dir} · ${legacy[2]} · ${legacy[3]} · ${rest}`
      : `${dir} · ${legacy[2]} · ${legacy[3]}`;
  } else {
    text = text
      .replace(/^usb Mac→painel · /, "Mac → painel · ")
      .replace(/^usb painel→Mac · /, "Painel → Mac · ")
      .replace(/^usb: /, "")
      .replace(/^panel Mac→painel /, "Ajuste no painel · ")
      .replace(/^agentd: /, "");
  }
  return { when: m[1], text };
}

function renderActivityFeed(lines) {
  const list = document.querySelector('[data-live="activity-feed"]');
  if (!list) return;
  const usable = (lines || [])
    .map(friendlyActivityLine)
    .filter((x) => x.text && !/AGENT heartbeat/i.test(x.text))
    .slice(-40)
    .reverse();
  if (!usable.length) {
    list.innerHTML = `<li><div class="copy"><span>Ainda sem eventos. Conecte o painel ou mude um widget.</span></div></li>`;
    return;
  }
  list.innerHTML = usable
    .map(
      (x) => `<li>
      <div class="when">${escapeHtml(x.when || "agora")}</div>
      <div class="copy">${escapeHtml(x.text)}</div>
    </li>`,
    )
    .join("");
}

async function refreshActivitySimple() {
  if (visibleScreenId() !== "activity" || activityMode() !== "simple") return;
  try {
    const { lines } = await fetchLogs(120);
    renderActivityFeed(lines);
  } catch {
    renderActivityFeed([]);
  }
}

async function refreshLiveStatus({ homeOnly = false } = {}) {
  const onHome = visibleScreenId() === "home";
  if (homeOnly && !onHome) return;
  try {
    const status = await fetchStatus();
    lastStatus = status;
    if (!isAgentdOnline(status)) {
      if (onHome) applyAgentdOffline();
      else applyUsbPill("err", "Painel: desconhecido");
      return;
    }
    if (onHome) {
      applyAgentdOnline(status);
    } else {
      const usbUp = isUsbUp(status);
      applyUsbPill(
        usbUp ? "ok" : "warn",
        usbUp ? "Painel: conectado (USB)" : "Painel: desconectado",
      );
      applyMachineName(status);
    }
  } catch {
    if (onHome) applyAgentdOffline();
    else applyUsbPill("err", "Painel: desconhecido");
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
  void refreshLiveStatus();

  if (id === "doctor") {
    refreshDoctorScreen();
    statusPollId = setInterval(() => {
      void refreshLiveStatus();
      void refreshDoctorScreen();
    }, STATUS_POLL_MS);
  } else if (id === "setup") {
    refreshSetupScreen();
    statusPollId = setInterval(() => {
      void refreshLiveStatus();
      void refreshSetupScreen();
    }, STATUS_POLL_MS);
  } else if (id === "panel") {
    void refreshPanelScreen();
    statusPollId = setInterval(() => {
      void refreshLiveStatus();
      void refreshPanelScreen();
    }, STATUS_POLL_MS);
  } else if (id === "widgets") {
    void refreshWidgetsScreen();
    statusPollId = setInterval(() => {
      void refreshLiveStatus();
      void refreshWidgetsScreen();
    }, STATUS_POLL_MS);
  } else if (id === "mac") {
    void refreshMacScreen();
    statusPollId = setInterval(() => {
      void refreshLiveStatus();
      void refreshMacScreen();
    }, STATUS_POLL_MS);
  } else if (id === "machines") {
    void renderMachinesList();
    statusPollId = setInterval(() => void refreshLiveStatus(), STATUS_POLL_MS);
  } else if (id === "more") {
    void refreshUpdateRow();
    statusPollId = setInterval(() => {
      void refreshLiveStatus();
      void refreshUpdateRow();
    }, STATUS_POLL_MS);
  } else if (id === "scan-result") {
    void refreshScanResultScreen();
    statusPollId = setInterval(() => void refreshLiveStatus(), STATUS_POLL_MS);
  } else if (id === "activity" && activityMode() === "simple") {
    void refreshActivitySimple();
    statusPollId = setInterval(() => {
      void refreshLiveStatus();
      void refreshActivitySimple();
    }, STATUS_POLL_MS);
  } else {
    statusPollId = setInterval(() => void refreshLiveStatus(), STATUS_POLL_MS);
  }

  if (id === "activity" && activityMode() === "advanced") {
    refreshActivityLog();
    logPollId = setInterval(refreshActivityLog, LOG_POLL_MS);
  }
}

async function refreshScanResultScreen() {
  try {
    const status = await fetchStatus();
    applyMachineName(status);
  } catch {
    /* offline */
  }
  if (lastScan?.found) {
    renderScanFound(lastScan.found);
    return;
  }
  try {
    const result = await postScan({});
    lastScan = result;
    renderScanFound(result.found);
  } catch {
    renderScanFound([]);
  }
}

const REST_OPTS = [0, 2, 5, 10, 30];
const CLOCK_OPTS = [0, 2, 5, 10];

function nearestIndex(opts, value) {
  let best = 0;
  let gap = Infinity;
  opts.forEach((v, i) => {
    const g = Math.abs(v - value);
    if (g < gap) {
      gap = g;
      best = i;
    }
  });
  return best;
}

function labelMinutes(v) {
  if (v === 0) return "Nunca / até tocar";
  return `${v} min`;
}

function markChipGroup(group, predicate) {
  const root = document.querySelector(`[data-panel-group="${group}"]`);
  if (!root) return;
  root.querySelectorAll("button").forEach((btn) => {
    let patch = {};
    try {
      patch = JSON.parse(btn.dataset.panelSet || "{}");
    } catch {
      patch = {};
    }
    btn.classList.toggle("on", predicate(patch));
  });
}

function applyPanelState(state) {
  const banner = document.querySelector('[data-live="panel-status-banner"]');
  const controls = document.querySelector('[data-live="panel-controls"]');
  if (!banner) return;

  if (!state?.reachable || !state?.ok || !state.settings) {
    setBannerVariant(banner, "err");
    banner.querySelector('[data-live="panel-status-title"]').textContent =
      "Painel inacessível na rede";
    banner.querySelector('[data-live="panel-status-sub"]').textContent =
      state?.error ||
      "Abra o Tessera no Wi‑Fi (tessera.local). Ajustes do aparelho não passam pelo cabo USB.";
    if (controls) controls.hidden = true;
    lastPanelState = state;
    drawPanelWidgetsBoard(state);
    return;
  }

  const s = state.settings;
  const info = state.info;
  setBannerVariant(banner, "ok");
  banner.querySelector('[data-live="panel-status-title"]').textContent = info
    ? `${info.host}`
    : "Painel na rede";
  banner.querySelector('[data-live="panel-status-sub"]').textContent = info?.ip
    ? info.ip
    : "Pronto para receber ajustes.";
  if (controls) controls.hidden = false;

  const brightSub = document.querySelector('[data-live="panel-brightness-sub"]');
  if (brightSub) {
    const label =
      s.brightness <= 40 ? "Baixo" : s.brightness <= 70 ? "Médio" : "Alto";
    brightSub.textContent = `Agora: ${label} (${s.brightness}%)`;
  }

  markChipGroup("portrait", (p) => p.portrait === s.portrait);
  markChipGroup("brightness", (p) => {
    const b = p.brightness;
    if (b === 30) return s.brightness <= 40;
    if (b === 60) return s.brightness > 40 && s.brightness <= 70;
    if (b === 100) return s.brightness > 70;
    return false;
  });
  markChipGroup("appearance", (p) => p.appearance === s.appearance);

  const sound = document.querySelector("[data-panel-toggle='sound']");
  if (sound) {
    sound.classList.toggle("on", !!s.sound);
    sound.setAttribute("aria-pressed", s.sound ? "true" : "false");
  }

  const rest = document.querySelector("[data-panel-rest]");
  const clock = document.querySelector("[data-panel-clock]");
  if (rest) rest.value = String(nearestIndex(REST_OPTS, s.rest_minutes));
  if (clock) clock.value = String(nearestIndex(CLOCK_OPTS, s.clock_minutes));
  const restLab = document.querySelector('[data-live="panel-rest-label"]');
  const clockLab = document.querySelector('[data-live="panel-clock-label"]');
  if (restLab) restLab.textContent = labelMinutes(s.rest_minutes);
  if (clockLab) clockLab.textContent = labelMinutes(s.clock_minutes);

  lastPanelState = state;
  drawPanelWidgetsBoard(state);
}

async function refreshPanelScreen() {
  if (visibleScreenId() !== "panel") return;
  try {
    const state = await fetchPanelState();
    applyPanelState(state);
  } catch (err) {
    applyPanelState({
      ok: false,
      reachable: false,
      error: String(err?.message ?? err),
      widgets: [],
    });
  }
}

async function runScanIfNeeded(id) {
  if (id !== "scan") return;
  try {
    const status = await fetchStatus();
    applyMachineName(status);
    const result = await postScan({});
    lastScan = result;
    // Short beat so the “varrendo” screen is visible, then show real profile data.
    setTimeout(() => {
      if (visibleScreenId() === "scan") go("scan-result");
    }, 900);
  } catch {
    /* offline — stay on scan; user can cancel */
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
  document.title = `Tessera — ${meta.title}`;
  history.replaceState(null, "", "#" + id);
  ensurePolls();
  void runScanIfNeeded(id);
}

document.body.addEventListener("click", (e) => {
  const openSettings = e.target.closest("[data-open-settings]");
  if (openSettings) {
    e.preventDefault();
    const pane = openSettings.dataset.openSettings;
    const hintEl = document.querySelector('[data-live="setup-hint"]');
    void openPrivacySettings(pane)
      .then((prep) => {
        if (hintEl) {
          hintEl.hidden = false;
          hintEl.textContent = prep?.hint ?? "Ligue Tessera na lista e volte aqui.";
        }
      })
      .catch((err) => {
        if (hintEl) {
          hintEl.hidden = false;
          hintEl.textContent =
            "Não abri Ajustes automaticamente. Abra Privacidade e Segurança manualmente. " +
            String(err?.message ?? err);
        } else {
          window.alert(String(err?.message ?? err));
        }
      });
    return;
  }
  const scanToggle = e.target.closest("[data-scan-toggle]");
  if (scanToggle && !scanToggle.disabled) {
    e.preventDefault();
    scanToggle.classList.toggle("on");
    scanToggle.setAttribute(
      "aria-pressed",
      scanToggle.classList.contains("on") ? "true" : "false",
    );
    return;
  }
  const updateAuto = e.target.closest("[data-update-auto]");
  if (updateAuto) {
    e.preventDefault();
    const next = !updateAuto.classList.contains("on");
    updateAuto.classList.toggle("on", next);
    updateAuto.setAttribute("aria-pressed", next ? "true" : "false");
    void postUpdate({ auto_update: next })
      .then(paintUpdateStatus)
      .catch((err) => {
        updateAuto.classList.toggle("on", !next);
        updateAuto.setAttribute("aria-pressed", !next ? "true" : "false");
        window.alert(String(err?.message ?? err));
      });
    return;
  }
  const updateCheck = e.target.closest("[data-action='update-check']");
  if (updateCheck) {
    e.preventDefault();
    const sub = document.querySelector('[data-live="update-sub"]');
    if (sub) sub.textContent = "Procurando…";
    void postUpdate({ check: true })
      .then(paintUpdateStatus)
      .catch((err) => {
        if (sub) sub.textContent = String(err?.message ?? err);
      });
    return;
  }
  const capToggle = e.target.closest("[data-cap-toggle]");
  if (capToggle) {
    e.preventDefault();
    if (capToggle.disabled) return;
    capToggle.classList.toggle("on");
    capToggle.setAttribute(
      "aria-pressed",
      capToggle.classList.contains("on") ? "true" : "false",
    );
    void persistMacCaps().catch((err) => window.alert(String(err?.message ?? err)));
    return;
  }
  const widgetToggle = e.target.closest("[data-widget-toggle]");
  if (widgetToggle) {
    e.preventDefault();
    const id = widgetToggle.dataset.widgetToggle;
    const widgets = Array.isArray(lastPanelState?.widgets)
      ? lastPanelState.widgets.map((w) => ({ ...w }))
      : [];
    const row = widgets.find((w) => w.id === id);
    if (!row) return;
    if (row.shown && widgets.filter((w) => w.shown).length <= 1) {
      window.alert("Deixe ao menos um widget na home.");
      return;
    }
    row.shown = !row.shown;
    void postPanelDashboard(widgets)
      .then((state) => {
        applyPanelState(state);
        if (visibleScreenId() === "widgets") applyWidgetsScreen(state);
      })
      .catch((err) => {
        void refreshPanelScreen();
        window.alert(
          String(err?.message ?? err) +
            "\n\nLigue o cabo ou a rede do painel para salvar.",
        );
      });
    return;
  }
  const widgetNav = e.target.closest("[data-widget-nav]");
  if (widgetNav) {
    e.preventDefault();
    setSelectedWidget(widgetNav.dataset.widgetNav || "");
    return;
  }
  const widgetsBack = e.target.closest("[data-action='widgets-back']");
  if (widgetsBack) {
    e.preventDefault();
    setSelectedWidget("");
    return;
  }
  const wsBtn = e.target.closest("[data-ws-set]");
  if (wsBtn) {
    e.preventDefault();
    let payload = null;
    try {
      payload = JSON.parse(wsBtn.dataset.wsSet || "null");
    } catch {
      return;
    }
    if (!payload?.id || !payload?.key) return;
    const group = wsBtn.closest("[data-ws-chips]");
    if (group) {
      group.querySelectorAll("button").forEach((b) => b.classList.remove("on", "pending"));
      wsBtn.classList.add("on", "pending");
    }
    void postWidgetSettings(payload.id, payload.key, payload.value)
      .then((state) => {
        wsBtn.classList.remove("pending");
        applyWidgetsScreen(state);
      })
      .catch((err) => {
        wsBtn.classList.remove("pending");
        void refreshWidgetsScreen();
        window.alert(
          String(err?.message ?? err) +
            "\n\nLigue o cabo ou a rede do painel para salvar.",
        );
      });
    return;
  }
  const panelAi = e.target.closest("[data-panel-ai]");
  if (panelAi) {
    e.preventDefault();
    const key = panelAi.dataset.panelAi;
    const next = !panelAi.classList.contains("on");
    panelAi.classList.toggle("on", next);
    panelAi.setAttribute("aria-pressed", next ? "true" : "false");
    const capId =
      key === "show_aitokens_codex"
        ? "codex"
        : key === "show_aitokens_claude"
          ? "claude"
          : key === "show_aitokens_cursor"
            ? "cursor"
            : "";
    void postPanelSettings({ [key]: next })
      .then(async (state) => {
        applyWidgetsScreen(state);
        if (capId) {
          const cap = document.querySelector(`[data-cap-toggle="${capId}"]`);
          if (cap) {
            cap.classList.toggle("on", next);
            cap.setAttribute("aria-pressed", next ? "true" : "false");
          }
          try {
            await postScan({
              found: [{ id: capId, enabled: next }],
            });
            await refreshMacScreen();
            await refreshHomeCaps();
          } catch {
            /* companion offline */
          }
        }
      })
      .catch((err) => {
        panelAi.classList.toggle("on", !next);
        panelAi.setAttribute("aria-pressed", !next ? "true" : "false");
        window.alert(
          String(err?.message ?? err) +
            "\n\nLigue o cabo ou a rede do painel para salvar.",
        );
      });
    return;
  }
  const saveTodoist = e.target.closest("[data-action='save-todoist-token']");
  if (saveTodoist) {
    e.preventDefault();
    const input = document.querySelector("[data-todoist-token]");
    const token = (input?.value || "").trim();
    if (!token) {
      window.alert("Cole um token para salvar. Em branco não altera o atual.");
      return;
    }
    void postPanelToken(token)
      .then((state) => {
        if (input) input.value = "";
        applyWidgetsScreen(state);
      })
      .catch((err) =>
        window.alert(
          String(err?.message ?? err) +
            "\n\nLigue o cabo ou a rede do painel para salvar.",
        ),
      );
    return;
  }
  const saveWeather = e.target.closest("[data-action='save-weather-city']");
  if (saveWeather) {
    e.preventDefault();
    const input = document.querySelector("[data-weather-city]");
    const name = (input?.value || "").trim();
    if (!name) {
      window.alert("Digite a cidade.");
      return;
    }
    const body =
      weatherPick && weatherPick.name === name
        ? {
            name: weatherPick.name,
            latitude: weatherPick.latitude,
            longitude: weatherPick.longitude,
          }
        : { city: name };
    void postPanelWeather(body)
      .then((state) => {
        weatherPick = null;
        applyWidgetsScreen(state);
      })
      .catch((err) =>
        window.alert(
          String(err?.message ?? err) +
            "\n\nLigue o cabo ou a rede do painel para salvar.",
        ),
      );
    return;
  }
  const weatherSug = e.target.closest("[data-weather-pick]");
  if (weatherSug) {
    e.preventDefault();
    let pick = null;
    try {
      pick = JSON.parse(weatherSug.dataset.weatherPick || "null");
    } catch {
      return;
    }
    if (!pick?.name) return;
    weatherPick = pick;
    const input = document.querySelector("[data-weather-city]");
    if (input) input.value = pick.name;
    const box = document.querySelector('[data-live="weather-suggest"]');
    if (box) {
      box.innerHTML = "";
      box.hidden = true;
    }
    void postPanelWeather({
      name: pick.name,
      latitude: pick.latitude,
      longitude: pick.longitude,
    })
      .then((state) => applyWidgetsScreen(state))
      .catch((err) => window.alert(String(err?.message ?? err)));
    return;
  }
  const calAdd = e.target.closest("[data-action='calendar-add-account']");
  if (calAdd) {
    e.preventDefault();
    void postCalendarAddAccount()
      .then((state) => {
        applyWidgetsScreen(state);
        window.alert("Abrindo autorização no browser do Mac…");
      })
      .catch((err) =>
        window.alert(
          String(err?.message ?? err) +
            "\n\nLigue o cabo ou a rede do painel.",
        ),
      );
    return;
  }
  const tileBtn = e.target.closest("[data-tile-value]");
  if (tileBtn) {
    e.preventDefault();
    const group = tileBtn.closest("[data-tile-size]");
    if (!group) return;
    const widgetId = group.dataset.tileSize;
    const value = tileBtn.dataset.tileValue;
    group.querySelectorAll("[data-tile-value]").forEach((b) => {
      b.classList.remove("on", "pending");
    });
    tileBtn.classList.add("on", "pending");
    void postWidgetSettings(widgetId, "home_tile_size", value)
      .then((state) => {
        tileBtn.classList.remove("pending");
        applyWidgetsScreen(state);
      })
      .catch((err) => {
        tileBtn.classList.remove("pending");
        void refreshWidgetsScreen();
        window.alert(
          String(err?.message ?? err) +
            "\n\nLigue o cabo ou a rede do painel para salvar.",
        );
      });
    return;
  }
  const panelSet = e.target.closest("[data-panel-set]");
  if (panelSet) {
    e.preventDefault();
    let patch = {};
    try {
      patch = JSON.parse(panelSet.dataset.panelSet || "{}");
    } catch {
      return;
    }
    const group = panelSet.closest(".chips");
    if (group) {
      group.querySelectorAll("button").forEach((b) => {
        b.classList.remove("on", "pending");
      });
      panelSet.classList.add("on", "pending");
    }
    const brightSub = document.querySelector('[data-live="panel-brightness-sub"]');
    if (brightSub && patch.brightness != null) {
      const label =
        patch.brightness <= 40 ? "Baixo" : patch.brightness <= 70 ? "Médio" : "Alto";
      brightSub.textContent = `Agora: ${label} (${patch.brightness}%)`;
    }
    void postPanelSettings(patch)
      .then((state) => {
        panelSet.classList.remove("pending");
        applyPanelState(state);
      })
      .catch((err) => {
        panelSet.classList.remove("pending");
        void refreshPanelScreen();
        window.alert(String(err?.message ?? err));
      });
    return;
  }
  const panelToggle = e.target.closest("[data-panel-toggle='sound']");
  if (panelToggle) {
    e.preventDefault();
    const next = !panelToggle.classList.contains("on");
    panelToggle.classList.toggle("on", next);
    panelToggle.setAttribute("aria-pressed", next ? "true" : "false");
    void postPanelSettings({ sound: next })
      .then(applyPanelState)
      .catch((err) => {
        panelToggle.classList.toggle("on", !next);
        panelToggle.setAttribute("aria-pressed", !next ? "true" : "false");
        window.alert(String(err?.message ?? err));
      });
    return;
  }
  const panelRefresh = e.target.closest("[data-action='panel-refresh']");
  if (panelRefresh) {
    e.preventDefault();
    void postPanelRefresh()
      .then(() => refreshPanelScreen())
      .catch((err) => window.alert(String(err?.message ?? err)));
    return;
  }
  const panelReload = e.target.closest("[data-action='panel-reload']");
  if (panelReload) {
    e.preventDefault();
    void refreshPanelScreen();
    return;
  }
  const editName = e.target.closest("[data-action='edit-machine-name']");
  if (editName) {
    e.preventDefault();
    const current = machineLabel(lastStatus);
    const next = window.prompt("Nome deste Mac no Tessera", current === "Sem nome ainda" ? "" : current);
    if (next == null) return;
    const trimmed = next.trim();
    if (!trimmed) return;
    void persistScanChoices({ displayName: trimmed }).catch((err) =>
      window.alert(String(err?.message ?? err)),
    );
    return;
  }
  const scanSetup = e.target.closest("[data-action='scan-continue-setup']");
  if (scanSetup) {
    e.preventDefault();
    void persistScanChoices({ setupCompleted: false })
      .then(() => go("setup"))
      .catch((err) => window.alert(String(err?.message ?? err)));
    return;
  }
  const scanHome = e.target.closest("[data-action='scan-save-home']");
  if (scanHome) {
    e.preventDefault();
    void persistScanChoices({ setupCompleted: true })
      .then(() => go("home"))
      .catch((err) => window.alert(String(err?.message ?? err)));
    return;
  }
  const recheck = e.target.closest("[data-action='recheck-permissions']");
  if (recheck) {
    e.preventDefault();
    void refreshSetupScreen().then(() => refreshDoctorScreen());
    return;
  }
  const daemon = e.target.closest(
    "[data-action='agentd-restart'], [data-action='agentd-stop'], [data-action='agentd-start']",
  );
  if (daemon) {
    e.preventDefault();
    void controlAgent(daemon.dataset.action.replace("agentd-", ""));
    return;
  }
  const panelReboot = e.target.closest("[data-action='panel-reboot']");
  if (panelReboot) {
    e.preventDefault();
    void rebootPanel();
    return;
  }
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
      "O companion em segundo plano (Tessera) precisa estar rodando.\n\nSe instalou pelo cartão SD, rode de novo o install.command.\nOu, em desenvolvimento: mise run agentd-run\n\nDepois toque em Tentar de novo.",
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
  if (
    tog &&
    !tog.dataset.panelToggle &&
    !tog.dataset.scanToggle &&
    !tog.dataset.capToggle &&
    !tog.dataset.panelAi &&
    !tog.dataset.updateAuto
  ) {
    tog.classList.toggle("on");
    tog.setAttribute("aria-pressed", tog.classList.contains("on"));
  }
});

document.body.addEventListener("change", (e) => {
  const calCheck = e.target.closest("[data-cal-id]");
  if (calCheck) {
    void saveAgendaSelection().catch((err) => {
      void refreshWidgetsScreen();
      window.alert(String(err?.message ?? err));
    });
    return;
  }
  const rest = e.target.closest("[data-panel-rest]");
  if (rest) {
    const minutes = REST_OPTS[Number(rest.value)] ?? 5;
    const restLab = document.querySelector('[data-live="panel-rest-label"]');
    if (restLab) restLab.textContent = labelMinutes(minutes);
    void postPanelSettings({ rest_minutes: minutes })
      .then(applyPanelState)
      .catch((err) => {
        void refreshPanelScreen();
        window.alert(String(err?.message ?? err));
      });
    return;
  }
  const clock = e.target.closest("[data-panel-clock]");
  if (clock) {
    const minutes = CLOCK_OPTS[Number(clock.value)] ?? 2;
    const clockLab = document.querySelector('[data-live="panel-clock-label"]');
    if (clockLab) clockLab.textContent = labelMinutes(minutes);
    void postPanelSettings({ clock_minutes: minutes })
      .then(applyPanelState)
      .catch((err) => {
        void refreshPanelScreen();
        window.alert(String(err?.message ?? err));
      });
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
    lastStatus = status;
    applyMachineName(status);
    // Clean install: setup_completed stays false until the wizard writes the profile.
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

const weatherInputEl = document.querySelector("[data-weather-city]");
if (weatherInputEl) {
  weatherInputEl.addEventListener("input", () => {
    weatherPick = null;
    clearTimeout(weatherSuggestTimer);
    weatherSuggestTimer = setTimeout(async () => {
      const q = weatherInputEl.value.trim();
      const box = document.querySelector('[data-live="weather-suggest"]');
      if (!box) return;
      if (q.length < 2) {
        box.innerHTML = "";
        box.hidden = true;
        return;
      }
      try {
        const r = await fetch(
          `https://geocoding-api.open-meteo.com/v1/search?name=${encodeURIComponent(q)}&count=5&language=pt&format=json`,
        );
        const j = await r.json();
        const rows = j.results || [];
        if (!rows.length) {
          box.innerHTML = "";
          box.hidden = true;
          return;
        }
        box.hidden = false;
        box.innerHTML = rows
          .map((row) => {
            const where = [row.admin1, row.country].filter(Boolean).join(" · ");
            const pick = escapeHtml(
              JSON.stringify({
                name: row.name,
                latitude: row.latitude,
                longitude: row.longitude,
              }),
            );
            return `<button type="button" data-weather-pick="${pick}">
              <b>${escapeHtml(row.name)}</b><span>${escapeHtml(where)}</span>
            </button>`;
          })
          .join("");
      } catch {
        box.innerHTML = "";
        box.hidden = true;
      }
    }, 280);
  });
  weatherInputEl.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      document.querySelector("[data-action='save-weather-city']")?.click();
    }
  });
}

if (window.__TAURI__?.event?.listen) {
  window.__TAURI__.event.listen("navigate", (ev) => {
    const id = ev.payload;
    if (typeof id === "string" && screens.some((s) => s.id === id)) {
      go(id);
    }
  });
}

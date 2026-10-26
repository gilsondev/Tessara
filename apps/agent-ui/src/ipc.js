import { DOCTOR_FIXTURE, LOGS_FIXTURE_LINES } from "./fixtures.js";

/** agentd localhost IPC (TESSARA_AGENTD_IPC_PORT / VITE_AGENTD_PORT, default 19792). */
export const AGENTD_PORT = Number(
  (typeof import.meta !== "undefined" &&
    import.meta.env &&
    import.meta.env.VITE_AGENTD_PORT) ||
    19792,
);

export function agentdUrl(path) {
  return `http://127.0.0.1:${AGENTD_PORT}${path}`;
}

const IPC_TIMEOUT_MS = 1500;

export async function fetchStatus() {
  const r = await fetch(agentdUrl("/v1/status"), {
    signal: AbortSignal.timeout(800),
  });
  if (!r.ok) throw new Error(`status ${r.status}`);
  const body = await r.json();
  if (body?.ok !== true) throw new Error("agentd not ok");
  return body;
}

/** @returns {Promise<{ scanned_at: string, found: object[] }>} */
export async function fetchScanLast() {
  const r = await fetch(agentdUrl("/v1/scan/last"), {
    signal: AbortSignal.timeout(IPC_TIMEOUT_MS),
  });
  if (!r.ok) throw new Error(`scan/last ${r.status}`);
  return r.json();
}

/**
 * @param {object} [body]
 * @returns {Promise<{ scanned_at: string, found: object[] }>}
 */
export async function postScan(body = {}) {
  const r = await fetch(agentdUrl("/v1/scan"), {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(IPC_TIMEOUT_MS * 2),
  });
  if (!r.ok) throw new Error(`scan ${r.status}`);
  return r.json();
}

/** @returns {Promise<{ machines: object[] }>} */
export async function fetchMachines() {
  const r = await fetch(agentdUrl("/v1/machines"), {
    signal: AbortSignal.timeout(IPC_TIMEOUT_MS),
  });
  if (!r.ok) throw new Error(`machines ${r.status}`);
  return r.json();
}

/**
 * @returns {Promise<{ report: { summary: string, checks: object[] }, demo: boolean }>}
 */
export async function fetchDoctor() {
  try {
    const r = await fetch(agentdUrl("/v1/doctor"), {
      signal: AbortSignal.timeout(IPC_TIMEOUT_MS),
    });
    if (r.status === 404 || r.status === 501) {
      return { report: DOCTOR_FIXTURE, demo: true };
    }
    if (!r.ok) throw new Error(`doctor ${r.status}`);
    const report = await r.json();
    if (!report?.checks?.length) throw new Error("doctor empty");
    return { report, demo: false };
  } catch {
    return { report: DOCTOR_FIXTURE, demo: true };
  }
}

/**
 * @param {number} [tail=200]
 * @returns {Promise<{ lines: string[], demo: boolean }>}
 */
export async function fetchLogs(tail = 200) {
  const q = Number.isFinite(tail) ? Math.max(1, Math.min(tail, 2000)) : 200;
  try {
    const r = await fetch(agentdUrl(`/v1/logs?tail=${q}`), {
      signal: AbortSignal.timeout(IPC_TIMEOUT_MS),
    });
    if (r.status === 404 || r.status === 501) {
      return { lines: LOGS_FIXTURE_LINES, demo: true };
    }
    if (!r.ok) throw new Error(`logs ${r.status}`);
    const body = await r.json();
    const lines = Array.isArray(body?.lines) ? body.lines : [];
    if (!lines.length) throw new Error("logs empty");
    return { lines, demo: false };
  } catch {
    return { lines: LOGS_FIXTURE_LINES, demo: true };
  }
}

/** agentd reachable and healthy (`ok: true` in /v1/status). */
export function isAgentdOnline(status) {
  return status?.ok === true;
}

export function isUsbUp(status) {
  return status?.usb === "up";
}

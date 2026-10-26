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

export async function fetchStatus() {
  const r = await fetch(agentdUrl("/v1/status"), {
    signal: AbortSignal.timeout(800),
  });
  if (!r.ok) throw new Error(`status ${r.status}`);
  return r.json();
}

/** Embedded demo payloads when agentd is offline or routes fail. */

export const DOCTOR_FIXTURE = {
  summary: "1 coisa precisa de você",
  checks: [
    {
      id: "usb",
      level: "ok",
      title: "Painel no cabo",
      detail: "USB ok · handshake · firmware 0.9.4",
    },
    {
      id: "agentd",
      level: "ok",
      title: "App em segundo plano",
      detail: "Abre com o Mac e está rodando agora",
    },
    {
      id: "files",
      level: "bad",
      title: "Acesso aos arquivos",
      detail: "Claude e Focus precisam que você marque o Tessera em Ajustes do Mac.",
      action: "setup",
      settings_pane: "full_disk_access",
      settings_url:
        "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_AllFiles",
    },
    {
      id: "music",
      level: "ok",
      title: "Música",
      detail: "Spotify detectado · agora: parado",
    },
    {
      id: "focus",
      level: "warn",
      title: "Focus",
      detail: "Último modo: Trabalho · pausado até liberar acesso aos arquivos",
      action: "setup",
      settings_pane: "full_disk_access",
      settings_url:
        "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_AllFiles",
    },
    {
      id: "cursor",
      level: "ok",
      title: "Cursor",
      detail: "Conta logada · cotas lidas · 42% do mês",
    },
    {
      id: "claude",
      level: "warn",
      title: "Claude",
      detail: "App achado · cotas bloqueadas pela autorização acima",
    },
    {
      id: "codex",
      level: "skip",
      title: "Codex / ChatGPT",
      detail: "Não instalado neste Mac — nada a fazer",
    },
    {
      id: "push",
      level: "ok",
      title: "Envio ao painel",
      detail: "Fila limpa · último push há 2 s",
    },
  ],
};

export const LOGS_FIXTURE_LINES = [
  "2026-10-25T21:04:12.441-03:00 INFO  usb: attached vid=303a pid=1001 serial=JC3248-A1 path=/dev/cu.usbmodem*",
  "2026-10-25T21:04:12.610-03:00 INFO  agentd: handshake ok proto=1 device=tessera fw=0.9.4",
  "2026-10-25T21:04:12.802-03:00 INFO  media.watch: source=spotify state=paused title=\"\" dirty=false",
  "2026-10-25T21:04:12.918-03:00 INFO  focus.watch: mode=\"Trabalho\" assertions_mtime=1729905851 push=ok",
  "2026-10-25T21:04:13.104-03:00 DEBUG aitokens.cursor: remaining=42% window=monthly path=~/Library/Application Support/Cursor/*",
  "2026-10-25T21:04:13.220-03:00 WARN  aitokens.claude: fda=denied; skip quota until Full Disk Access",
  "2026-10-25T21:05:01.044-03:00 WARN  usb: detach (transient) — reconnect in 380ms",
  "2026-10-25T21:05:01.424-03:00 INFO  usb: reattached; resume dirty-push queue=2",
  "2026-10-25T18:22:09.011-03:00 ERROR focus.watch: TCC kTCCServiceSystemPolicyAllFiles denied → UI “Falta autorização de arquivos”",
];

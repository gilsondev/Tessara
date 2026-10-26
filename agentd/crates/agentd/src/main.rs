mod ipc;
mod logutil;
mod mac_events;
mod panel;
mod update;
mod usb_loop;

use clap::Parser;

#[derive(Parser, Debug)]
#[command(name = "Tessera", version, about = "Companion Tessera — sync Mac ↔ painel")]
struct Args {
    /// Print AGENT handshake line and exit (for golden/CI)
    #[arg(long)]
    print_agent_line: bool,
    /// Serial device path (e.g. /dev/cu.usbmodem*)
    #[arg(long)]
    port: Option<String>,
    /// Open port, DTR, send AGENT line, read ~2s, exit
    #[arg(long)]
    once: bool,
}

#[tokio::main]
async fn main() {
    let args = Args::parse();
    if args.print_agent_line {
        println!("{}", tessera_protocol::agent_ok_line());
        return;
    }

    let profile = match tessera_profile::load_or_init() {
        Ok(p) => p,
        Err(e) => {
            eprintln!("Tessera: profile: {e}");
            std::process::exit(1);
        }
    };
    eprintln!(
        "agentd: machine {} ({})",
        profile.display_name, profile.machine_id
    );

    if args.once {
        let port = match args.port {
            Some(p) => p,
            None => {
                usb_loop::print_candidate_ports();
                let ports = tessera_usb::list_candidate_ports();
                if ports.is_empty() {
                    eprintln!("Tessera: --once requer porta (--port) ou painel conectado");
                    std::process::exit(1);
                }
                eprintln!("Tessera: especifique --port <caminho>");
                std::process::exit(1);
            }
        };
        if let Err(e) = usb_loop::run_once(&port) {
            eprintln!("Tessera: {e}");
            std::process::exit(1);
        }
        return;
    }

    let app_state = ipc::AppState::default();
    let logs = Some(app_state.logs.clone());
    crate::logutil::info(
        &logs,
        format!(
            "agentd: started v{} machine={} — logs Mac↔painel na Atividade",
            env!("CARGO_PKG_VERSION"),
            profile.machine_id
        ),
    );
    update::spawn_poller(app_state.update.clone());
    let usb_link = app_state.usb.clone();
    let profile_paths = app_state.profile_paths.clone();

    if let Some(port) = args.port.clone() {
        crate::logutil::info(&logs, format!("usb: sessão em background (--port {port})"));
        let paths = profile_paths.clone();
        let logs_usb = logs.clone();
        std::thread::spawn(move || {
            if let Err(e) = usb_loop::run_session(&port, paths, Some(usb_link), logs_usb.clone()) {
                crate::logutil::warn(&logs_usb, format!("usb: sessão encerrada — {e}"));
            }
        });
    } else {
        usb_loop::print_candidate_ports();
        let logs_usb = logs.clone();
        std::thread::spawn(move || {
            usb_loop::run_auto(profile_paths, Some(usb_link), logs_usb)
        });
    }

    if let Err(e) = ipc::run_ipc_server(app_state).await {
        eprintln!("Tessera: IPC: {e}");
        std::process::exit(1);
    }
}

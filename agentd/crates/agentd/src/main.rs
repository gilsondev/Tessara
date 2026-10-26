mod ipc;
mod usb_loop;

use clap::Parser;

#[derive(Parser, Debug)]
#[command(name = "agentd", version, about = "Tessara Agent v2 daemon")]
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
        println!("{}", tessara_protocol::agent_ok_line());
        return;
    }

    let profile = match tessara_profile::load_or_init() {
        Ok(p) => p,
        Err(e) => {
            eprintln!("agentd: profile: {e}");
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
                let ports = tessara_usb::list_candidate_ports();
                if ports.is_empty() {
                    eprintln!("agentd: --once requer porta (--port) ou painel conectado");
                    std::process::exit(1);
                }
                eprintln!("agentd: especifique --port <caminho>");
                std::process::exit(1);
            }
        };
        if let Err(e) = usb_loop::run_once(&port) {
            eprintln!("agentd: {e}");
            std::process::exit(1);
        }
        return;
    }

    if args.port.is_some() {
        eprintln!("agentd: use --once com --port para smoke USB");
        std::process::exit(1);
    }

    usb_loop::print_candidate_ports();
    if let Err(e) = ipc::run_ipc_server(ipc::AppState::default()).await {
        eprintln!("agentd: IPC: {e}");
        std::process::exit(1);
    }
}

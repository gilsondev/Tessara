use clap::Parser;

#[derive(Parser, Debug)]
#[command(name = "agentd", version, about = "Tessara Agent v2 daemon")]
struct Args {
    /// Print AGENT handshake line and exit (for golden/CI)
    #[arg(long)]
    print_agent_line: bool,
}

fn main() {
    let args = Args::parse();
    if args.print_agent_line {
        print!("{}", tessara_protocol::agent_ok_line(env!("CARGO_PKG_VERSION")));
        return;
    }
    eprintln!("agentd: USB loop not wired yet — see plan parity");
    std::process::exit(0);
}

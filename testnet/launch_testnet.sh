#!/usr/bin/env bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════
#  NPChain Testnet — One-Command Launch Script
#
#  Usage:
#    ./launch_testnet.sh              Start testnet
#    ./launch_testnet.sh stop         Stop all containers
#    ./launch_testnet.sh reset        Wipe all data + restart fresh
#    ./launch_testnet.sh status       Show status dashboard
#    ./launch_testnet.sh logs         Tail all logs
#    ./launch_testnet.sh logs miner   Tail miner logs
# ═══════════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

log()  { echo -e "${GREEN}  [✓]${NC} $1"; }
warn() { echo -e "${YELLOW}  [!]${NC} $1"; }
err()  { echo -e "${RED}  [✗]${NC} $1"; }
step() { echo -e "\n${CYAN}  ═══ $1 ═══${NC}"; }

COMPOSE="docker compose"
command -v docker-compose &>/dev/null && COMPOSE="docker-compose"

banner() {
    echo -e "${BOLD}"
    cat << 'EOF'

    ╔═══════════════════════════════════════════════════════════════╗
    ║                                                               ║
    ║   ███╗   ██╗██████╗  ██████╗██╗  ██╗ █████╗ ██╗███╗   ██╗   ║
    ║   ████╗  ██║██╔══██╗██╔════╝██║  ██║██╔══██╗██║████╗  ██║   ║
    ║   ██╔██╗ ██║██████╔╝██║     ███████║███████║██║██╔██╗ ██║   ║
    ║   ██║╚██╗██║██╔═══╝ ██║     ██╔══██║██╔══██║██║██║╚██╗██║   ║
    ║   ██║ ╚████║██║     ╚██████╗██║  ██║██║  ██║██║██║ ╚████║   ║
    ║   ╚═╝  ╚═══╝╚═╝      ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝   ║
    ║                                                               ║
    ║         Proof-of-NP-Witness Testnet Launcher                  ║
    ║         Quantum-Resistant • Privacy-Preserving                ║
    ║                                                               ║
    ╚═══════════════════════════════════════════════════════════════╝

EOF
    echo -e "${NC}"
}

cmd_stop() {
    step "Stopping Testnet"
    $COMPOSE -f docker-compose.yml down
    log "All containers stopped"
}

cmd_reset() {
    step "Resetting Testnet (wiping all data)"
    $COMPOSE -f docker-compose.yml down -v
    log "All data wiped"
    echo ""
    cmd_start
}

cmd_status() {
    echo ""
    echo -e "${BOLD}  NPChain Testnet Status${NC}"
    echo "  ────────────────────────────────────────────────"
    $COMPOSE -f docker-compose.yml ps
    echo ""

    # Try to get chain info from RPC
    if curl -sf http://localhost:18332/status &>/dev/null; then
        echo -e "  ${GREEN}RPC:${NC} http://localhost:18332 ✓"
        CHAIN_INFO=$(curl -sf http://localhost:18332/status 2>/dev/null || echo "{}")
        HEIGHT=$(echo "$CHAIN_INFO" | grep -o '"height":[0-9]*' | cut -d: -f2)
        PEERS=$(echo "$CHAIN_INFO" | grep -o '"peers":[0-9]*' | cut -d: -f2)
        [ -n "$HEIGHT" ] && echo -e "  ${CYAN}Chain height:${NC} $HEIGHT"
        [ -n "$PEERS" ] && echo -e "  ${CYAN}Connected peers:${NC} $PEERS"
    else
        echo -e "  ${YELLOW}RPC:${NC} Not reachable yet (node may still be starting)"
    fi

    echo ""
    echo "  Endpoints:"
    echo "    P2P:       localhost:19333"
    echo "    RPC:       http://localhost:18332"
    echo "    Explorer:  http://localhost:8080"
    echo "    Faucet:    http://localhost:8081"
    echo "    Grafana:   http://localhost:3000 (admin / npchain-testnet)"
    echo ""
}

cmd_logs() {
    local service="${1:-}"
    if [ -n "$service" ]; then
        $COMPOSE -f docker-compose.yml logs -f "$service"
    else
        $COMPOSE -f docker-compose.yml logs -f
    fi
}

cmd_start() {
    banner

    # ─── Pre-flight ───
    step "Pre-flight Checks"

    if ! command -v docker &>/dev/null; then
        err "Docker is not installed."
        echo ""
        echo "    Install Docker:"
        echo "      curl -fsSL https://get.docker.com | sh"
        echo "      sudo usermod -aG docker \$USER"
        echo "      (log out and back in)"
        echo ""
        exit 1
    fi
    log "Docker installed ($(docker --version | awk '{print $3}'))"

    if ! docker info &>/dev/null; then
        err "Docker daemon is not running."
        echo "    Start it: sudo systemctl start docker"
        exit 1
    fi
    log "Docker daemon running"

    DOCKER_MEM=$(docker info 2>/dev/null | grep "Total Memory" | awk '{print $3, $4}')
    log "Docker memory: $DOCKER_MEM"

    # Check available disk space
    DISK_FREE=$(df -h . | tail -1 | awk '{print $4}')
    log "Disk available: $DISK_FREE"

    # ─── Build ───
    step "Building NPChain (this may take a few minutes the first time)"

    $COMPOSE -f docker-compose.yml build --parallel 2>&1 | \
        while IFS= read -r line; do
            if echo "$line" | grep -q "Successfully"; then
                log "$line"
            elif echo "$line" | grep -q "ERROR\|error\|FATAL"; then
                err "$line"
            fi
        done

    log "Build complete"

    # ─── Genesis ───
    step "Generating Genesis Block"

    # Remove old genesis if resetting
    $COMPOSE -f docker-compose.yml up genesis 2>&1 | \
        while IFS= read -r line; do
            echo "    $line"
        done

    log "Genesis block created"

    # ─── Launch seed nodes ───
    step "Launching Seed Nodes (3 nodes)"

    $COMPOSE -f docker-compose.yml up -d seed1 seed2 seed3
    log "Seed nodes starting..."

    # Wait for seed1 to be healthy
    echo -n "    Waiting for seed1 health check"
    for i in $(seq 1 30); do
        if docker inspect --format='{{.State.Health.Status}}' npchain_seed1 2>/dev/null | grep -q "healthy"; then
            echo ""
            log "seed1 is healthy"
            break
        fi
        echo -n "."
        sleep 2
    done
    echo ""

    # ─── Launch miner ───
    step "Launching Miner"

    $COMPOSE -f docker-compose.yml up -d miner
    log "Miner starting (4 threads, CDCL/MitM/DSatur/DFS solvers)"

    sleep 3

    # ─── Launch services ───
    step "Launching Services (faucet, explorer, monitoring)"

    $COMPOSE -f docker-compose.yml up -d faucet explorer prometheus grafana
    log "Faucet starting on port 8081"
    log "Block explorer starting on port 8080"
    log "Grafana starting on port 3000"

    sleep 2

    # ─── Verify ───
    step "Verifying Deployment"

    ALL_OK=true
    SERVICES="npchain_seed1 npchain_seed2 npchain_seed3 npchain_miner"

    for svc in $SERVICES; do
        STATUS=$(docker inspect --format='{{.State.Running}}' "$svc" 2>/dev/null || echo "false")
        if [ "$STATUS" = "true" ]; then
            log "$svc: running"
        else
            err "$svc: NOT RUNNING"
            ALL_OK=false
        fi
    done

    echo ""

    if [ "$ALL_OK" = true ]; then
        echo -e "${BOLD}"
        cat << 'DONE'

    ╔═══════════════════════════════════════════════════════════════╗
    ║                                                               ║
    ║   TESTNET IS LIVE!                                            ║
    ║                                                               ║
    ╠═══════════════════════════════════════════════════════════════╣
    ║                                                               ║
    ║   Network:     NPChain Testnet (chain_id: 0x544352)          ║
    ║   Consensus:   Proof-of-NP-Witness (PoNW)                    ║
    ║   Block time:  ~10 seconds                                    ║
    ║   Token:       tCerts (test tokens, no real value)              ║
    ║   Emission:    52.5M tCerts/year, unlimited supply              ║
    ║   Crypto:      Dilithium-5, Kyber-1024 (post-quantum)        ║
    ║                                                               ║
    ║   Endpoints:                                                  ║
    ║   ──────────────────────────────────────────────────          ║
    ║   P2P:         localhost:19333                                ║
    ║   JSON-RPC:    http://localhost:18332                         ║
    ║   Explorer:    http://localhost:8080                          ║
    ║   Faucet:      http://localhost:8081                          ║
    ║   Grafana:     http://localhost:3000                          ║
    ║                (user: admin / pass: npchain-testnet)          ║
    ║                                                               ║
    ║   Quick Commands:                                             ║
    ║   ──────────────────────────────────────────────────          ║
    ║   Watch miner:    ./launch_testnet.sh logs miner              ║
    ║   Check status:   ./launch_testnet.sh status                  ║
    ║   Stop testnet:   ./launch_testnet.sh stop                    ║
    ║   Full reset:     ./launch_testnet.sh reset                   ║
    ║                                                               ║
    ║   Connect external node:                                      ║
    ║     npchain_node --testnet --peers <your-ip>:19333            ║
    ║                                                               ║
    ║   Get test tokens:                                            ║
    ║     curl http://localhost:8081/drip?address=<your-address>    ║
    ║                                                               ║
    ╚═══════════════════════════════════════════════════════════════╝

DONE
        echo -e "${NC}"
    else
        echo ""
        err "Some services failed to start. Check logs:"
        echo "    $COMPOSE -f docker-compose.yml logs"
    fi
}

# ─── CLI Router ───
case "${1:-start}" in
    start)    cmd_start ;;
    stop)     cmd_stop ;;
    reset)    cmd_reset ;;
    status)   cmd_status ;;
    logs)     cmd_logs "${2:-}" ;;
    *)
        echo "Usage: $0 {start|stop|reset|status|logs [service]}"
        exit 1
        ;;
esac

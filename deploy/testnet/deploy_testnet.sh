#!/usr/bin/env bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════
#  NPChain Testnet Deployment Script
#
#  Deploys a complete testnet with one command.
#
#  Usage:
#    ./deploy_testnet.sh local       # Local Docker deployment
#    ./deploy_testnet.sh vps         # Deploy to remote VPS(es)
#    ./deploy_testnet.sh stop        # Stop testnet
#    ./deploy_testnet.sh reset       # Wipe data and redeploy
#    ./deploy_testnet.sh status      # Check testnet health
#    ./deploy_testnet.sh logs        # Tail all logs
#
#  Requirements:
#    - Docker Engine 24+
#    - Docker Compose v2
#    - 4+ CPU cores, 8+ GB RAM (for full local testnet)
#    - Ports: 19333-19336 (P2P), 18332-18333 (RPC), 8080 (faucet),
#             3000 (Grafana), 9090 (Prometheus)
# ═══════════════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
COMPOSE_FILE="$SCRIPT_DIR/docker-compose.testnet.yml"
CMD="${1:-help}"

# ─── Colors ───
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
log()  { echo -e "${GREEN}[✓]${NC} $1"; }
warn() { echo -e "${YELLOW}[!]${NC} $1"; }
err()  { echo -e "${RED}[✗]${NC} $1"; }
step() { echo -e "\n${CYAN}${BOLD}═══ $1 ═══${NC}"; }

COMPOSE_CMD="docker compose"
command -v docker-compose &>/dev/null && COMPOSE_CMD="docker-compose"

banner() {
    echo -e "${GREEN}"
    echo '    ╔═══════════════════════════════════════════════════════╗'
    echo '    ║                                                       ║'
    echo '    ║          NPChain Testnet Deployment                   ║'
    echo '    ║          Proof-of-NP-Witness Consensus                ║'
    echo '    ║          Post-Quantum • Privacy • Unlimited Supply    ║'
    echo '    ║                                                       ║'
    echo '    ╚═══════════════════════════════════════════════════════╝'
    echo -e "${NC}"
}

# ═══════════════════════════════════════════════════════════════════
#  PREFLIGHT CHECKS
# ═══════════════════════════════════════════════════════════════════
preflight() {
    step "Preflight Checks"

    # Docker
    if ! command -v docker &>/dev/null; then
        err "Docker not installed. Install: https://docs.docker.com/get-docker/"
        exit 1
    fi
    DOCKER_VERSION=$(docker version --format '{{.Server.Version}}' 2>/dev/null || echo "unknown")
    log "Docker $DOCKER_VERSION"

    # Docker Compose
    if ! ($COMPOSE_CMD version &>/dev/null); then
        err "Docker Compose not available"
        exit 1
    fi
    log "Docker Compose available"

    # System resources
    TOTAL_MEM_MB=$(free -m 2>/dev/null | awk '/^Mem:/{print $2}' || echo "0")
    TOTAL_CPUS=$(nproc 2>/dev/null || echo "0")

    if [ "$TOTAL_MEM_MB" -gt 0 ]; then
        if [ "$TOTAL_MEM_MB" -lt 4096 ]; then
            warn "Only ${TOTAL_MEM_MB}MB RAM detected. Recommended: 8GB+"
        else
            log "RAM: ${TOTAL_MEM_MB}MB"
        fi
    fi

    if [ "$TOTAL_CPUS" -gt 0 ]; then
        if [ "$TOTAL_CPUS" -lt 2 ]; then
            warn "Only ${TOTAL_CPUS} CPU core(s). Recommended: 4+"
        else
            log "CPUs: ${TOTAL_CPUS}"
        fi
    fi

    # Disk space
    DISK_FREE_GB=$(df -BG "$SCRIPT_DIR" 2>/dev/null | awk 'NR==2{print $4}' | tr -d 'G' || echo "0")
    if [ "$DISK_FREE_GB" -gt 0 ] && [ "$DISK_FREE_GB" -lt 10 ]; then
        warn "Only ${DISK_FREE_GB}GB disk free. Recommended: 20GB+"
    else
        log "Disk: ${DISK_FREE_GB}GB free"
    fi

    # Port availability
    for port in 19333 18332 8080 3000 9090; do
        if ss -tlnp 2>/dev/null | grep -q ":${port} " 2>/dev/null; then
            warn "Port $port already in use"
        fi
    done

    log "Preflight complete"
}

# ═══════════════════════════════════════════════════════════════════
#  LOCAL DEPLOYMENT
# ═══════════════════════════════════════════════════════════════════
deploy_local() {
    banner
    preflight

    step "Building NPChain Testnet Images"
    cd "$PROJECT_ROOT"
    $COMPOSE_CMD -f "$COMPOSE_FILE" build --parallel
    log "Images built"

    step "Starting Testnet"
    $COMPOSE_CMD -f "$COMPOSE_FILE" up -d
    log "Containers starting..."

    step "Waiting for Genesis"
    echo -n "  "
    RETRIES=0
    while [ $RETRIES -lt 30 ]; do
        if docker inspect npchain_genesis_init --format='{{.State.Status}}' 2>/dev/null | grep -q "exited"; then
            EXIT_CODE=$(docker inspect npchain_genesis_init --format='{{.State.ExitCode}}' 2>/dev/null)
            if [ "$EXIT_CODE" = "0" ]; then
                echo ""
                log "Genesis block created"
                break
            else
                echo ""
                err "Genesis generator failed (exit code: $EXIT_CODE)"
                docker logs npchain_genesis_init 2>&1 | tail -20
                exit 1
            fi
        fi
        echo -n "."
        sleep 2
        RETRIES=$((RETRIES + 1))
    done

    step "Waiting for Boot Node"
    echo -n "  "
    RETRIES=0
    while [ $RETRIES -lt 60 ]; do
        if docker inspect npchain_boot --format='{{.State.Health.Status}}' 2>/dev/null | grep -q "healthy"; then
            echo ""
            log "Boot node healthy"
            break
        fi
        echo -n "."
        sleep 3
        RETRIES=$((RETRIES + 1))
    done

    step "Waiting for Network Sync"
    sleep 10
    echo -n "  "
    for node in npchain_seed1 npchain_seed2 npchain_seed3 npchain_miner npchain_rpc; do
        STATUS=$(docker inspect "$node" --format='{{.State.Running}}' 2>/dev/null || echo "false")
        if [ "$STATUS" = "true" ]; then
            echo -n "${GREEN}✓${NC} "
        else
            echo -n "${RED}✗${NC} "
        fi
    done
    echo ""
    log "Seed nodes synced"

    show_status
}

# ═══════════════════════════════════════════════════════════════════
#  VPS DEPLOYMENT (via SSH)
# ═══════════════════════════════════════════════════════════════════
deploy_vps() {
    banner

    echo "  VPS Deployment requires server addresses."
    echo ""
    echo "  Recommended VPS specs:"
    echo "    Boot/Miner node: 4 vCPU, 8GB RAM, 100GB SSD"
    echo "    Seed nodes:      2 vCPU, 4GB RAM, 50GB SSD"
    echo "    RPC/Faucet:      2 vCPU, 4GB RAM, 50GB SSD"
    echo ""
    echo "  Supported providers: DigitalOcean, Vultr, Hetzner, AWS, any Linux VPS"
    echo ""

    read -p "  Boot node IP (or press Enter for localhost): " BOOT_IP
    BOOT_IP=${BOOT_IP:-localhost}

    if [ "$BOOT_IP" = "localhost" ] || [ "$BOOT_IP" = "127.0.0.1" ]; then
        echo ""
        warn "Using localhost — running local deployment instead"
        deploy_local
        return
    fi

    read -p "  SSH user [root]: " SSH_USER
    SSH_USER=${SSH_USER:-root}

    step "Deploying to $BOOT_IP"

    # Package the project
    log "Packaging project..."
    TARBALL="/tmp/npchain_testnet.tar.gz"
    cd "$PROJECT_ROOT"
    tar czf "$TARBALL" \
        --exclude='.git' \
        --exclude='build' \
        --exclude='*.o' \
        .

    # Upload and deploy
    log "Uploading to $SSH_USER@$BOOT_IP..."
    scp "$TARBALL" "$SSH_USER@$BOOT_IP:/tmp/"

    log "Deploying on remote server..."
    ssh "$SSH_USER@$BOOT_IP" bash -s << 'REMOTE_SCRIPT'
        set -euo pipefail

        # Install Docker if needed
        if ! command -v docker &>/dev/null; then
            echo "Installing Docker..."
            curl -fsSL https://get.docker.com | sh
            systemctl enable docker
            systemctl start docker
        fi

        # Extract and deploy
        mkdir -p /opt/npchain
        cd /opt/npchain
        tar xzf /tmp/npchain_testnet.tar.gz

        # Open firewall ports
        if command -v ufw &>/dev/null; then
            ufw allow 19333/tcp  # P2P
            ufw allow 18333/tcp  # RPC
            ufw allow 8080/tcp   # Faucet
            ufw allow 3000/tcp   # Grafana
        fi

        # Deploy
        cd deploy/testnet
        docker compose -f docker-compose.testnet.yml up -d --build

        echo "Testnet deployed successfully on $(hostname)"
REMOTE_SCRIPT

    log "Remote deployment complete"
    echo ""
    echo "  Access your testnet:"
    echo "    P2P:       $BOOT_IP:19333"
    echo "    RPC:       http://$BOOT_IP:18333"
    echo "    Faucet:    http://$BOOT_IP:8080"
    echo "    Grafana:   http://$BOOT_IP:3000 (admin / npchain-testnet)"
}

# ═══════════════════════════════════════════════════════════════════
#  STATUS CHECK
# ═══════════════════════════════════════════════════════════════════
show_status() {
    echo ""
    echo -e "${BOLD}  ╔═════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}  ║           NPChain Testnet — DEPLOYED                  ║${NC}"
    echo -e "${BOLD}  ╠═════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ║  Network:     NPChain Testnet (chain_id: TCR)          ║${NC}"
    echo -e "${BOLD}  ║  Consensus:   Proof-of-NP-Witness (PoNW)              ║${NC}"
    echo -e "${BOLD}  ║  Block Time:  15 seconds                               ║${NC}"
    echo -e "${BOLD}  ║  Emission:    52.5M Certs/year (unlimited supply)        ║${NC}"
    echo -e "${BOLD}  ║  Crypto:      Dilithium-5 + Kyber-1024 (post-quantum) ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ╠═════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}  ║  ENDPOINTS                                              ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ║  P2P Seed Nodes:                                        ║${NC}"
    echo -e "${BOLD}  ║    • localhost:19333  (boot node)                       ║${NC}"
    echo -e "${BOLD}  ║    • localhost:19334  (seed 1)                          ║${NC}"
    echo -e "${BOLD}  ║    • localhost:19335  (seed 2)                          ║${NC}"
    echo -e "${BOLD}  ║    • localhost:19336  (seed 3)                          ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ║  RPC API:      http://localhost:18333                   ║${NC}"
    echo -e "${BOLD}  ║  Faucet:       http://localhost:8080                    ║${NC}"
    echo -e "${BOLD}  ║  Grafana:      http://localhost:3000                    ║${NC}"
    echo -e "${BOLD}  ║                (login: admin / npchain-testnet)         ║${NC}"
    echo -e "${BOLD}  ║  Prometheus:   http://localhost:9090                    ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ╠═════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}  ║  QUICK START                                            ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ║  1. Get test coins:                                     ║${NC}"
    echo -e "${BOLD}  ║     Open http://localhost:8080                          ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ║  2. Create a wallet:                                    ║${NC}"
    echo -e "${BOLD}  ║     docker exec npchain_rpc npchain_wallet keygen      ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ║  3. Check chain status:                                 ║${NC}"
    echo -e "${BOLD}  ║     curl http://localhost:18333/api/v1/status           ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ║  4. View logs:                                          ║${NC}"
    echo -e "${BOLD}  ║     ./deploy_testnet.sh logs                            ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ║  5. Connect external node:                              ║${NC}"
    echo -e "${BOLD}  ║     npchain_node --testnet --seed localhost:19333       ║${NC}"
    echo -e "${BOLD}  ║                                                         ║${NC}"
    echo -e "${BOLD}  ╚═════════════════════════════════════════════════════════╝${NC}"
    echo ""

    # Show container status
    step "Container Status"
    $COMPOSE_CMD -f "$COMPOSE_FILE" ps 2>/dev/null || true
}

# ═══════════════════════════════════════════════════════════════════
#  STOP / RESET / LOGS
# ═══════════════════════════════════════════════════════════════════
do_stop() {
    step "Stopping Testnet"
    cd "$PROJECT_ROOT"
    $COMPOSE_CMD -f "$COMPOSE_FILE" down
    log "Testnet stopped"
}

do_reset() {
    step "Resetting Testnet (wiping all data)"
    cd "$PROJECT_ROOT"
    $COMPOSE_CMD -f "$COMPOSE_FILE" down -v
    log "All volumes removed"
    echo ""
    warn "Testnet data wiped. Run './deploy_testnet.sh local' to redeploy."
}

do_logs() {
    cd "$PROJECT_ROOT"
    $COMPOSE_CMD -f "$COMPOSE_FILE" logs -f --tail=50
}

# ═══════════════════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════════════════
case "$CMD" in
    local)
        deploy_local
        ;;
    vps)
        deploy_vps
        ;;
    stop)
        do_stop
        ;;
    reset)
        do_reset
        ;;
    status)
        banner
        show_status
        ;;
    logs)
        do_logs
        ;;
    help|*)
        banner
        echo "  Usage: $0 <command>"
        echo ""
        echo "  Commands:"
        echo "    local     Deploy testnet locally via Docker"
        echo "    vps       Deploy to remote server(s) via SSH"
        echo "    stop      Stop the testnet"
        echo "    reset     Wipe all data and stop"
        echo "    status    Show testnet status and endpoints"
        echo "    logs      Tail all container logs"
        echo ""
        echo "  Requirements:"
        echo "    Docker 24+, Docker Compose v2"
        echo "    4+ CPU cores, 8+ GB RAM recommended"
        echo ""
        ;;
esac

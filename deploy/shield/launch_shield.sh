#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  NPChain 8-Proxy Shield — Launch Script
#
#  Starts the full 8-proxy shield + core testnet node.
#  External peers connect to port 19333 (P2P) and 18333 (RPC).
#  All traffic passes through 8 security layers before reaching
#  the core node.
#
#  Usage:
#    ./launch_shield.sh                                    # Default address
#    ./launch_shield.sh cert1YOUR_ADDRESS                  # Custom miner address
#    MINER_ADDRESS=cert1... ./launch_shield.sh             # Via env var
# ═══════════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Miner address
if [ -n "$1" ]; then
    export MINER_ADDRESS="$1"
elif [ -z "$MINER_ADDRESS" ]; then
    export MINER_ADDRESS="cert189e6b5cd23fe7fabb499b8a7ae781e8b77df5755"
fi

echo ""
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║         NPChain 8-Proxy Shield — Testnet Launch          ║"
echo "╠═══════════════════════════════════════════════════════════╣"
echo "║                                                           ║"
echo "║  INTERNET                                                 ║"
echo "║    │                                                      ║"
echo "║    ▼ :19333 (P2P)                                        ║"
echo "║  ┌─────────────────────────────────────────────────────┐  ║"
echo "║  │ P1: Edge Sentinel    — DDoS absorption              │  ║"
echo "║  │ P2: Protocol Gate    — Wire format validation       │  ║"
echo "║  │ P3: Rate Governor    — Token-bucket rate limiting   │  ║"
echo "║  │ P4: Identity Verify  — Peer authentication          │  ║"
echo "║  │ P5: Content Inspect  — Deep packet inspection       │  ║"
echo "║  │ P6: Anomaly Detect   — Behavioral analysis          │  ║"
echo "║  │ P7: Encrypt Bridge   — Internal re-encryption       │  ║"
echo "║  │ P8: Final Gateway    — Circuit breaker              │  ║"
echo "║  └─────────────────────────────────────────────────────┘  ║"
echo "║    │                                                      ║"
echo "║    ▼                                                      ║"
echo "║  ┌─────────────────────────────────────────────────────┐  ║"
echo "║  │           CORE NODE (no public ports)               │  ║"
echo "║  │           Mining: $MINER_ADDRESS  ║"
echo "║  └─────────────────────────────────────────────────────┘  ║"
echo "║                                                           ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# Build and launch
echo "[SHIELD] Building containers..."
docker-compose build --parallel 2>&1 | tail -5

echo ""
echo "[SHIELD] Starting 8-proxy shield + core node..."
docker-compose up -d

echo ""
echo "[SHIELD] Waiting for containers to start..."
sleep 3

echo ""
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║  SHIELD ACTIVE — All 8 layers running                    ║"
echo "╠═══════════════════════════════════════════════════════════╣"
echo "║                                                           ║"
echo "║  P2P endpoint:   YOUR_PUBLIC_IP:19333                    ║"
echo "║  RPC endpoint:   YOUR_PUBLIC_IP:18333                    ║"
echo "║  Explorer:       http://localhost:18333/api/v1/status    ║"
echo "║                                                           ║"
echo "║  Testers connect with:                                    ║"
echo "║  ./npchain_testnet --seed YOUR_PUBLIC_IP:19333            ║"
echo "║                                                           ║"
echo "║  Commands:                                                ║"
echo "║    docker-compose logs -f              # All logs         ║"
echo "║    docker-compose logs -f core_node    # Node only        ║"
echo "║    docker-compose logs -f proxy_1_edge # Edge proxy       ║"
echo "║    docker-compose ps                   # Container status ║"
echo "║    docker-compose down                 # Stop everything  ║"
echo "║                                                           ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# Show container status
docker-compose ps

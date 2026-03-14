#!/usr/bin/env bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════
#  NPChain Testnet Node Entrypoint
#  Routes to the correct binary based on NPCHAIN_MINING env var.
# ═══════════════════════════════════════════════════════════════════

echo ""
echo "  ┌──────────────────────────────────────────────┐"
echo "  │  NPChain Testnet Node Starting...            │"
echo "  └──────────────────────────────────────────────┘"
echo ""
echo "  Network:    ${NPCHAIN_NETWORK:-testnet}"
echo "  Data dir:   ${NPCHAIN_DATA_DIR:-/data/npchain/testnet}"
echo "  Mining:     ${NPCHAIN_MINING:-false}"
echo "  RPC:        ${NPCHAIN_RPC:-false}"
echo "  Seeds:      ${NPCHAIN_SEED_NODES:-none}"
echo ""

# Wait for genesis block to be available
GENESIS="${NPCHAIN_GENESIS:-/genesis/genesis.bin}"
echo "  Waiting for genesis block at ${GENESIS}..."
RETRIES=0
while [ ! -f "$GENESIS" ] && [ $RETRIES -lt 60 ]; do
    sleep 2
    RETRIES=$((RETRIES + 1))
done

if [ ! -f "$GENESIS" ]; then
    echo "  ERROR: Genesis block not found after 120 seconds"
    exit 1
fi
echo "  Genesis block found ($(stat -c%s "$GENESIS" 2>/dev/null || echo '?') bytes)"

# Copy genesis to data dir if not already there
DATA_DIR="${NPCHAIN_DATA_DIR:-/data/npchain/testnet}"
mkdir -p "$DATA_DIR"
if [ ! -f "$DATA_DIR/genesis.bin" ]; then
    cp "$GENESIS" "$DATA_DIR/genesis.bin"
    echo "  Genesis copied to data directory"
fi

# Build command arguments
ARGS=(
    "--testnet"
    "--datadir" "$DATA_DIR"
    "--genesis" "$DATA_DIR/genesis.bin"
    "--port" "${NPCHAIN_LISTEN_PORT:-19333}"
)

# Add seed nodes
if [ -n "${NPCHAIN_SEED_NODES:-}" ]; then
    IFS=',' read -ra SEEDS <<< "$NPCHAIN_SEED_NODES"
    for seed in "${SEEDS[@]}"; do
        ARGS+=("--seed" "$seed")
    done
fi

# Add RPC if enabled
if [ "${NPCHAIN_RPC:-false}" = "true" ]; then
    ARGS+=(
        "--rpc"
        "--rpc-port" "${NPCHAIN_RPC_PORT:-18332}"
        "--rpc-host" "0.0.0.0"
    )
    if [ -n "${NPCHAIN_RPC_KEY:-}" ]; then
        ARGS+=("--rpc-key" "$NPCHAIN_RPC_KEY")
    fi
fi

# Add config file if specified
if [ -n "${NPCHAIN_CONFIG:-}" ] && [ -f "${NPCHAIN_CONFIG}" ]; then
    ARGS+=("--config" "$NPCHAIN_CONFIG")
fi

# Add log level
ARGS+=("--log-level" "${NPCHAIN_LOG_LEVEL:-info}")

# Decide: mine or just run node
if [ "${NPCHAIN_MINING:-false}" = "true" ]; then
    echo ""
    echo "  Starting MINER (mining enabled)..."
    echo "  Threads: ${NPCHAIN_MINING_THREADS:-auto}"
    echo ""

    if [ "${NPCHAIN_MINING_THREADS:-0}" != "0" ]; then
        ARGS+=("--mining-threads" "$NPCHAIN_MINING_THREADS")
    fi

    exec /usr/local/bin/npchain_miner "${ARGS[@]}"
else
    echo ""
    echo "  Starting FULL NODE (no mining)..."
    echo ""
    exec /usr/local/bin/npchain_node "${ARGS[@]}"
fi

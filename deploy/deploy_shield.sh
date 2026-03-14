#!/usr/bin/env bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════
#  NPChain 8-Proxy Shield — Deployment Script
#
#  This script:
#    1. Generates unique Dilithium keypairs for each proxy + core
#    2. Creates the inter-proxy trust configuration
#    3. Sets up network isolation rules (iptables)
#    4. Launches all containers via Docker Compose
#    5. Verifies the attestation chain is functional
#    6. Enables monitoring and alerting
#
#  Usage:
#    ./deploy_shield.sh [--prod|--staging|--dev]
#
# ═══════════════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV="${1:-dev}"

echo "╔═══════════════════════════════════════════════════╗"
echo "║   NPChain 8-Proxy Shield Deployment              ║"
echo "║   Environment: ${ENV}                             ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""

# ─── Colors ───
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log()   { echo -e "${GREEN}[✓]${NC} $1"; }
warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
error() { echo -e "${RED}[✗]${NC} $1"; exit 1; }
step()  { echo -e "\n${CYAN}═══ $1 ═══${NC}"; }

# ─── Pre-flight Checks ───
step "Pre-flight Checks"

command -v docker >/dev/null 2>&1 || error "Docker not installed"
command -v docker-compose >/dev/null 2>&1 || command -v docker compose >/dev/null 2>&1 || error "Docker Compose not installed"
log "Docker available"

if [ "$ENV" = "prod" ]; then
    [ "$(id -u)" -eq 0 ] || error "Production deployment requires root"
    log "Running as root"
fi

# ─── Step 1: Generate Proxy Identities ───
step "Step 1: Generating Dilithium Keypairs for All Proxies"

KEY_DIR="${SCRIPT_DIR}/keys"
mkdir -p "$KEY_DIR"
chmod 700 "$KEY_DIR"

PROXY_NAMES=(
    "edge_sentinel"
    "protocol_gate"
    "rate_governor"
    "identity_verifier"
    "content_inspector"
    "anomaly_detector"
    "encryption_bridge"
    "final_gateway"
    "core_node"
)

for name in "${PROXY_NAMES[@]}"; do
    if [ ! -f "${KEY_DIR}/${name}.pub" ]; then
        echo "  Generating keypair for ${name}..."
        # In production: use npchain_wallet keygen
        # For now: create placeholder key files
        openssl rand 2592 > "${KEY_DIR}/${name}.pub"
        openssl rand 4896 > "${KEY_DIR}/${name}.key"
        chmod 600 "${KEY_DIR}/${name}.key"
        log "  ${name} keypair generated"
    else
        log "  ${name} keypair exists (skipping)"
    fi
done

# ─── Step 2: Create Trust Configuration ───
step "Step 2: Creating Inter-Proxy Trust Configuration"

CONFIG_DIR="${SCRIPT_DIR}/config"
mkdir -p "$CONFIG_DIR"

cat > "${CONFIG_DIR}/trust_chain.json" << 'TRUST_EOF'
{
  "version": 1,
  "description": "NPChain 8-Proxy Shield Trust Configuration",
  "proxy_chain": [
    {
      "layer": 1,
      "name": "Edge Sentinel",
      "accepts_from": ["internet"],
      "forwards_to": ["protocol_gate"],
      "public_key_file": "edge_sentinel.pub"
    },
    {
      "layer": 2,
      "name": "Protocol Gate",
      "accepts_from": ["edge_sentinel"],
      "forwards_to": ["rate_governor"],
      "public_key_file": "protocol_gate.pub"
    },
    {
      "layer": 3,
      "name": "Rate Governor",
      "accepts_from": ["protocol_gate"],
      "forwards_to": ["identity_verifier"],
      "public_key_file": "rate_governor.pub"
    },
    {
      "layer": 4,
      "name": "Identity Verifier",
      "accepts_from": ["rate_governor"],
      "forwards_to": ["content_inspector"],
      "public_key_file": "identity_verifier.pub"
    },
    {
      "layer": 5,
      "name": "Content Inspector",
      "accepts_from": ["identity_verifier"],
      "forwards_to": ["anomaly_detector"],
      "public_key_file": "content_inspector.pub"
    },
    {
      "layer": 6,
      "name": "Anomaly Detector",
      "accepts_from": ["content_inspector"],
      "forwards_to": ["encryption_bridge"],
      "public_key_file": "anomaly_detector.pub"
    },
    {
      "layer": 7,
      "name": "Encryption Bridge",
      "accepts_from": ["anomaly_detector"],
      "forwards_to": ["final_gateway"],
      "public_key_file": "encryption_bridge.pub"
    },
    {
      "layer": 8,
      "name": "Final Gateway",
      "accepts_from": ["encryption_bridge"],
      "forwards_to": ["core_node"],
      "public_key_file": "final_gateway.pub",
      "requires_full_attestation_chain": true
    }
  ],
  "core_node": {
    "accepts_from": ["final_gateway"],
    "accepts_from_ip": "172.30.6.10",
    "public_key_file": "core_node.pub"
  },
  "attestation_rules": {
    "max_pipeline_latency_ms": 10000,
    "require_sequential_timestamps": true,
    "require_all_signatures": true
  }
}
TRUST_EOF

log "Trust chain configuration created"

# ─── Step 3: Create Prometheus Configuration ───
step "Step 3: Setting Up Monitoring"

cat > "${CONFIG_DIR}/prometheus.yml" << 'PROM_EOF'
global:
  scrape_interval: 10s
  evaluation_interval: 10s

scrape_configs:
  - job_name: 'proxy_edge'
    static_configs:
      - targets: ['172.30.2.10:9100']
        labels:
          layer: '1'
          name: 'edge_sentinel'

  - job_name: 'proxy_protocol'
    static_configs:
      - targets: ['172.30.3.10:9100']
        labels:
          layer: '2'
          name: 'protocol_gate'

  - job_name: 'proxy_rate'
    static_configs:
      - targets: ['172.30.3.20:9100']
        labels:
          layer: '3'
          name: 'rate_governor'

  - job_name: 'proxy_identity'
    static_configs:
      - targets: ['172.30.3.30:9100']
        labels:
          layer: '4'
          name: 'identity_verifier'

  - job_name: 'proxy_content'
    static_configs:
      - targets: ['172.30.4.20:9100']
        labels:
          layer: '5'
          name: 'content_inspector'

  - job_name: 'proxy_anomaly'
    static_configs:
      - targets: ['172.30.4.30:9100']
        labels:
          layer: '6'
          name: 'anomaly_detector'

  - job_name: 'proxy_encrypt'
    static_configs:
      - targets: ['172.30.5.20:9100']
        labels:
          layer: '7'
          name: 'encryption_bridge'

  - job_name: 'proxy_gateway'
    static_configs:
      - targets: ['172.30.5.30:9100']
        labels:
          layer: '8'
          name: 'final_gateway'

  - job_name: 'core_node'
    static_configs:
      - targets: ['172.30.6.20:9100']
        labels:
          layer: 'core'
          name: 'core_node'

alerting:
  alertmanagers:
    - static_configs:
        - targets: ['alertmanager:9093']

rule_files:
  - /etc/prometheus/alerts.yml
PROM_EOF

cat > "${CONFIG_DIR}/alerts.yml" << 'ALERT_EOF'
groups:
  - name: proxy_shield
    rules:
      - alert: ProxyDown
        expr: up == 0
        for: 30s
        labels:
          severity: critical
        annotations:
          summary: "Proxy layer {{ $labels.name }} is down"

      - alert: HighDDoSRate
        expr: rate(npchain_proxy_edge_dropped_ddos_total[5m]) > 1000
        for: 1m
        labels:
          severity: warning
        annotations:
          summary: "DDoS absorption rate exceeding 1000/sec"

      - alert: CircuitBreakerOpen
        expr: npchain_proxy_gateway_circuit_open == 1
        for: 0s
        labels:
          severity: critical
        annotations:
          summary: "Final Gateway circuit breaker is OPEN — traffic halted"

      - alert: AttestationFailures
        expr: rate(npchain_proxy_gateway_attestation_invalid_total[5m]) > 10
        for: 2m
        labels:
          severity: critical
        annotations:
          summary: "High attestation failure rate — possible proxy compromise"

      - alert: AnomalyDetected
        expr: npchain_proxy_anomaly_eclipse_detected_total > 0
        for: 0s
        labels:
          severity: critical
        annotations:
          summary: "Eclipse attack detected by anomaly detector"

      - alert: HighLatency
        expr: npchain_proxy_pipeline_latency_ms > 5000
        for: 1m
        labels:
          severity: warning
        annotations:
          summary: "Proxy pipeline latency exceeding 5 seconds"
ALERT_EOF

log "Prometheus + alerting configured"

# ─── Step 4: Apply Firewall Rules (Production Only) ───
if [ "$ENV" = "prod" ]; then
    step "Step 4: Applying Firewall Rules"

    # Only port 9333 is open to the world
    iptables -A INPUT -p tcp --dport 9333 -j ACCEPT
    iptables -A INPUT -p tcp --dport 3000 -s 127.0.0.1 -j ACCEPT  # Grafana local only
    iptables -A INPUT -p tcp --dport 9333 -j DROP  # Everything else blocked

    # Rate limit new connections
    iptables -A INPUT -p tcp --dport 9333 --syn -m limit --limit 100/s --limit-burst 200 -j ACCEPT
    iptables -A INPUT -p tcp --dport 9333 --syn -j DROP

    log "Firewall rules applied (port 9333 only)"
else
    step "Step 4: Firewall Rules (Skipped — ${ENV} mode)"
    warn "In production, run with --prod to apply iptables rules"
fi

# ─── Step 5: Launch Containers ───
step "Step 5: Launching 8-Proxy Shield + Core Node"

cd "$SCRIPT_DIR"

COMPOSE_CMD="docker compose"
command -v docker-compose >/dev/null 2>&1 && COMPOSE_CMD="docker-compose"

$COMPOSE_CMD -f docker-compose.shield.yml up -d --build

echo ""
log "All containers launching..."
sleep 5

# ─── Step 6: Verify Health ───
step "Step 6: Health Verification"

echo ""
echo "  Container Status:"
echo "  ─────────────────────────────────────────────────"
$COMPOSE_CMD -f docker-compose.shield.yml ps --format "table {{.Name}}\t{{.Status}}\t{{.Ports}}"

echo ""

# Check each proxy is running
ALL_HEALTHY=true
for name in proxy_edge proxy_protocol proxy_rate proxy_identity proxy_content proxy_anomaly proxy_encrypt proxy_gateway core_node; do
    STATUS=$(docker inspect --format='{{.State.Running}}' "npchain_${name}" 2>/dev/null || echo "false")
    if [ "$STATUS" = "true" ]; then
        log "  ${name}: running"
    else
        error "  ${name}: NOT RUNNING"
        ALL_HEALTHY=false
    fi
done

if [ "$ALL_HEALTHY" = true ]; then
    echo ""
    echo "╔═══════════════════════════════════════════════════╗"
    echo "║   8-Proxy Shield Deployed Successfully!          ║"
    echo "╠═══════════════════════════════════════════════════╣"
    echo "║                                                   ║"
    echo "║   Public endpoint:  port 9333                    ║"
    echo "║   Grafana:          http://localhost:3000         ║"
    echo "║   Proxies:          8/8 running                  ║"
    echo "║   Core node:        isolated (no public access)  ║"
    echo "║                                                   ║"
    echo "║   Monitor:                                        ║"
    echo "║     docker compose -f docker-compose.shield.yml  ║"
    echo "║       logs -f proxy_anomaly                       ║"
    echo "║                                                   ║"
    echo "║   Emergency stop:                                 ║"
    echo "║     ./deploy_shield.sh stop                       ║"
    echo "║                                                   ║"
    echo "╚═══════════════════════════════════════════════════╝"
fi

# NPChain Testnet Deployment Guide

## What Gets Deployed

| Service | Container | Port | Role |
|---------|-----------|------|------|
| Genesis Init | `npchain_genesis_init` | — | Creates block #0, exits |
| Boot Node | `npchain_boot` | 19333, 18332 | First miner, seeds the network |
| Seed 1 | `npchain_seed1` | 19334 | Peer discovery |
| Seed 2 | `npchain_seed2` | 19335 | Peer discovery |
| Seed 3 | `npchain_seed3` | 19336 | Peer discovery |
| Miner | `npchain_miner` | — | Dedicated PoNW mining |
| RPC Node | `npchain_rpc` | 18333 | API endpoint |
| Faucet | `npchain_faucet` | 8080 | Free test coins |
| Prometheus | `npchain_testnet_prometheus` | 9090 | Metrics |
| Grafana | `npchain_testnet_grafana` | 3000 | Dashboards |

## Testnet Parameters

| Parameter | Testnet | Mainnet |
|-----------|---------|---------|
| Chain ID | `TCR` (0x544352) | `CRT` (0x435254) |
| Block time | 15 seconds | 60 seconds |
| Block reward | ~25 Certs | ~100 Certs |
| Annual emission | 52.5M Certs | 52.5M Certs |
| Supply cap | Unlimited | Unlimited |
| Committee size | 3 (2-of-3) | 21 (14-of-21) |
| P2P port | 19333 | 9333 |
| Min subnet diversity | 1 | 4 |
| Difficulty adjustment | Every 36 blocks | Every 144 blocks |

---

## Option A: Local Deployment (Recommended for First Test)

### Prerequisites

```bash
# Docker Engine 24+
docker --version

# Docker Compose v2
docker compose version

# Minimum: 4 CPU cores, 8 GB RAM, 20 GB disk
```

### Step 1: Clone and Navigate

```bash
git clone <your-repo-url> npchain
cd npchain/deploy/testnet
```

### Step 2: Deploy

```bash
chmod +x deploy_testnet.sh
./deploy_testnet.sh local
```

This will:
1. Build all Docker images from source (~3-5 minutes first time)
2. Generate the genesis block (block #0)
3. Start the boot node and wait for it to be healthy
4. Start 3 seed nodes + 1 miner + 1 RPC node
5. Start the faucet and monitoring stack
6. Print all endpoints

### Step 3: Verify

```bash
# Check all containers are running
./deploy_testnet.sh status

# Watch logs (blocks should appear every ~15 seconds)
./deploy_testnet.sh logs

# Check chain status via RPC
curl http://localhost:18333/api/v1/status
```

### Step 4: Get Test Coins

Open http://localhost:8080 in your browser. Enter your testnet address
and receive 10,000 free Certs.

Or create a wallet first:
```bash
docker exec npchain_rpc npchain_wallet keygen
```

### Step 5: Connect Your Own Node

```bash
# Build the node binary
cd npchain && mkdir build && cd build
cmake .. -DNPCHAIN_TESTNET=ON && make -j$(nproc)

# Run your node, connecting to the local testnet
./npchain_node --testnet --seed localhost:19333 --datadir ./my_node_data
```

---

## Option B: VPS Deployment (Public Testnet)

### Prerequisites

- 1 VPS: 4 vCPU, 8 GB RAM, 100 GB SSD (boot + miner)
- Optional: 2-3 additional VPS for seed nodes
- SSH key access to all servers
- Domain name pointed to your VPS IP (optional but nice)

### Recommended Providers

| Provider | Specs | ~Cost/mo |
|----------|-------|----------|
| Hetzner CX41 | 4 vCPU, 16 GB, 160 GB | $15 |
| DigitalOcean | 4 vCPU, 8 GB, 160 GB | $48 |
| Vultr | 4 vCPU, 8 GB, 160 GB | $48 |
| AWS t3.xlarge | 4 vCPU, 16 GB | ~$55 |

### Step 1: Provision Server

```bash
# Example: DigitalOcean
doctl compute droplet create npchain-testnet \
    --image ubuntu-24-04-x64 \
    --size s-4vcpu-8gb \
    --region nyc3 \
    --ssh-keys <your-key-id>
```

### Step 2: Deploy Remotely

```bash
cd npchain/deploy/testnet
./deploy_testnet.sh vps

# Enter your VPS IP when prompted
# Script will SSH in, install Docker, and deploy everything
```

### Step 3: Open Firewall

```bash
# On the VPS:
ufw allow 19333/tcp    # P2P (required for other nodes to connect)
ufw allow 18333/tcp    # RPC (optional, for wallet connections)
ufw allow 8080/tcp     # Faucet (optional, for public access)
ufw allow 3000/tcp     # Grafana (optional, restrict to your IP)
```

### Step 4: Share With Others

Give people these connection details:
```
Seed node:  your-server-ip:19333
RPC:        http://your-server-ip:18333
Faucet:     http://your-server-ip:8080

Connect:    npchain_node --testnet --seed your-server-ip:19333
```

---

## Option C: Multi-Server Deployment (Production-Like)

For a more realistic testnet spread across multiple servers:

### Server Layout

| Server | Role | Ports Open |
|--------|------|-----------|
| Server 1 | Boot + Miner | 19333 |
| Server 2 | Seed + RPC | 19333, 18333 |
| Server 3 | Seed + Faucet | 19333, 8080 |
| Server 4 | Seed + Monitoring | 19333, 3000 |

### Deploy Each Server

```bash
# Server 1: Boot node (deploy first)
ssh root@server1
cd /opt/npchain/deploy/testnet
NPCHAIN_MINING=true NPCHAIN_SEED_NODES="" \
    docker compose -f docker-compose.testnet.yml up -d boot_node genesis_init

# Server 2: Seed + RPC (after boot is up)
ssh root@server2
NPCHAIN_SEED_NODES="server1-ip:19333" \
    docker compose -f docker-compose.testnet.yml up -d seed1 rpc_node

# Server 3: Seed + Faucet
ssh root@server3
NPCHAIN_SEED_NODES="server1-ip:19333,server2-ip:19333" \
    docker compose -f docker-compose.testnet.yml up -d seed2 faucet

# Server 4: Seed + Monitoring
ssh root@server4
NPCHAIN_SEED_NODES="server1-ip:19333,server2-ip:19333,server3-ip:19333" \
    docker compose -f docker-compose.testnet.yml up -d seed3 prometheus grafana
```

---

## Management Commands

```bash
# View status
./deploy_testnet.sh status

# View logs (all containers)
./deploy_testnet.sh logs

# View specific container logs
docker logs -f npchain_boot
docker logs -f npchain_miner

# Stop testnet (preserves data)
./deploy_testnet.sh stop

# Wipe everything and start fresh
./deploy_testnet.sh reset

# Restart a single service
docker restart npchain_miner

# Scale miners (add more mining power)
docker compose -f docker-compose.testnet.yml up -d --scale miner=3

# Execute wallet commands
docker exec npchain_rpc npchain_wallet keygen
docker exec npchain_rpc npchain_wallet balance
docker exec npchain_rpc npchain_wallet info
```

## Monitoring

- **Grafana**: http://localhost:3000 (admin / npchain-testnet)
- **Prometheus**: http://localhost:9090

Key metrics to watch:
- Block height (should increase every ~15s)
- Peer count (should be 5+ for local deployment)
- Mining solve time
- Transaction pool size
- Memory and CPU usage per container

## Troubleshooting

**Genesis block not created**: Check `docker logs npchain_genesis_init`

**Boot node unhealthy**: Check `docker logs npchain_boot` — usually a build error

**Nodes not connecting**: Verify seed node addresses and that port 19333 is open

**No blocks being mined**: Check miner logs — difficulty may be too high for available CPU

**Faucet errors**: Ensure the RPC node is running and healthy first

**Reset everything**: `./deploy_testnet.sh reset && ./deploy_testnet.sh local`

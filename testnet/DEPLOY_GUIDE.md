# NPChain Testnet Deployment Guide

## Quick Start (Single Server)

The fastest way to get the testnet running. One server, one command.

### Prerequisites

**Minimum hardware:**
- 4 CPU cores
- 8 GB RAM
- 50 GB SSD
- Ubuntu 22.04+ or Debian 12+

**Install Docker:**

```bash
# Install Docker
curl -fsSL https://get.docker.com | sh
sudo usermod -aG docker $USER

# Log out and back in, then verify
docker --version
docker compose version
```

### Launch

```bash
# Clone the repo
git clone https://github.com/your-org/npchain.git
cd npchain/testnet

# Make script executable
chmod +x launch_testnet.sh

# Launch everything
./launch_testnet.sh
```

That's it. The script will build all binaries from source, generate the genesis block, start 3 seed nodes, start the miner, launch the faucet and block explorer, and set up monitoring. Takes 3-5 minutes the first time (building C++), then about 30 seconds on subsequent launches.

### What happens during launch

1. **Build phase** — Compiles the C++20 codebase inside Docker (multi-stage build, clean environment)
2. **Genesis phase** — Generates the genesis block with a Dilithium-5 keypair, creates the faucet key
3. **Seed nodes** — 3 full nodes start, form a P2P mesh, load the genesis block
4. **Health check** — Script waits until seed1 passes health checks (RPC responding)
5. **Miner** — Connects to all 3 seeds, begins solving NP-complete instances
6. **Services** — Faucet, explorer, and Grafana come online

### Verify it's working

```bash
# Check all containers
./launch_testnet.sh status

# Watch the miner find blocks in real time
./launch_testnet.sh logs miner

# Check chain height via RPC
curl http://localhost:18332/status

# Open block explorer
open http://localhost:8080

# Get test tokens
curl http://localhost:8081/drip?address=YOUR_ADDRESS_HERE
```

### Daily operations

```bash
# Stop the testnet (preserves data)
./launch_testnet.sh stop

# Restart (uses existing data)
./launch_testnet.sh start

# Full reset (wipes all chain data, new genesis)
./launch_testnet.sh reset

# Tail specific service logs
./launch_testnet.sh logs seed1
./launch_testnet.sh logs miner
./launch_testnet.sh logs faucet
```

---

## Multi-Server Deployment (Distributed)

For a more realistic testnet across multiple machines.

### Server Layout

| Server | Role | Specs | Ports |
|--------|------|-------|-------|
| Server A | Seed 1 + RPC + Explorer | 2 CPU, 4 GB | 19333, 18332, 8080 |
| Server B | Seed 2 + Faucet | 2 CPU, 4 GB | 19333, 8081 |
| Server C | Seed 3 + Monitoring | 2 CPU, 4 GB | 19333, 3000 |
| Server D | Miner | 4+ CPU, 8 GB | 19333 |

### Step 1: Build on every server

```bash
# On each server
git clone https://github.com/your-org/npchain.git
cd npchain

# Build the binaries natively (no Docker)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Binaries are in build/
ls -la npchain_node npchain_miner npchain_wallet npchain_genesis
```

### Step 2: Generate genesis (on Server A only)

```bash
# Generate genesis block
./build/npchain_genesis --testnet --output genesis.dat

# Copy genesis.dat to ALL other servers
scp genesis.dat serverB:~/npchain/
scp genesis.dat serverC:~/npchain/
scp genesis.dat serverD:~/npchain/

# Also copy the faucet key to Server B
scp genesis_faucet.key serverB:~/npchain/
```

### Step 3: Start seed nodes

```bash
# Server A
./build/npchain_node \
    --testnet \
    --datadir ./data \
    --genesis ./genesis.dat \
    --port 19333 \
    --rpc-port 18332 \
    --rpc-bind 0.0.0.0 \
    --seed \
    --peers SERVERB_IP:19333,SERVERC_IP:19333 \
    --block-time 10

# Server B
./build/npchain_node \
    --testnet \
    --datadir ./data \
    --genesis ./genesis.dat \
    --port 19333 \
    --seed \
    --peers SERVERA_IP:19333,SERVERC_IP:19333 \
    --block-time 10

# Server C
./build/npchain_node \
    --testnet \
    --datadir ./data \
    --genesis ./genesis.dat \
    --port 19333 \
    --seed \
    --peers SERVERA_IP:19333,SERVERB_IP:19333 \
    --block-time 10
```

### Step 4: Start miner

```bash
# Server D
./build/npchain_miner \
    --testnet \
    --datadir ./data \
    --genesis ./genesis.dat \
    --peers SERVERA_IP:19333,SERVERB_IP:19333,SERVERC_IP:19333 \
    --threads $(nproc) \
    --block-time 10
```

### Step 5: Firewall rules

```bash
# On each server — only open what's needed
sudo ufw allow 19333/tcp    # P2P
sudo ufw allow 18332/tcp    # RPC (only on Server A, restrict to trusted IPs)
sudo ufw allow 8080/tcp     # Explorer (Server A)
sudo ufw allow 8081/tcp     # Faucet (Server B)
sudo ufw enable
```

---

## Cloud Deployment (AWS / GCP / Azure)

### AWS Quick Deploy

```bash
# 1. Launch 4 EC2 instances (t3.large for seeds, c5.2xlarge for miner)
# 2. Security group: allow TCP 19333 between instances, 18332/8080/8081 from your IP
# 3. On each instance:

sudo apt update && sudo apt install -y build-essential cmake git libssl-dev
git clone https://github.com/your-org/npchain.git
cd npchain && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# Then follow the multi-server steps above using private IPs
```

### Systemd Service (for persistent running)

```bash
# Create service file
sudo tee /etc/systemd/system/npchain-node.service << 'EOF'
[Unit]
Description=NPChain Testnet Node
After=network.target

[Service]
Type=simple
User=npchain
Group=npchain
WorkingDirectory=/home/npchain/npchain
ExecStart=/home/npchain/npchain/build/npchain_node \
    --testnet \
    --datadir /home/npchain/data \
    --genesis /home/npchain/npchain/genesis.dat \
    --port 19333 \
    --rpc-port 18332 \
    --rpc-bind 0.0.0.0 \
    --seed \
    --block-time 10
Restart=always
RestartSec=10
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF

# Create user and enable
sudo useradd -m -s /bin/bash npchain
sudo systemctl daemon-reload
sudo systemctl enable npchain-node
sudo systemctl start npchain-node
sudo journalctl -u npchain-node -f
```

---

## Connecting Your Own Node

Anyone can join the testnet:

```bash
# Build NPChain
git clone https://github.com/your-org/npchain.git
cd npchain && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# Get the genesis block from any seed node
curl http://SEED_IP:18332/genesis -o genesis.dat

# Start your node
./npchain_node \
    --testnet \
    --datadir ./my-data \
    --genesis ./genesis.dat \
    --peers SEED_IP:19333

# Start mining (optional)
./npchain_miner \
    --testnet \
    --datadir ./my-data \
    --genesis ./genesis.dat \
    --peers SEED_IP:19333 \
    --threads $(nproc)

# Create a wallet
./npchain_wallet --testnet keygen

# Get test tokens from faucet
curl http://SEED_IP:8081/drip?address=YOUR_ADDRESS
```

---

## Testnet Parameters

| Parameter | Value |
|-----------|-------|
| Network name | NPChain Testnet |
| Chain ID | 0x544352 ("TCR") |
| Token | tCerts (no real value) |
| Block time | ~10 seconds |
| Annual emission | 52.5M tCerts/year |
| Block reward | ~16.65 tCerts/block |
| Supply cap | Unlimited |
| Consensus | Proof-of-NP-Witness |
| NP problems | k-SAT, Subset-Sum, Graph Coloring, Hamiltonian Path |
| Difficulty window | 30 blocks |
| Signatures | CRYSTALS-Dilithium Level 5 |
| Key exchange | CRYSTALS-Kyber-1024 |
| Hash | SHA3-256 / SHAKE-256 / BLAKE3 |
| P2P port | 19333 |
| RPC port | 18332 |
| Privacy | Stealth addresses, confidential transactions |

---

## Troubleshooting

**Docker build fails:**
Ensure you have at least 4 GB RAM available for the C++ compilation. The build is parallelized and memory-hungry.

**Seed nodes can't find each other:**
Check that port 19333 is open between all machines. On cloud providers, check security group rules.

**Miner not finding blocks:**
At genesis difficulty (1), blocks should be found in under a second. If the miner is stuck, check it can reach the seed nodes via the P2P port.

**RPC not responding:**
The RPC server binds to 127.0.0.1 by default. Use `--rpc-bind 0.0.0.0` to expose it (only in testnet). Check the `--rpc-port` flag.

**Chain stuck / not progressing:**
Check miner logs for solver timeouts. The testnet uses easy difficulty, so if problems are too hard, something is wrong with the difficulty adjustment. Reset with `./launch_testnet.sh reset`.

**Out of disk space:**
Testnet chain grows at roughly 1 GB per week with 10-second blocks. Prune old data or increase disk.

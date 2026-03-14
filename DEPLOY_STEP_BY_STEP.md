# NPChain Testnet — Step-by-Step Deployment Walkthrough

Everything below is in order. Do not skip steps.

---

## What You Will Have When Done

- A live testnet producing blocks every 15 seconds
- 3 seed nodes for peer discovery
- A dedicated miner solving NP-complete problems
- A public RPC API for wallet connections
- A web faucet dispensing free test Certs
- Grafana dashboard monitoring the whole network
- A public URL people can connect to and start testing

---

## STEP 1: Get a Server

You need one server to start. You can add more later.

**Minimum specs:**
- 4 CPU cores
- 8 GB RAM
- 80 GB SSD
- Ubuntu 22.04 or 24.04
- A public IPv4 address

**Where to get one (cheapest to most expensive):**

| Provider | Plan | Cost/month | How to order |
|----------|------|-----------|--------------|
| Hetzner | CPX31 | ~$15 | hetzner.com/cloud |
| Vultr | 4 vCPU / 8 GB | ~$48 | vultr.com |
| DigitalOcean | s-4vcpu-8gb | ~$48 | digitalocean.com |
| AWS | t3.xlarge | ~$55 | aws.amazon.com/ec2 |

Pick one. Create the server. Choose Ubuntu 24.04 as the operating system. 
Add your SSH key during creation.

Once your server is created, you will get an IP address. Write it down.
For the rest of this guide, replace `YOUR_SERVER_IP` with that address.

---

## STEP 2: Connect to Your Server

Open a terminal on your local computer. 

```bash
ssh root@YOUR_SERVER_IP
```

If this is your first time connecting, type `yes` when asked about the fingerprint.

You should now see a command prompt on your server. All remaining steps
happen on this server, not your local machine.

---

## STEP 3: Install Docker

Copy and paste this entire block:

```bash
# Update system packages
apt update && apt upgrade -y

# Install Docker
curl -fsSL https://get.docker.com | sh

# Start Docker and enable it on boot
systemctl enable docker
systemctl start docker

# Install Docker Compose plugin
apt install -y docker-compose-plugin

# Verify installation
docker --version
docker compose version
```

You should see Docker version 24+ and Compose version 2+.
If either command fails, do not continue — fix Docker first.

---

## STEP 4: Open Firewall Ports

```bash
# Install firewall if not present
apt install -y ufw

# Allow SSH (so you don't lock yourself out)
ufw allow 22/tcp

# Allow NPChain testnet ports
ufw allow 19333/tcp    # P2P — nodes connect here
ufw allow 19334/tcp    # Seed node 1
ufw allow 19335/tcp    # Seed node 2
ufw allow 19336/tcp    # Seed node 3
ufw allow 18333/tcp    # RPC API — wallets connect here
ufw allow 8080/tcp     # Faucet web page
ufw allow 3000/tcp     # Grafana monitoring dashboard
ufw allow 9090/tcp     # Prometheus metrics

# Enable firewall
ufw --force enable

# Verify
ufw status
```

You should see all ports listed as ALLOW.

---

## STEP 5: Upload the NPChain Code

On your LOCAL computer (not the server), open a new terminal and run:

```bash
# Navigate to wherever your npchain folder is
cd /path/to/npchain

# Create a tarball of the project
tar czf npchain.tar.gz \
    --exclude='.git' \
    --exclude='build' \
    --exclude='*.o' \
    .

# Upload to server
scp npchain.tar.gz root@YOUR_SERVER_IP:/root/
```

Now go back to your SERVER terminal:

```bash
# Create project directory
mkdir -p /opt/npchain
cd /opt/npchain

# Extract
tar xzf /root/npchain.tar.gz

# Verify files are there
ls -la
```

You should see the `CMakeLists.txt`, `src/`, `include/`, `deploy/` folders.

---

## STEP 6: Build and Launch the Testnet

This is the big step. One command builds everything and starts the network.

```bash
cd /opt/npchain/deploy/testnet

# Make the deploy script executable
chmod +x deploy_testnet.sh
chmod +x entrypoint.sh

# Launch
docker compose -f docker-compose.testnet.yml up -d --build
```

**This will take 5-10 minutes the first time** because Docker needs to:
1. Download the Ubuntu base image
2. Install the C++ compiler toolchain
3. Compile the entire NPChain codebase from source
4. Create containers for all 10 services

Watch the progress. When you see `Creating npchain_boot ... done` and 
similar for all containers, it is working.

---

## STEP 7: Verify Everything Started

```bash
# Check all containers are running
docker compose -f docker-compose.testnet.yml ps
```

You should see something like:

```
NAME                          STATUS
npchain_genesis_init          Exited (0)     <-- This is correct, it runs once
npchain_boot                  Up (healthy)
npchain_seed1                 Up
npchain_seed2                 Up
npchain_seed3                 Up
npchain_miner                 Up
npchain_rpc                   Up
npchain_faucet                Up
npchain_testnet_prometheus    Up
npchain_testnet_grafana       Up
```

The genesis_init container should show `Exited (0)` — that means it
created the genesis block and finished. Every other container should say `Up`.

**If any container says `Exited (1)` or `Restarting`**, check its logs:

```bash
docker logs npchain_boot        # Check boot node
docker logs npchain_miner       # Check miner
docker logs npchain_faucet      # Check faucet
```

---

## STEP 8: Watch Blocks Being Produced

```bash
# Follow the boot node logs — you should see new blocks every ~15 seconds
docker logs -f npchain_boot
```

You should see output like:

```
[MINE] New template: height=1 difficulty=1 problem=k-SAT txs=0
  ✓ BLOCK MINED #1
    Hash:       a3f8c2d1...
    Solver:     CDCL-SAT
    Reward:     24.97 Certs (annual cap: 52.5M)
    Inflation:  100.000%

[MINE] New template: height=2 difficulty=1 problem=Subset-Sum txs=0
  ✓ BLOCK MINED #2
    ...
```

Press `Ctrl+C` to stop watching logs.

If you see blocks appearing, **your testnet is live**.

---

## STEP 9: Test the Faucet

Open your web browser and go to:

```
http://YOUR_SERVER_IP:8080
```

You should see the NPChain Testnet Faucet page with a green terminal-style
interface. This is where people will get free test Certs.

To test it:
1. First create a wallet address (next step)
2. Paste the address into the faucet
3. Click "Request 10,000 Certs"

---

## STEP 10: Create a Test Wallet

```bash
# Run the wallet tool inside the RPC container
docker exec npchain_rpc npchain_wallet keygen
```

This generates a Dilithium-5 post-quantum keypair and outputs your address:

```
  Address: cert1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh
```

Copy this address. Use it in the faucet to receive 10,000 test Certs.

---

## STEP 11: Check the RPC API

```bash
# From your server
curl http://localhost:18333/api/v1/status
```

Or from anywhere on the internet:
```bash
curl http://YOUR_SERVER_IP:18333/api/v1/status
```

This should return JSON with the current block height, peer count, 
chain ID, and other status information.

---

## STEP 12: Open Grafana Dashboard

Open in your browser:

```
http://YOUR_SERVER_IP:3000
```

Login:
- Username: `admin`
- Password: `npchain-testnet`

You will see metrics for block height, peer connections, mining solve times,
memory usage, and CPU usage across all nodes.

---

## STEP 13: Share With Testers

Give your testers these details:

```
╔═══════════════════════════════════════════════════════════╗
║  NPChain Testnet — Connection Info                       ║
╠═══════════════════════════════════════════════════════════╣
║                                                           ║
║  Seed Nodes (for connecting your own node):               ║
║    YOUR_SERVER_IP:19333                                   ║
║    YOUR_SERVER_IP:19334                                   ║
║    YOUR_SERVER_IP:19335                                   ║
║    YOUR_SERVER_IP:19336                                   ║
║                                                           ║
║  RPC API (for wallets and dApps):                         ║
║    http://YOUR_SERVER_IP:18333                            ║
║                                                           ║
║  Faucet (get free test Certs):                            ║
║    http://YOUR_SERVER_IP:8080                             ║
║                                                           ║
║  Grafana (network monitoring):                            ║
║    http://YOUR_SERVER_IP:3000                             ║
║    Login: admin / npchain-testnet                         ║
║                                                           ║
║  To connect your own node:                                ║
║    npchain_node --testnet --seed YOUR_SERVER_IP:19333     ║
║                                                           ║
║  Chain: NPChain Testnet (chain_id: TCR)                  ║
║  Consensus: Proof-of-NP-Witness                           ║
║  Crypto: CRYSTALS-Dilithium + Kyber (Post-Quantum)       ║
║  Block Time: 15 seconds                                   ║
║  Coin: Certs (unlimited supply, 52.5M/year cap)          ║
╚═══════════════════════════════════════════════════════════╝
```

---

## STEP 14: How Testers Connect Their Own Node

Someone who wants to run their own testnet node does this on their machine:

```bash
# Option A: Run via Docker (easiest)
docker run -d \
    --name my_npchain_node \
    -p 19333:19333 \
    -e NPCHAIN_NETWORK=testnet \
    -e NPCHAIN_MINING=true \
    -e NPCHAIN_SEED_NODES=YOUR_SERVER_IP:19333 \
    npchain/testnet-node:latest

# Option B: Build from source
git clone YOUR_REPO_URL npchain
cd npchain
mkdir build && cd build
cmake .. -DNPCHAIN_TESTNET=ON
make -j$(nproc)

# Run the node
./npchain_node --testnet --seed YOUR_SERVER_IP:19333

# Or run as a miner
./npchain_miner --testnet --seed YOUR_SERVER_IP:19333
```

---

## Daily Operations

### Check if testnet is healthy
```bash
cd /opt/npchain/deploy/testnet
docker compose -f docker-compose.testnet.yml ps
docker logs --tail 20 npchain_boot
```

### View real-time logs
```bash
# All containers
docker compose -f docker-compose.testnet.yml logs -f --tail=50

# Just the miner
docker logs -f npchain_miner

# Just the boot node
docker logs -f npchain_boot
```

### Restart a crashed container
```bash
docker restart npchain_miner
docker restart npchain_faucet
```

### Stop the testnet (keeps data)
```bash
cd /opt/npchain/deploy/testnet
docker compose -f docker-compose.testnet.yml down
```

### Start the testnet again (resumes from where it left off)
```bash
cd /opt/npchain/deploy/testnet
docker compose -f docker-compose.testnet.yml up -d
```

### Wipe everything and start fresh
```bash
cd /opt/npchain/deploy/testnet
docker compose -f docker-compose.testnet.yml down -v
docker compose -f docker-compose.testnet.yml up -d --build
```

### Update the code and redeploy
```bash
# On your local machine: upload new code
cd /path/to/npchain
tar czf npchain.tar.gz --exclude='.git' --exclude='build' .
scp npchain.tar.gz root@YOUR_SERVER_IP:/root/

# On the server: extract and rebuild
cd /opt/npchain
tar xzf /root/npchain.tar.gz
cd deploy/testnet
docker compose -f docker-compose.testnet.yml up -d --build
```

### Add more mining power (scale miners)
```bash
cd /opt/npchain/deploy/testnet
docker compose -f docker-compose.testnet.yml up -d --scale miner=3
```

---

## Troubleshooting

### "Cannot connect to the Docker daemon"
```bash
systemctl start docker
```

### Build fails with "out of memory"
Your server needs more RAM. Either upgrade, or add swap:
```bash
fallocate -l 4G /swapfile
chmod 600 /swapfile
mkswap /swapfile
swapon /swapfile
echo '/swapfile none swap sw 0 0' >> /etc/fstab
```
Then rebuild.

### No blocks being produced
Check the miner logs:
```bash
docker logs npchain_miner
```
If it says "Waiting for genesis block", the genesis init may have failed:
```bash
docker logs npchain_genesis_init
```

### Faucet shows "Cannot reach node RPC"
The RPC node may still be syncing. Wait 30 seconds and retry.
```bash
docker logs npchain_rpc
```

### Containers keep restarting
Check what is failing:
```bash
docker compose -f docker-compose.testnet.yml ps
docker logs npchain_boot 2>&1 | tail -30
```

### Port already in use
```bash
# Find what is using the port
ss -tlnp | grep 19333

# Kill it or change the port in docker-compose.testnet.yml
```

### Server ran out of disk
```bash
# Check disk usage
df -h

# Clean up old Docker images
docker system prune -a -f
```

---

## Optional: Point a Domain Name

If you own a domain (e.g., testnet.yourproject.com):

1. Go to your DNS provider
2. Add an A record: `testnet.yourproject.com` → `YOUR_SERVER_IP`
3. Wait 5-10 minutes for DNS propagation
4. Now people can connect to: `testnet.yourproject.com:19333`

---

## Optional: Add HTTPS to the Faucet

```bash
# Install Caddy (auto-HTTPS reverse proxy)
apt install -y caddy

# Configure
cat > /etc/caddy/Caddyfile << 'EOF'
testnet.yourproject.com {
    # Faucet
    handle /faucet* {
        reverse_proxy localhost:8080
    }
    # RPC API
    handle /api/* {
        reverse_proxy localhost:18333
    }
    # Grafana
    handle /grafana* {
        reverse_proxy localhost:3000
    }
}
EOF

systemctl restart caddy
```

Now the faucet is at `https://testnet.yourproject.com/faucet`

---

## Summary: What You Just Deployed

```
YOUR_SERVER_IP
    │
    ├── :19333  Boot Node (mining + P2P seed)
    ├── :19334  Seed Node 1 (peer discovery)
    ├── :19335  Seed Node 2 (peer discovery)
    ├── :19336  Seed Node 3 (peer discovery)
    ├── :18333  RPC API (wallet connections)
    ├── :8080   Faucet (free test Certs)
    ├── :3000   Grafana (monitoring dashboard)
    ├── :9090   Prometheus (metrics collection)
    │
    └── Internal (not exposed):
        ├── Dedicated Miner (solving NP-complete problems)
        └── Genesis Init (created block #0, then exited)

Network: NPChain Testnet
Chain ID: TCR
Consensus: Proof-of-NP-Witness (PoNW)
Block Time: 15 seconds
Coin: Certs (unlimited supply, 52.5M/year annual cap)
Crypto: CRYSTALS-Dilithium + Kyber-1024 (post-quantum)
Privacy: Stealth addresses + confidential transactions
```

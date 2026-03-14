# NPChain

**Proof-of-NP-Witness Consensus Blockchain**

NPChain is the first blockchain where miners solve NP-complete problem instances — k-SAT, Subset-Sum, Graph Coloring, and Hamiltonian Path — instead of brute-force hash puzzles. Finding a witness is hard. Verifying one is O(n).

- **ASIC-resistant** — problem type rotates every block
- **Quantum-resilient** — only quadratic speedup via Grover's algorithm
- **Post-quantum cryptography** — Dilithium signatures, Kyber KEM, SHA3-256

---

## 🚀 Join the Testnet (5 Minutes)

The NPChain testnet is live. Mine blocks, earn testnet Certs, and help stress-test the network.

### Requirements

- **Windows PC**
- **MSYS2** — download from [msys2.org](https://www.msys2.org/) and install

### Step 1: Install Tools

Open **MSYS2 UCRT64** from your Start menu (not regular CMD/PowerShell), then run:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc git mingw-w64-ucrt-x86_64-python
```

Type `Y` when prompted.

### Step 2: Download NPChain

```bash
git clone https://github.com/RudeCane/NPChain.git
cd NPChain
```

### Step 3: Build

```bash
g++ -std=c++20 -O2 -pthread -I include -I src \
    src/testnet_node.cpp src/crypto/hash.cpp src/crypto/dilithium.cpp \
    -o npchain_testnet.exe -lws2_32
```

You'll see two warnings about `memcpy` — ignore them. As long as there are no errors, you're good.

### Step 4: Create Your Wallet

Start the wallet web server:

```bash
cd web
python3 -m http.server 8888
```

Open **http://localhost:8888** in your browser.

1. **Create a password** (8+ characters) — this protects your wallet
2. **Copy your `cert1...` address** — this is your mining address
3. **Copy the mining command** shown on screen

Leave this browser tab open — it will show your balance once mining starts.

### Step 5: Start Mining

Open a **second MSYS2 UCRT64 terminal** and paste your mining command:

```bash
cd NPChain
./npchain_testnet.exe --address cert1YOUR_ADDRESS_HERE
```

To join the public testnet with other miners, add the seed node:

```bash
./npchain_testnet.exe --address cert1YOUR_ADDRESS_HERE --seed 47.197.198.200:19333
```

### Step 6: Watch Your Certs Grow

Go back to **http://localhost:8888** in your browser. You should see:

- **Balance** increasing with each block you mine
- **Chain Status** — height, difficulty, total supply
- **Recent Blocks** — highlights blocks you mined
- **Node connected** status indicator (green dot)

Each block earns **~47,564 Certs**.

---

## How It Works

Every block's previous hash seeds a SHAKE-256 PRNG that generates an NP-complete problem instance:

| Block Hash mod 4 | Problem Type | What the Miner Solves |
|-------------------|-------------|----------------------|
| 0 | **k-SAT** | Boolean satisfiability |
| 1 | **Subset-Sum** | Find subset summing to target |
| 2 | **Graph Coloring** | Color vertices with k colors |
| 3 | **Hamiltonian Path** | Find path visiting all vertices |

The miner finds a valid witness (solution). Every other node verifies it in O(n). No ASICs can dominate because the problem changes every block.

## Cryptographic Stack

| Layer | Algorithm | Purpose |
|-------|-----------|---------|
| Signatures | CRYSTALS-Dilithium Level 5 | Post-quantum digital signatures |
| Key Exchange | CRYSTALS-Kyber-1024 | Post-quantum key encapsulation |
| Block Hashing | SHA3-256 | Block integrity |
| PRNG | SHAKE-256 | Deterministic instance generation |
| Fast Hashing | BLAKE3 | Data integrity checks |

## Testnet Parameters

| Parameter | Value |
|-----------|-------|
| Coin | **Certs** |
| Chain ID | TCR |
| Block time | 15 seconds |
| Block reward | ~47,564 Certs |
| Annual emission | 100 billion Certs |
| P2P port | 19333 |
| RPC port | 18333 |
| Difficulty adjustment | Every 36 blocks |

## CLI Reference

```
./npchain_testnet.exe [options]

Required:
  --address <cert1...>     Your wallet address (create in web wallet first)

Optional:
  --seed <host:port>       Connect to a seed node (join public testnet)
  --p2p-port <port>        P2P listen port (default: 19333)
  --rpc-port <port>        RPC/HTTP port (default: 18333)
  --block-time <seconds>   Target block time (default: 15)
  --blocks <N>             Stop after N blocks (0 = unlimited)
  --fast                   No delays between blocks (testing only)
  --help                   Show all options
```

**Examples:**

```bash
# Solo mining (your own chain)
./npchain_testnet.exe --address cert1abc123...

# Join the public testnet
./npchain_testnet.exe --address cert1abc123... --seed 47.197.198.200:19333

# Run your own seed node
./npchain_testnet.exe --address cert1abc123... --p2p-port 19333

# Connect a second local node
./npchain_testnet.exe --address cert1def456... --p2p-port 19334 --rpc-port 18334 --seed 127.0.0.1:19333
```

## RPC API

Your node runs an HTTP API on port 18333:

| Endpoint | Returns |
|----------|---------|
| `GET /api/v1/status` | Chain height, difficulty, supply, peers, version |
| `GET /api/v1/blocks` | Recent blocks with miner, reward, hash |
| `GET /api/v1/balance/<address>` | Balance for any cert1... address |
| `GET /api/v1/peers` | Connected peer list |
| `GET /api/v1/miner` | Current miner address, balance, blocks mined |

**Example:**
```bash
curl http://localhost:18333/api/v1/status
```

## Web Wallet & Explorer

The `web/` folder contains a browser-based wallet and block explorer.

**Wallet** — create address, view balance, copy mining commands
**Explorer** — browse blocks, see all miners, track chain growth

```bash
cd web
python3 -m http.server 8888
# Open http://localhost:8888
```

## P2P Networking

Nodes communicate over a binary TCP protocol:

| Message | Purpose |
|---------|---------|
| HELLO | Exchange chain height, hash, miner address |
| GET_BLOCKS | Request blocks from a specific height |
| BLOCKS | Send requested blocks |
| NEW_BLOCK | Broadcast a freshly mined block |
| PING/PONG | Keepalive |

When you connect with `--seed`, your node syncs the full chain from the seed, then both nodes mine competitively and share new blocks in real-time.

## Project Structure

```
NPChain/
├── src/
│   ├── testnet_node.cpp     # Testnet node (mining + RPC + P2P)
│   ├── mainnet_node.cpp     # Mainnet node (60s blocks, production params)
│   ├── p2p.hpp              # P2P networking module
│   ├── wallet.cpp           # CLI wallet (optional)
│   ├── chain_validator.cpp  # Consensus test suite (26/26 passing)
│   └── crypto/              # SHA3-256, Dilithium stubs
├── include/                 # Headers (consensus, crypto, mining, etc.)
├── web/
│   ├── index.html           # Browser wallet (create address, view balance)
│   └── explorer.html        # Block explorer
├── deploy/                  # Docker 8-proxy shield (production)
└── docs/                    # Architecture documentation
```

## Economics

- **100 billion Certs per year** — hard emission cap
- **No halving** — predictable, stable emission forever
- **EIP-1559 fee burn** — creates deflationary pressure as usage grows
- **6 decimal places** — 1,000,000 base units per Cert

## Troubleshooting

**"No wallet address provided"** — Create a wallet first at `http://localhost:8888`, then copy your address into the mining command.

**"Failed to connect to seed"** — Make sure the seed node is running. Try `curl http://SEED_IP:18333/api/v1/status` to verify.

**Wallet shows "Node offline"** — Make sure your node is running in another terminal. The wallet needs the node's RPC on port 18333.

**Wallet won't load in browser** — Don't open index.html directly. Serve it: `python3 -m http.server 8888`, then go to `http://localhost:8888`.

**Build warnings about memcpy** — These are harmless warnings from the Dilithium stub. Ignore them.

## Links

- **GitHub:** [github.com/RudeCane/NPChain](https://github.com/RudeCane/NPChain)
- **Documentation:** [NPChain GitBook](https://rudecane.gitbook.io/npchain)

## License

MIT

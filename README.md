# NPChain

**Proof-of-NP-Witness Consensus Blockchain**

NPChain is a novel blockchain where miners solve NP-complete problem instances instead of brute-force hash puzzles. Verification is O(n) — anyone can check a witness instantly, but finding one requires real computational work.

---

## 🚀 Join the Testnet

The NPChain testnet is live. Mine blocks, earn testnet Certs, and help stress-test the network.

### Requirements

- **Windows** with [MSYS2](https://www.msys2.org/) installed
- **g++ compiler** — install in MSYS2 UCRT64 terminal:
  ```bash
  pacman -S mingw-w64-ucrt-x86_64-gcc
  ```

### Step 1: Download

```bash
git clone https://github.com/RudeCane/NPChain.git
cd NPChain
```

### Step 2: Build

Open **MSYS2 UCRT64** terminal and run:

```bash
# Build the testnet node
g++ -std=c++20 -O2 -pthread -I include -I src \
    src/testnet_node.cpp src/crypto/hash.cpp src/crypto/dilithium.cpp \
    -o npchain_testnet.exe -lws2_32

# Build the wallet
g++ -std=c++20 -O2 -I include \
    src/wallet.cpp src/crypto/hash.cpp src/crypto/dilithium.cpp \
    -o npchain_wallet.exe
```

### Step 3: Create Your Wallet

```bash
./npchain_wallet.exe create
```

Save your wallet address (`cert1...`) — this is where your mining rewards go.

### Step 4: Start Mining

**Solo mining (your own chain):**
```bash
./npchain_testnet.exe --address cert1YOUR_ADDRESS
```

**Join the public testnet (connect to seed node):**
```bash
./npchain_testnet.exe --address cert1YOUR_ADDRESS --seed SEED_IP:19333
```

> The current seed node IP will be posted in [Discussions](https://github.com/RudeCane/NPChain/discussions) and updated regularly.

### Step 5: Verify It Works

You should see output like:
```
[GENESIS] Block #0 mined
[GENESIS] Hash: 9f50b9aa...
[GENESIS] Reward: 47564.6880 Certs

  ✓ Block #1 | 10 vars | 0ms | diff=1 | reward=47564.69 Certs
  ✓ Block #2 | 10 vars | 0ms | diff=1 | reward=47564.69 Certs
```

Check your balance at: `http://localhost:18333/api/v1/balance/cert1YOUR_ADDRESS`

---

## How It Works

Each block's previous hash seeds a SHAKE-256 PRNG that deterministically generates an NP-complete problem instance. The problem type rotates every block:

| `H mod 4` | Problem Type | What the Miner Solves |
|------------|-------------|----------------------|
| 0 | k-SAT | Boolean satisfiability |
| 1 | Subset-Sum | Find subset summing to target |
| 2 | Graph Coloring | Color vertices with k colors |
| 3 | Hamiltonian Path | Find path visiting all vertices |

**Why this matters:**
- **ASIC-resistant** — the problem mutates every block, no fixed circuit can dominate
- **Quantum-resilient** — only quadratic speedup via Grover's algorithm
- **Useful work** — solving NP-complete instances has real-world applications

## Cryptographic Stack (Post-Quantum)

- **Signatures:** CRYSTALS-Dilithium Level 5
- **KEM:** CRYSTALS-Kyber-1024
- **Hashing:** SHA3-256 (blocks), SHAKE-256 (PRNG), BLAKE3 (integrity)

## CLI Reference

### Testnet Node

```
npchain_testnet.exe [options]

Options:
  --address <cert1...>     Miner reward address (required)
  --p2p-port <port>        P2P listen port (default: 19333)
  --rpc-port <port>        RPC/HTTP port (default: 18333)
  --seed <host:port>       Seed node to connect to (repeatable)
  --fast                   No inter-block delays
  --block-time <seconds>   Target block time (default: 15)
  --blocks <N>             Stop after N blocks

Examples:
  # Solo mining
  ./npchain_testnet.exe --address cert1abc123...

  # Connect to seed node
  ./npchain_testnet.exe --address cert1abc123... --seed 203.0.113.50:19333

  # Run two nodes locally
  ./npchain_testnet.exe --address cert1ALICE --p2p-port 19333
  ./npchain_testnet.exe --address cert1BOB --p2p-port 19334 --rpc-port 18334 --seed 127.0.0.1:19333
```

### Wallet

```
npchain_wallet.exe <command>

Commands:
  create       Generate a new wallet (Dilithium keypair)
  info         Show wallet address and public key
  balance      Check balance via RPC (node must be running)
  export-web   Export wallet for the browser wallet
```

## RPC Endpoints

The node runs an HTTP/JSON API on port 18333:

| Endpoint | Description |
|----------|-------------|
| `GET /api/v1/status` | Chain height, difficulty, supply, peers, version |
| `GET /api/v1/blocks` | Recent blocks with full details |
| `GET /api/v1/balance/<address>` | Balance for any address |
| `GET /api/v1/peers` | Connected peer list |

## Web Wallet & Block Explorer

A browser-based wallet and Etherscan-style block explorer are in the `web/` folder.

```bash
cd web
python -c "import http.server; s=http.server.HTTPServer(('0.0.0.0',8888),http.server.SimpleHTTPRequestHandler); print('http://localhost:8888'); s.serve_forever()"
```

- **http://localhost:8888** — Wallet (create address, check balance, import CLI wallet)
- **http://localhost:8888/explorer.html** — Block explorer (live chain view)

## P2P Networking

Nodes communicate over a binary TCP protocol:

| Message | ID | Purpose |
|---------|----|---------|
| HELLO | 0x01 | Handshake — exchange height, hash, port |
| GET_BLOCKS | 0x02 | Request blocks from height N |
| BLOCKS | 0x03 | Response with block array |
| NEW_BLOCK | 0x04 | Broadcast freshly mined block |
| PING/PONG | 0x05/0x06 | Keepalive |

When a new node connects with `--seed`, it syncs the full chain from the seed, then both nodes mine competitively and broadcast new blocks to each other.

## Testnet Parameters

| Parameter | Value |
|-----------|-------|
| Chain ID | TCR (0x544352) |
| Block time | 15 seconds |
| Block reward | ~47,564 Certs |
| Annual emission | 100B Certs |
| P2P port | 19333 |
| RPC port | 18333 |
| Difficulty window | 36 blocks |

## Economics

- **Coin:** Certs
- **Annual emission cap:** 100,000,000,000 Certs/year (hard ceiling)
- **Base units:** 1,000,000 per Cert (6 decimal places)
- **No halving, no hard supply cap**
- **EIP-1559 fee burn** for deflationary pressure

## Project Structure

```
NPChain/
├── src/
│   ├── testnet_node.cpp     # Complete testnet node (mining + RPC + P2P)
│   ├── p2p.hpp              # P2P networking module
│   ├── wallet.cpp           # CLI wallet
│   ├── chain_validator.cpp  # Consensus test suite (26/26 passing)
│   └── crypto/              # SHA3-256, Dilithium stubs
├── include/                 # Headers (consensus, crypto, mining, etc.)
├── web/                     # Browser wallet & block explorer
├── deploy/                  # Docker shield (8-proxy production architecture)
└── docs/                    # Architecture documentation
```

## Running the Validator

Verify the consensus engine is working correctly:

```bash
g++ -std=c++20 -O2 -pthread -I include \
    src/chain_validator.cpp src/crypto/hash.cpp src/crypto/dilithium.cpp \
    -o npchain_validator.exe

./npchain_validator.exe
# Expected: 26/26 tests passing
```

## License

MIT

## Links

- **Documentation:** [NPChain GitBook](https://rudecane.gitbook.io/npchain)
- **GitHub:** [github.com/RudeCane/NPChain](https://github.com/RudeCane/NPChain)

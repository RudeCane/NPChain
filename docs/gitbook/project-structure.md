# Project Structure

```
NPChain/
├── src/
│   ├── testnet_node.cpp      # Testnet node (mining + RPC + P2P + governance)
│   ├── mainnet_node.cpp      # Mainnet node (production parameters)
│   ├── p2p.hpp               # P2P networking module
│   ├── wallet.cpp            # CLI wallet
│   ├── chain_validator.cpp   # Consensus test suite (26/26 passing)
│   └── crypto/               # SHA3-256, Dilithium stubs
├── include/
│   ├── core/                 # Block, transaction types
│   ├── consensus/            # Mining, difficulty, validation
│   ├── crypto/               # Hash, Dilithium, Kyber, crypto agility
│   ├── governance/           # NIP system, voting engine
│   ├── config/               # Testnet/mainnet parameters
│   ├── security/             # Threat model types
│   └── utils/                # Common types, result monad
├── web/
│   ├── index.html            # Browser wallet
│   ├── governance.html       # Governance voting UI
│   └── explorer.html         # Block explorer
├── deploy/
│   └── shield/               # Docker 8-proxy shield
├── docs/
│   ├── ARCHITECTURE.md       # Full technical spec
│   └── gitbook/              # GitBook documentation
└── README.md                 # Quick start guide
```

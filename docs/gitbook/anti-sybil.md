# Anti-Sybil Protection

## Three Layers

### 1. IP Connection Limiting
Maximum 2 P2P connections per IP address. Prevents one machine from flooding the network with fake peers.

### 2. Consecutive Block Cap
No miner can produce more than 5 blocks in a row. If you hit the limit, your node pauses mining until another miner's block arrives. This forces diversity.

### 3. Minimum Stake Requirement
Infrastructure is in place to require a minimum Cert balance before mining blocks are accepted. Set to 0 on testnet to allow new miners. Will be configured for mainnet.

## Why These Matter

Without anti-sybil protection, a single operator could:
- Run 100 miners from one IP and dominate block production
- Mine the entire chain solo and accumulate all rewards
- Manipulate governance votes with fake addresses

These protections ensure the network remains decentralized.

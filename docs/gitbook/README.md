# NPChain

## The First Proof-of-NP-Witness Blockchain

NPChain is a novel blockchain where miners solve NP-complete problem instances — k-SAT, Subset-Sum, Graph Coloring, and Hamiltonian Path — instead of brute-force hash puzzles.

Finding a witness is hard. Verifying one is O(n).

### Why NPChain?

**ASIC-resistant forever.** The problem type rotates every block. You can't build an ASIC for all four NP-complete problem types simultaneously.

**Quantum-resilient at every layer.** NP search gets at most Grover's quadratic speedup. Dilithium signatures and Kyber key exchange are immune to Shor's algorithm.

**Fair mining.** CPU-bound, not GPU-bound. Regular PCs can mine. No GPU farms, no ASICs, no mining pools dominating the network.

**Democratic governance.** 70% of all miners must participate in votes. 75% approval required. No silent takeovers.

### Quick Links

- [Join the Testnet](quick-start.md) — mine in 5 minutes
- [How Mining Works](mining.md) — understand PoNW consensus
- [Governance](governance.md) — how the network decides
- [Tokenomics](tokenomics.md) — 100B Certs/year, EIP-1559 fee burn
- [GitHub](https://github.com/RudeCane/NPChain) — full source code

### Current Status

NPChain testnet is live. Miners are earning testnet Certs that will convert to mainnet Certs at a 1000:1 ratio when mainnet launches.

| Network | Status | Block Time | Reward |
|---------|--------|-----------|--------|
| Testnet | **Live** | 15 seconds | ~47,564 Certs |
| Mainnet | Coming Soon | 60 seconds | ~190,258 Certs |

# How Mining Works

## Proof-of-NP-Witness (PoNW)

NPChain miners don't grind SHA-256 hashes. Instead, each block requires solving an NP-complete problem instance that is deterministically generated from the previous block's hash.

## Mining Flow

1. **Previous block hash** seeds a SHAKE-256 PRNG
2. PRNG generates an NP-complete problem instance (SAT, Subset-Sum, Graph Coloring, or Hamiltonian Path)
3. Miner uses WalkSAT + CDCL to find a valid witness (solution)
4. Witness is included in the new block
5. Every node regenerates the same instance and verifies the witness in O(n)
6. Block is accepted, miner earns the block reward

## Difficulty Scaling

As difficulty increases:
- Problem instances grow larger (more variables, more clauses)
- Solving takes more time, memory, and CPU power
- Verification remains O(n) — always fast

Difficulty adjusts every 36 blocks on testnet (every ~9 minutes) to target 15-second block times.

## Why PoNW is Better

| Feature | Proof-of-Work (Bitcoin) | Proof-of-NP-Witness (NPChain) |
|---------|----------------------|------------------------------|
| Computation | Brute-force hash grinding | Intelligent search algorithms |
| ASIC resistance | None — ASICs dominate | Permanent — problem types rotate |
| GPU advantage | Massive | None — CPU-bound |
| Verification | O(1) hash check | O(n) witness check |
| Quantum impact | Grover's halves hash security | Grover's gives quadratic speedup only |
| Energy profile | Wasteful hash collisions | Useful computational work |

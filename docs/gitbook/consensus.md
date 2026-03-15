# Proof-of-NP-Witness Consensus

## How It Works

NPChain's consensus mechanism is fundamentally different from every existing blockchain. Instead of grinding random hashes (Proof-of-Work) or staking tokens (Proof-of-Stake), miners must solve **NP-complete problem instances**.

```
1. Previous block hash seeds a SHAKE-256 PRNG
2. PRNG deterministically generates an NP-complete problem instance
3. Problem type selected by hash mod 4:
   - 0: k-SAT (Boolean satisfiability)
   - 1: Subset-Sum
   - 2: Graph Coloring
   - 3: Hamiltonian Path
4. Miner searches for a satisfying WITNESS
5. Every validator regenerates the instance and verifies the witness in O(n)
6. First valid witness wins the block reward
```

## Why NP-Complete Problems?

The P vs NP asymmetry is the most powerful asymmetry in computer science:

- **Finding** a solution: exponential time (hard)
- **Verifying** a solution: polynomial time (fast)

This is exactly what a blockchain needs. Mining should be hard. Validation should be fast.

## Problem Type Rotation

Every block uses a different problem type based on the previous block's hash:

| Hash mod 4 | Problem | What the Miner Solves |
|------------|---------|----------------------|
| 0 | **k-SAT** | Find boolean variable assignments satisfying all clauses |
| 1 | **Subset-Sum** | Find a subset of integers that sum to a target value |
| 2 | **Graph Coloring** | Color graph vertices with k colors, no adjacent same color |
| 3 | **Hamiltonian Path** | Find a path visiting every vertex exactly once |

This rotation makes ASIC design impossible — you'd need four completely different specialized circuits, and the problem parameters change every block.

## Difficulty Adjustment

Difficulty scales the **problem size**, not a hash target:

- **k-SAT**: More variables and clauses
- **Subset-Sum**: Larger numbers and set size
- **Graph Coloring**: More vertices and edges
- **Hamiltonian Path**: More vertices and edge density

The difficulty adjusts every 36 blocks (testnet) or 144 blocks (mainnet) to maintain target block times.

## Quantum Resistance

| Attack Vector | Classical | Quantum (Grover) | Impact |
|--------------|-----------|-------------------|--------|
| Witness search (NP) | O(2^n) | O(2^(n/2)) | Quadratic speedup only |
| Hash collision (SHA-3) | O(2^256) | O(2^128) | Still secure |
| Signature forgery | Infeasible (lattice) | Infeasible (lattice) | Immune |

Bitcoin's PoW: Grover gives quadratic speedup, but **signatures break under Shor's algorithm**.

NPChain: Grover gives quadratic speedup on witness search AND lattice cryptography blocks Shor entirely.

## Verification

When a miner broadcasts a block with a witness:

1. Any node regenerates the exact same problem instance from the previous block hash
2. The witness (solution) is checked against the instance
3. Verification is O(n) — just plug in the values and check constraints
4. If valid, the block is accepted. If not, rejected.

No trust required. Pure mathematics.

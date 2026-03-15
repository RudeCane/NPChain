# Quantum Resistance

## NPChain's Quantum Security

NPChain is designed to be secure against quantum computers at every layer.

### Signatures: Dilithium-5
NIST-standardized lattice-based signatures. Immune to Shor's algorithm. A quantum computer cannot forge a Dilithium signature regardless of qubit count.

### Key Exchange: Kyber-1024
Lattice-based KEM for encrypted node communication. Quantum-safe key agreement.

### Hashing: SHA3-256
Grover's algorithm reduces hash security from 256-bit to 128-bit. 128-bit security is still computationally infeasible.

### Mining: NP-Complete Problems
Grover's provides at most a quadratic speedup for unstructured search. An NP-complete problem requiring 2^100 classical steps would require ~2^50 quantum steps — still exponential, still hard.

## Comparison to Bitcoin

| Layer | Bitcoin | NPChain |
|-------|---------|---------|
| Signatures | ECDSA (broken by Shor's) | Dilithium-5 (quantum-safe) |
| Hashing | SHA-256 (128-bit PQ) | SHA3-256 (128-bit PQ) |
| Mining | Hash grinding (Grover's halves) | NP solving (Grover's quadratic) |
| Key Exchange | ECDH (broken by Shor's) | Kyber-1024 (quantum-safe) |

## Crypto Agility

If quantum computers advance beyond current projections, NPChain's governance system allows the community to vote in new algorithms without a hard fork.

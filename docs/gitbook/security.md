# Threat Model

## Attack Vectors

### 51% Attack
**Risk: Low.** An attacker would need to solve NP-complete problems faster than the rest of the network combined. Unlike hash mining, you can't rent NP-solving ASICs because they don't exist.

### Sybil Attack
**Risk: Mitigated.** NPChain limits 2 P2P connections per IP, caps consecutive blocks per miner at 5, and can require minimum stake to mine.

### Quantum Attack
**Risk: Low.** Dilithium signatures are immune to Shor's algorithm. NP-problem solving gets at most a quadratic speedup from Grover's. SHA3-256 retains 128-bit post-quantum security.

### Chain Tampering
**Risk: Low.** Chain persistence files are fully re-validated on load. Every block's hash, prev_hash chain, height sequence, and witness are re-verified against the NP-instance.

### Governance Manipulation
**Risk: Low.** 70% of all miners must participate in votes. 75% approval required. No silent takeovers possible.

## Security Design Principles

1. Post-quantum cryptography at every layer
2. ASIC resistance through problem rotation
3. Democratic governance with high participation requirements
4. Chain integrity verification on every load
5. Admin-protected migration endpoints

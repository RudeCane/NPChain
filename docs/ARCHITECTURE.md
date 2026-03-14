# NPChain: P vs NP Mining Blockchain — Full Architecture

## Version 0.1.0 | Whitepaper-Grade Technical Specification

---

## 1. Executive Summary

NPChain is a novel blockchain architecture whose consensus mechanism is rooted in
the fundamental asymmetry of the **P vs NP** problem: finding solutions to NP-complete
problems is computationally hard, but *verifying* a given solution is fast (polynomial).

Instead of brute-force hash grinding (Bitcoin) or stake-based voting (Ethereum 2.0),
NPChain miners must discover **satisfying witnesses** for procedurally-generated
NP-complete problem instances. This creates a consensus layer that is:

- **ASIC-resistant** — problem structure mutates every block
- **Quantum-hardened** — NP search gets at most Grover's quadratic speedup
- **Verifiable in O(n)** — any node validates a block in linear time
- **Scientifically meaningful** — mining effort maps to real combinatorial research

---

## 2. Novel Consensus Algorithm: Proof-of-NP-Witness (PoNW)

### 2.1 Core Concept

No existing blockchain uses this consensus mechanism. Here is how it works:

```
┌─────────────────────────────────────────────────────────────────┐
│                    PROOF-OF-NP-WITNESS (PoNW)                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. Previous block hash H(b_{k-1}) seeds a CSPRNG              │
│  2. CSPRNG deterministically generates an NP-complete instance  │
│     (rotating between k-SAT, Subset-Sum, Graph Coloring,       │
│      Hamiltonian Path — selected by H mod 4)                    │
│  3. Miner searches for a satisfying WITNESS W                  │
│  4. Miner broadcasts (W, nonce, coinbase_tx, tx_merkle_root)   │
│  5. Every validator regenerates the instance from H(b_{k-1})   │
│     and checks W in O(n) time                                  │
│  6. First valid witness wins the block reward                  │
│                                                                 │
│  Difficulty adjustment: Scale the problem size (variables,      │
│  clauses, set cardinality) to target 60-second block times     │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Problem Instance Generation Pipeline

```
H(block_{k-1})
    │
    ▼
┌──────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  SHAKE-256   │────▶│  Problem Selector │────▶│ Instance Builder │
│  (seed PRNG) │     │  H mod 4 = type   │     │ (parameterized)  │
└──────────────┘     └──────────────────┘     └────────┬────────┘
                                                       │
                      ┌────────────────────────────────┼────────────────┐
                      │                  │              │                │
                      ▼                  ▼              ▼                ▼
                 ┌─────────┐     ┌────────────┐  ┌──────────┐   ┌────────────┐
                 │  k-SAT  │     │ Subset-Sum │  │ Graph    │   │ Hamiltonian│
                 │ Instance│     │  Instance   │  │ Coloring │   │   Path     │
                 └─────────┘     └────────────┘  └──────────┘   └────────────┘
```

### 2.3 Why This Is Quantum Resistant at the Consensus Layer

| Attack Vector           | Classical          | Quantum (Grover)    | Impact        |
|-------------------------|--------------------|---------------------|---------------|
| Witness search (NP)     | O(2^n)             | O(2^{n/2})          | Quadratic only|
| Hash collision (SHA-3)  | O(2^{256})         | O(2^{128})          | Still secure  |
| Signature forgery       | Infeasible (lattice)| Infeasible (lattice)| Immune       |
| Key recovery            | Infeasible (LWE)   | Infeasible (LWE)    | Immune       |

Bitcoin's PoW: Grover gives O(2^{128}) → still OK but **signatures break under Shor**.
NPChain: Grover gives quadratic on witness search AND lattice crypto blocks Shor entirely.

### 2.4 Difficulty Adjustment Algorithm

```
target_block_time  = 60 seconds
window_size        = 144 blocks (≈ 2.4 hours)
max_adjustment     = 1.25x per window

new_difficulty = old_difficulty × (target_time / actual_avg_time)
               = clamp(new_difficulty, old/1.25, old×1.25)

"Difficulty" maps to:
  k-SAT       → number of variables N, clause ratio α = clauses/N
  Subset-Sum  → bit-length of target, set cardinality
  Graph Color → vertex count, edge density, chromatic number k
  Hamiltonian → vertex count, edge density
```

---

## 3. Full System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           NPChain Full Stack                           │
├─────────────┬──────────────┬──────────────┬─────────────┬──────────────┤
│   Network   │  Consensus   │    Crypto    │   Privacy   │   Security   │
│   Layer     │   Engine     │   Module     │   Shield    │   Fortress   │
├─────────────┼──────────────┼──────────────┼─────────────┼──────────────┤
│ P2P Gossip  │ PoNW Engine  │ Dilithium    │ Stealth     │ Threshold    │
│ DHT Routing │ Instance Gen │ Kyber KEM    │ Addresses   │ Signatures   │
│ Encrypted   │ Witness      │ SHAKE-256    │ Confidential│ VDF Time-    │
│ Transport   │ Validator    │ Hash-Based   │ Transactions│ Locks        │
│ NAT Punch   │ Difficulty   │ Commitments  │ zk-Lattice  │ Memory-Hard  │
│ Peer Scoring│ Adjustment   │ Merkle Trees │ Proofs      │ Anti-Flood   │
└─────────────┴──────────────┴──────────────┴─────────────┴──────────────┘
                                    │
                        ┌───────────┴───────────┐
                        │     Mining Engine      │
                        ├────────────────────────┤
                        │ Multi-Strategy Solver  │
                        │ ├─ DPLL/CDCL (SAT)     │
                        │ ├─ Meet-in-Middle (SS)  │
                        │ ├─ Backtracking (GC)    │
                        │ ├─ DFS+Pruning (Ham)    │
                        │ └─ Parallel Work Pool   │
                        │                        │
                        │ GPU Acceleration Layer  │
                        │ Thread Pool Manager     │
                        │ Solution Cache          │
                        └────────────────────────┘
```

---

## 4. Quantum-Resistant Cryptographic Stack

### 4.1 Digital Signatures: CRYSTALS-Dilithium (NIST PQC Standard)

- **Security basis**: Module-LWE (Learning With Errors) over lattices
- **Key sizes**: Public 1952 bytes, Private 4000 bytes (Level 5)
- **Signature size**: 4595 bytes
- **Shor-immune**: No known quantum algorithm breaks LWE efficiently

### 4.2 Key Encapsulation: CRYSTALS-Kyber

- **Used for**: Encrypted P2P transport, stealth address derivation
- **Security basis**: Module-LWE
- **Ciphertext size**: 1568 bytes (Kyber-1024)

### 4.3 Hash Functions

- **SHAKE-256**: Instance generation PRNG, address derivation
- **SHA3-256**: Block hashing, Merkle roots, transaction IDs
- **BLAKE3**: Fast integrity checks, peer message authentication

### 4.4 Commitment Scheme

```
Pedersen-style over lattice:
  Commit(v, r) = v·G + r·H   where G,H are lattice-based generators
  - Perfectly hiding, computationally binding
  - Used in confidential transaction amounts
```

---

## 5. Privacy Architecture

### 5.1 Stealth Addresses (Post-Quantum)

```
Alice wants to pay Bob without revealing Bob's address on-chain:

1. Bob publishes scan key (S) and spend key (B) [Dilithium public keys]
2. Alice generates ephemeral Kyber keypair (e, E)
3. Alice computes shared_secret = KyberDecaps(e, B)
4. One-time address P = DeriveKey(shared_secret ⊕ S)
5. Alice sends funds to P, publishes E on-chain
6. Bob scans: tries KyberDecaps(b, E) → recovers shared_secret → derives P
7. Only Bob can recognize and spend from P
```

### 5.2 Confidential Transactions

```
Instead of broadcasting amount in cleartext:
  - Sender creates Pedersen commitment: C = amount·G + blinding·H
  - Range proof (lattice-based Bulletproof analog) proves 0 ≤ amount < 2^64
  - Validators check: sum(inputs) - sum(outputs) - fee = 0 (homomorphic)
  - Nobody learns individual amounts
```

### 5.3 zk-Lattice Proofs (Transaction Graph Privacy)

```
Prover demonstrates knowledge of:
  - Valid UTXO membership (Merkle proof)
  - Correct key ownership (Dilithium signature knowledge)
  - Amount conservation (commitment arithmetic)
WITHOUT revealing which UTXO is being spent.

Proof system: Lattice-based Σ-protocol compiled to NIZK via Fiat-Shamir + SHAKE-256
```

---

## 6. Security Fortress (Hack Protection)

### 6.1 Threat Model & Countermeasures

```
┌──────────────────────┬────────────────────────────────────────────┐
│ Attack               │ Defense                                    │
├──────────────────────┼────────────────────────────────────────────┤
│ 51% attack           │ VDF time-locks + checkpoint anchoring      │
│ Sybil attack         │ Peer reputation scoring + PoNW cost        │
│ Eclipse attack       │ Mandatory diverse peer buckets (AS-aware)  │
│ Selfish mining       │ Uncle reward elimination + VDF ordering    │
│ Long-range attack    │ Finality checkpoints every 1000 blocks    │
│ Quantum key theft    │ Lattice-based crypto (Dilithium + Kyber)  │
│ Memory corruption    │ AddressSanitizer-style guards, safe alloc │
│ Network MITM         │ Kyber-encrypted transport + peer pinning  │
│ Block flood / DoS    │ Memory-hard auxiliary puzzle (Argon2id)   │
│ Replay attack        │ Chain-ID + epoch nonces in every tx       │
│ Signature malleation │ Canonical signature encoding              │
│ Side-channel leak    │ Constant-time lattice operations          │
└──────────────────────┴────────────────────────────────────────────┘
```

### 6.2 Verifiable Delay Function (VDF) Integration

```
After a valid witness is found:
  1. Miner computes VDF(witness, T) where T = sequential squarings
  2. VDF output is included in block header
  3. VDF is non-parallelizable → prevents instant block withholding
  4. Proof of VDF correctness is O(log T) to verify
  
This creates temporal ordering that pure PoNW alone cannot guarantee.
```

### 6.3 Threshold Block Signing

```
Block finalization requires t-of-n validator signatures:
  - Committee of n = 21 validators selected per epoch
  - Selection weighted by stake + recent PoNW contribution
  - Threshold Dilithium: t = 14 must co-sign for finality
  - Prevents single-point-of-failure key compromise
```

---

## 7. Transaction & Block Structure

### 7.1 Block Header (176 bytes + variable witness)

```
┌─────────────────────────────────────────┐
│ version            : uint32  (4 bytes)  │
│ prev_block_hash    : bytes32 (32 bytes) │
│ merkle_root        : bytes32 (32 bytes) │
│ state_root         : bytes32 (32 bytes) │
│ timestamp          : uint64  (8 bytes)  │
│ difficulty         : uint64  (8 bytes)  │
│ problem_type       : uint8   (1 byte)   │
│ problem_params     : bytes32 (32 bytes) │
│ witness_hash       : bytes32 (32 bytes) │
│ vdf_output         : bytes32 (32 bytes) │
│ vdf_proof          : variable           │
│ witness_data       : variable           │
│ miner_pubkey       : bytes   (1952 b)   │  ← Dilithium
│ miner_signature    : bytes   (4595 b)   │  ← Dilithium
│ threshold_sigs     : bytes   (variable)  │
└─────────────────────────────────────────┘
```

### 7.2 Transaction Format

```
┌─────────────────────────────────────────────┐
│ tx_version         : uint8                  │
│ chain_id           : uint32                 │
│ epoch_nonce        : uint64   (replay guard)│
│ inputs[]           :                        │
│   ├─ nullifier     : bytes32  (privacy)     │
│   ├─ commitment    : bytes32  (amount)      │
│   └─ zk_proof      : bytes    (ownership)   │
│ outputs[]          :                        │
│   ├─ stealth_addr  : bytes    (one-time)    │
│   ├─ commitment    : bytes32  (amount)      │
│   ├─ range_proof   : bytes    (validity)    │
│   └─ ephemeral_key : bytes    (Kyber)       │
│ fee_commitment     : bytes32                │
│ signature          : bytes    (Dilithium)   │
└─────────────────────────────────────────────┘
```

---

## 8. Network Architecture

```
┌──────────────────────────────────────────────────────┐
│                   Network Topology                   │
│                                                      │
│  ┌─────────┐    Kyber-Encrypted     ┌─────────┐    │
│  │  Miner  │◄═══════════════════════►│  Miner  │    │
│  │  Node   │     Gossip Protocol     │  Node   │    │
│  └────┬────┘                         └────┬────┘    │
│       │          DHT (Kademlia)           │         │
│       │    ┌──────────────────────┐       │         │
│       └────┤   Validator Nodes   ├───────┘         │
│            │  (Threshold Committee)│                 │
│            └──────────┬───────────┘                 │
│                       │                             │
│              ┌────────┴────────┐                    │
│              │   Full Nodes    │                    │
│              │ (Chain Storage) │                    │
│              └────────┬────────┘                    │
│                       │                             │
│              ┌────────┴────────┐                    │
│              │  Light Clients  │                    │
│              │ (SPV + Proofs)  │                    │
│              └─────────────────┘                    │
└──────────────────────────────────────────────────────┘

Peer Discovery: Kademlia DHT with AS-diversity enforcement
Transport:      Kyber-1024 key exchange → AES-256-GCM streams
Gossip:         Epidemic protocol with peer scoring
Anti-Eclipse:   Min 4 distinct /16 subnets required
```

---

## 9. Mining Strategy Algorithms

### 9.1 k-SAT Solver (CDCL-Based)

```
Conflict-Driven Clause Learning:
  1. Unit propagation
  2. Decision heuristic (VSIDS)  
  3. Conflict analysis → learn clause
  4. Non-chronological backtracking
  5. Restart with learned clauses
  
  Parallelization: Portfolio approach — multiple
  CDCL instances with different random seeds
```

### 9.2 Subset-Sum Solver

```
Meet-in-the-Middle:
  1. Split set S into S1, S2
  2. Enumerate all 2^{|S1|} subset sums of S1
  3. Sort and hash-table lookup
  4. For each subset sum of S2, check complement
  
  Complexity: O(2^{n/2}) — matches Grover's limit
```

### 9.3 Graph Coloring Solver

```
Backtracking with constraint propagation:
  1. Order vertices by degree (largest first)
  2. Try color assignments with forward checking
  3. MAC (Maintaining Arc Consistency) pruning
  4. Conflict-directed backjumping
```

### 9.4 Hamiltonian Path Solver

```
DFS with pruning:
  1. Start from random vertex
  2. Extend path greedily (Warnsdorff's heuristic)
  3. Backtrack with pruning via necessary edges
  4. Parallel: multiple random starting vertices
```

---

## 10. Economic Model — Unlimited Supply / Annual Cap

```
Emission Model:
  - Annual Cap:     52,500,000 Certs emitted per year (hard ceiling)
  - Block Reward:   ≈100 Certs per block (52.5M / 525,600 blocks)
  - Total Supply:   UNLIMITED — no hard cap
  - Inflation:      Declines asymptotically → 0% (but never reaches it)

Why no hard cap?
  - Miners ALWAYS have a baseline reward beyond fees
  - No "fee-only" security cliff (Bitcoin's long-term concern)
  - Predictable, constant emission = easy monetary policy
  - Real inflation rate becomes negligible over decades

Inflation Schedule:
  Year  1  →  ∞%      (new chain, supply growing from zero)
  Year  5  →  20.0%   (supply: 262.5M)
  Year 10  →  10.0%   (supply: 525M)
  Year 20  →   5.0%   (supply: 1.05B)
  Year 50  →   2.0%   (supply: 2.625B)
  Year 100 →   1.0%   (supply: 5.25B)
  Year 500 →   0.2%   (supply: 26.25B)
         ∞ →   0.0%   (asymptotic, never reached)

Complexity Frags:
  - 52,500,000 Certs ÷ 525,600 blocks = 99.885... Certs/block
  - Integer math: each block gets floor(cap / blocks_per_year)
  - The leftover units are called "Complexity Frags" — named after
    the NP-complexity problems that miners solve to earn them
  - COMPLEXITY_FRAGS blocks at the end of each year get +1 base unit
  - Guarantees EXACT annual cap is hit, never overshot or undershot

Fee Market:
  - Base fee (EIP-1559 style) burned   ← deflationary pressure
  - Priority fee to miner              ← incentive alignment
  - Confidential fee commitments       ← amounts stay hidden
  - Fee burns partially offset emission → effective inflation < nominal
```

---

## 11. C++ Project Map

```
npchain/
├── include/
│   ├── core/          block.hpp, transaction.hpp, chain.hpp, mempool.hpp
│   ├── consensus/     ponw_engine.hpp, difficulty.hpp, vdf.hpp
│   ├── crypto/        dilithium.hpp, kyber.hpp, hash.hpp, commitment.hpp,
│   │                  agility.hpp  ← NEW: crypto-agility framework
│   ├── governance/    governance.hpp ← NEW: on-chain upgrade governance
│   ├── mining/        solver.hpp, sat_solver.hpp, subset_solver.hpp,
│   │                  graph_color_solver.hpp, hamiltonian_solver.hpp
│   ├── network/       peer.hpp, gossip.hpp, transport.hpp
│   ├── privacy/       stealth.hpp, confidential_tx.hpp, zk_proof.hpp
│   ├── security/      threshold_sig.hpp, vdf.hpp, anti_flood.hpp
│   └── utils/         serialize.hpp, logger.hpp, config.hpp
├── src/               (implementations mirror include/)
├── tests/
├── docs/
└── CMakeLists.txt
```

---

## 12. Cryptographic Agility — Automatic Standards Tracking

### 12.1 The Problem

Every cryptographic algorithm has a shelf life:
- MD5: secure 1992, broken 2004 (12 years)
- SHA-1: secure 1995, broken 2017 (22 years)
- RSA-1024: secure 1977, deprecated ~2010 (33 years)

Bitcoin is permanently locked to secp256k1 and SHA-256. If either breaks,
the only option is a chain-killing hard fork. NPChain avoids this fate.

### 12.2 How It Works

```
Every cryptographic object on-chain carries a CryptoTag:

  ┌─────────────────────────────────────────────────┐
  │  CryptoTag (6 bytes, in every tx + block header)│
  ├─────────────────────────────────────────────────┤
  │  signature_algo  : uint16  (e.g., 0x0101 = Dilithium-5)
  │  kem_algo        : uint16  (e.g., 0x0201 = Kyber-1024)
  │  hash_algo       : uint16  (e.g., 0x0301 = SHA3-256)
  └─────────────────────────────────────────────────┘

When a validator verifies a transaction from ANY era:
  1. Read the CryptoTag
  2. Look up providers in the CryptoRegistry
  3. Verify using the CORRECT algorithm for that transaction

Block from 2025 → Dilithium-5 verification   ← still works
Block from 2035 → FALCON-1024 verification   ← also works
Block from 2045 → FutureSign verification    ← also works

No hard forks. No chain splits. All coexist.
```

### 12.3 Algorithm Lifecycle

```
  RESERVED → PROPOSED → APPROVED → ACTIVE → PREFERRED
                                      │
                                      ▼
                                  DEPRECATED → SUNSET → RETIRED
                                      │
                                  (still valid     (migration    (emergency
                                   for verify)      deadline)     only)

Typical timeline for a new algorithm:
  Day 0:    NIP submitted (NPChain Improvement Proposal)
  Day 1-90: Review period (minimum 2 independent security audits)
  Day 90-120: Validator vote (75% approval threshold)
  Day 120-180: Activation delay (nodes/wallets update)
  Day 180:  ACTIVE — new transactions can use it
  
Typical timeline for deprecation:
  Day 0:    NIP to deprecate (e.g., NIST downgrades confidence)
  Day 90:   Vote passes → algorithm marked DEPRECATED
  Day 90-455: Migration window (FREE migration transactions)
  Day 455:  SUNSET — wallets warned urgently
  Day 820:  RETIRED — only if algorithm is actually broken

Emergency timeline (active vulnerability found):
  Day 0:    Emergency NIP (skip review period)
  Day 0-7:  Emergency vote (90% threshold, 50% participation)
  Day 7-14: Emergency activation
  Day 14:   New algorithm ACTIVE, old one DEPRECATED immediately
```

### 12.4 Annual Crypto Health Check

```
Every BLOCKS_PER_YEAR blocks, the chain automatically:

  1. CryptoHealthMonitor scans all active algorithms
  2. Checks years in service vs estimated safe lifetime
  3. Cross-references NIST/IEEE current recommendations
  4. Generates a CryptoHealthReport
  5. Auto-creates deprecation NIPs for flagged algorithms
  6. Wallets display warnings to users holding affected funds

This ensures the chain PROACTIVELY migrates before problems hit,
rather than reacting after a break (which is too late).
```

### 12.5 Migration System

```
When an algorithm is deprecated, users must move funds to new keys.

The MigrationManager provides:
  - Fee waivers during the migration window (free transactions)
  - Special migration transaction type (signs with OLD, outputs to NEW)
  - Priority block inclusion for migration transactions
  - Wallet-level warnings with countdown to deadline
  - Governance can extend deadlines if adoption is slow

After the migration deadline:
  - UTXOs under the old algorithm are FROZEN (not destroyed)
  - Governance can vote to unfreeze or extend
  - Funds are never permanently lost due to algorithm change
```


---

## 13. 8-Proxy Shield — Defense-in-Depth Deployment

### 13.1 Architecture Overview

No external traffic ever touches the core node directly.
8 proxy servers form concentric defense rings.

```
INTERNET
  │
  ▼
┌────────────────────────────────────────────────────────────────┐
│ P1: EDGE SENTINEL      DDoS absorption, geo-filter, SYN flood │
│     (PUBLIC IP)         connection limits, bandwidth caps       │
└───────────────────────────────┬────────────────────────────────┘
                                │ Kyber-encrypted tunnel
┌───────────────────────────────▼────────────────────────────────┐
│ P2: PROTOCOL GATE       Message format validation, size check  │
│ P3: RATE GOVERNOR       Token-bucket per-IP/global rate limits │
│ P4: IDENTITY VERIFIER   Dilithium auth, ban list, AS-diversity │
└───────────────────────────────┬────────────────────────────────┘
                                │
┌───────────────────────────────▼────────────────────────────────┐
│ P5: CONTENT INSPECTOR   Deep packet inspection, tx/block check │
│ P6: ANOMALY DETECTOR    Behavioral analysis, eclipse/sybil     │
└───────────────────────────────┬────────────────────────────────┘
                                │
┌───────────────────────────────▼────────────────────────────────┐
│ P7: ENCRYPTION BRIDGE   Re-encrypt with internal Kyber keys    │
│                         Strip ALL external metadata             │
│ P8: FINAL GATEWAY       Verify attestation chain (all 7 sigs)  │
│                         Circuit breaker, emergency kill switch  │
└───────────────────────────────┬────────────────────────────────┘
                                │ Internal-only channel
                                ▼
                     ┌──────────────────────┐
                     │     CORE NODE        │
                     │  (No public IP)      │
                     │  (No internet route) │
                     └──────────────────────┘
```

### 13.2 Attestation Chain

Every message carries cryptographic proof that ALL 8 proxies
inspected and approved it. Each proxy signs a ProxyAttestation
with its Dilithium key. The Final Gateway (P8) checks:

  1. All 7 prior attestations are present
  2. All signatures verify against known proxy public keys
  3. Timestamps are strictly sequential (P1 < P2 < ... < P8)
  4. Total pipeline latency < 10 seconds
  5. Source is the Encryption Bridge (P7), not anyone else

If ANY check fails, the message is dropped and an alert fires.

### 13.3 Network Isolation (Docker)

6 isolated Docker networks enforce topology:
  - public_net:   Only P1 (Edge Sentinel) is connected
  - edge_net:     P1 → P2 link
  - filter_net:   P2, P3, P4 (protocol/rate/identity)
  - inspect_net:  P4, P5, P6 (content/anomaly)
  - internal_net: P6, P7, P8 + monitoring
  - core_net:     P8 → Core Node ONLY

Each container runs as non-root, read-only filesystem,
all capabilities dropped except NET_BIND_SERVICE.

### 13.4 Deployment

```bash
cd deploy/
chmod +x deploy_shield.sh
./deploy_shield.sh --prod    # Production (with iptables)
./deploy_shield.sh --dev     # Development (no firewall)
```

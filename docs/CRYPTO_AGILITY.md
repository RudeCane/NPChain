# NPChain Cryptographic Agility Strategy

## How NPChain Stays Ahead of Cryptographic Threats

### The Problem

Every cryptographic algorithm has a shelf life. MD5 was secure in 1992, broken by 2004. SHA-1 lasted until 2017. RSA will die when quantum computers scale. Any blockchain that hardcodes its crypto is building on a foundation with an expiration date.

### NPChain's Solution: Three-Layer Defense

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 1: CRYPTO REGISTRY — Pluggable Algorithm Framework   │
│  Every signature, hash, and key is tagged with an algorithm │
│  ID. The chain verifies old and new crypto forever.         │
├─────────────────────────────────────────────────────────────┤
│  Layer 2: GOVERNANCE — Democratic Upgrade Process           │
│  70% participation, 75% approval to change crypto.          │
│  90-day review period. Independent audits required.         │
├─────────────────────────────────────────────────────────────┤
│  Layer 3: MONITORING — Continuous Health Tracking           │
│  Annual crypto review. Track NIST advisories.               │
│  Auto-proposals when algorithms are flagged.                │
└─────────────────────────────────────────────────────────────┘
```

---

## Layer 1: Crypto Registry

Every cryptographic operation in NPChain goes through a central registry that maps algorithm IDs to implementations.

### Algorithm Lifecycle

```
RESERVED → PROPOSED → APPROVED → ACTIVE → PREFERRED
                                    ↓
                              DEPRECATED → SUNSET → RETIRED
```

| Status | Meaning |
|--------|---------|
| **RESERVED** | ID allocated, implementation not yet available |
| **PROPOSED** | Implementation submitted, under community review |
| **APPROVED** | Governance passed, waiting for activation height |
| **ACTIVE** | Accepted for new transactions |
| **PREFERRED** | Recommended default for new wallets |
| **DEPRECATED** | Still valid for verification, not for new transactions |
| **SUNSET** | Migration deadline approaching — move your funds |
| **RETIRED** | No longer accepted (emergency only) |

### On-Chain Algorithm Tags

Every transaction and block header includes a `CryptoTag` identifying which algorithms were used:

```
Block header:
  signature_algorithm: 0x0101  (Dilithium-5)
  hash_algorithm:      0x0301  (SHA3-256)
  
Transaction:
  signature_algorithm: 0x0101  (Dilithium-5)
  
Future transaction (Year 5):
  signature_algorithm: 0x0102  (FALCON-1024)
```

The validator reads the tag and uses the correct provider. Blocks from 2025 and 2035 verify through the same pipeline.

### Provider Interface

New algorithms plug in through a standard interface:

```cpp
class ISignatureProvider {
    AlgorithmID algorithm_id();
    Result<Bytes> generate_keypair();
    Result<Bytes> sign(message, secret_key);
    bool verify(message, signature, public_key);
};
```

Adding a new algorithm = implement this interface + governance vote.

---

## Layer 2: Governance-Driven Upgrades

### How a New Algorithm Gets Adopted

```
Step 1: NIST/IEEE publishes new standard
        (or community proposes improvement)

Step 2: Developer implements CryptoProvider
        (the actual code for the new algorithm)

Step 3: Submit NIP (NPChain Improvement Proposal)
        Type: CRYPTO_UPGRADE
        Requires: ~47,564 Certs minimum balance

Step 4: Review Period (90 days on mainnet)
        - Community code review
        - At least 2 independent security audits
        - Test on testnet

Step 5: Governance Vote
        - 70% of all miners must participate
        - 75% must approve
        - If passed: activation scheduled

Step 6: Activation Delay (60 days on mainnet)
        - All nodes update their software
        - New algorithm becomes ACTIVE
        - Old algorithm remains ACTIVE (no breaking change)

Step 7: Migration Window (optional, for deprecation)
        - If old algorithm is being deprecated
        - 1 year minimum for users to move funds
        - Fee-waived migration transactions
```

### Emergency Crypto Response

If a vulnerability is found in an active algorithm:

```
Step 1: Emergency NIP submitted
        Type: CRYPTO_RETIREMENT
        
Step 2: Fast-track vote
        - 1 week voting period (not 30 days)
        - 90% approval required (not 75%)
        - 50% participation minimum
        
Step 3: Immediate activation
        - 1 week activation delay (not 60 days)
        - Affected algorithm → RETIRED
        - Funds using retired algorithm → frozen
        - Governance can vote to unfreeze with migration
```

---

## Layer 3: Continuous Monitoring

### Annual Crypto Health Review

Every ~525,600 blocks (~1 year), the chain automatically triggers a crypto health review:

1. Check all active algorithms against NIST advisories
2. Review security margin estimates
3. Auto-generate deprecation proposals for flagged algorithms
4. Community votes on each

### What We Monitor

| Source | What We Track |
|--------|--------------|
| NIST | Post-Quantum Cryptography advisories |
| IACR | Cryptanalysis papers on lattice problems |
| IEEE | New standard publications |
| ePrint | Pre-prints on LWE/MLWE attacks |
| ETSI | Quantum-safe migration guidelines |
| BSI | German federal crypto recommendations |

### Security Margins

Current algorithms and their safety margins:

```
CRYSTALS-Dilithium Level 5:
  Classical security: 256 bits
  Quantum security:   128 bits (post-Grover)
  Basis: Module-LWE
  Status: NIST FIPS 204 (finalized 2024)
  Risk: LOW — no known practical attacks

CRYSTALS-Kyber-1024:
  Classical security: 256 bits  
  Quantum security:   128 bits
  Basis: Module-LWE
  Status: NIST FIPS 203 (finalized 2024)
  Risk: LOW — same lattice basis as Dilithium

SHA3-256 (Keccak):
  Classical security: 256 bits
  Quantum security:   128 bits (Grover)
  Basis: Sponge construction
  Status: NIST FIPS 202 (finalized 2015)
  Risk: VERY LOW — 10+ years, no weaknesses found

SHAKE-256:
  Same basis as SHA3-256 (Keccak XOF)
  Risk: VERY LOW
```

### Reserved Algorithm Slots (Future-Ready)

```
Signatures:
  0x0102: FALCON-1024     (NIST alternate, lattice-based)
  0x0103: SPHINCS+-256f   (hash-based, ultra-conservative)
  0x0104: SQIsign         (isogeny-based, compact signatures)
  0x0105: MAYO            (oil-and-vinegar, small signatures)

Key Exchange:
  0x0202: HQC-256         (code-based, NIST alternate)
  0x0203: BIKE-L5         (code-based, NIST alternate)

Hashing:
  0x0302: BLAKE3          (already allocated, fast integrity)
```

---

## Timeline Projection

```
Year 0 (2026):  Genesis with Dilithium-5, Kyber-1024, SHA3-256
                All NIST finalized standards. Maximum safety.

Year 2-3:       FALCON-1024 standardized as NIST alternate
                → NIP to add FALCON-1024 as ACTIVE
                → Wallets can choose Dilithium or FALCON
                → Both remain PREFERRED

Year 5-7:       If lattice cryptanalysis advances:
                → NIP to add SPHINCS+ (hash-based, different basis)
                → Diversifies cryptographic assumptions
                → Three active signature algorithms

Year 7-10:      If quantum computers reach 1000+ logical qubits:
                → NIP to make SPHINCS+ PREFERRED
                → Lattice algorithms remain ACTIVE
                → Migration window for high-value wallets

Year 10-15:     Next generation PQC algorithms from NIST Round 2+
                → Smooth upgrade through same governance process
                → Old blocks with old crypto still verify forever

At NO POINT does the chain hard-fork. The CryptoTag in each
transaction tells validators which provider to use.
```

---

## The NPChain Advantage

Most blockchains will face a crisis when they need to upgrade cryptography. Bitcoin will need a hard fork. Ethereum will need a hard fork. Every chain that hardcoded its crypto will break.

NPChain is designed from day one to upgrade smoothly:

1. **No hard forks** — new algorithms are additive
2. **Democratic process** — 70% of miners must agree
3. **Long timelines** — 90-day review, audits, activation delay
4. **Backward compatibility** — old transactions verify forever
5. **Multiple active algorithms** — diversified security assumptions
6. **Emergency response** — fast-track for critical vulnerabilities

Your Certs are secured by math that can evolve. That's the promise.

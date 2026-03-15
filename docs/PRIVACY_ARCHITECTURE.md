# NPChain Privacy Architecture

## Post-Quantum Privacy Without ZK

NPChain uses three proven privacy technologies — no zero-knowledge proofs, no trusted setups, no experimental cryptography. Every component has years of production use on other chains, rebuilt with lattice math for quantum resistance.

```
┌─────────────────────────────────────────────────────────────┐
│               NPChain Privacy Stack                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────┐  Hide WHO is sending                  │
│  │ Ring Signatures   │  Lattice-based ring sigs              │
│  │ (Monero-proven)   │  Sender hidden among N decoys         │
│  └──────────────────┘  10+ years proven in production        │
│                                                             │
│  ┌──────────────────┐  Hide HOW MUCH is sent                │
│  │ Pedersen          │  Homomorphic commitments               │
│  │ Commitments       │  Range proofs prove validity           │
│  │ (CT-proven)       │  Amounts invisible on-chain            │
│  └──────────────────┘  7+ years proven (Monero, Liquid)      │
│                                                             │
│  ┌──────────────────┐  Hide WHERE you are                   │
│  │ Dandelion++       │  Two-phase transaction relay           │
│  │ (P2P-proven)      │  Origin IP untraceable                 │
│  └──────────────────┘  Proven in Grin, proposed for Bitcoin  │
│                                                             │
│  ┌──────────────────┐  Hide WHO is mining                   │
│  │ Stealth Mining    │  One-time addresses per block          │
│  │ Addresses         │  Miner identity unlinkable             │
│  └──────────────────┘  Derived from master key               │
│                                                             │
└─────────────────────────────────────────────────────────────┘

No ZK proofs. No trusted setup. No ceremonies.
Just proven math, rebuilt for post-quantum.
```

---

## 1. Ring Signatures — Hide the Sender

### What They Are

A ring signature lets you sign a message on behalf of a group without revealing which member actually signed. Monero has used this since 2014 to hide transaction senders.

### How It Works on NPChain

```
Alice wants to send Certs to Bob:

1. Alice selects N-1 decoy public keys from the blockchain
   (other addresses that have received Certs)

2. Alice forms a "ring" of N public keys:
   Ring = {Alice_pk, Decoy1_pk, Decoy2_pk, ..., DecoyN_pk}

3. Alice creates a ring signature using her secret key
   The signature proves: "ONE of these N keys signed this"
   But not WHICH one.

4. Validators verify the ring signature:
   - One of the N keys is the real signer ✓
   - The signature is mathematically valid ✓
   - No double-spending (key images prevent reuse) ✓
   - Which key? Unknown. ✗ (that's the point)
```

### Post-Quantum Ring Signatures

Traditional ring signatures use elliptic curves — vulnerable to Shor's algorithm. NPChain uses **lattice-based ring signatures**:

```
Construction: Lattice Ring Signature (based on Module-LWE)

Security basis: Module-LWE (same as Dilithium)
Ring size: 11 (1 real + 10 decoys) — configurable via governance
Key image: Prevents double-spend without revealing signer

Signature size: ~4-8 KB per ring (vs ~1.5 KB non-private)
Verification time: O(N) where N = ring size

Quantum resistance: Shor provides no advantage against LWE
Grover: Quadratic speedup on search, but ring size compensates
```

### Key Images (Double-Spend Prevention)

```
Problem: If the sender is hidden, how do you prevent them
from spending the same coins twice?

Solution: Key images

1. For each UTXO, the owner computes:
   key_image = SHA3(secret_key || UTXO_id)

2. Key image is unique to the UTXO and the owner
   - Same UTXO → same key image (always)
   - Different UTXO → different key image

3. When spending, the key image is published on-chain

4. Validators maintain a set of ALL used key images
   If key_image already exists → REJECT (double spend)
   If key_image is new → ACCEPT

5. Key image reveals NOTHING about which ring member spent
   Only that "someone in this ring spent this UTXO"
```

---

## 2. Pedersen Commitments — Hide the Amount

### What They Are

Pedersen commitments hide transaction amounts while allowing validators to verify that inputs = outputs. Used in production by Monero (since 2017), Blockstream Liquid, Grin, and Beam.

### How It Works

```
Instead of: "Alice sends 1000 Certs to Bob"

Alice creates:
  C_input  = 5000·G + r1·H    (Alice has 5000, blinding factor r1)
  C_bob    = 1000·G + r2·H    (Bob gets 1000, blinding factor r2)
  C_change = 3990·G + r3·H    (Alice change 3990, blinding factor r3)
  C_fee    = 10·G + 0·H       (Fee 10, no blinding)

Where G, H are generator points (lattice-based for post-quantum)

Validators check (homomorphic property):
  C_input = C_bob + C_change + C_fee
  (5000·G + r1·H) = (1000·G + r2·H) + (3990·G + r3·H) + (10·G)
  → 5000 = 1000 + 3990 + 10 ✓
  → r1 = r2 + r3 ✓ (blinding factors must balance too)

What validators see: Four opaque 32-byte commitments
What validators verify: They balance correctly
What validators learn about amounts: NOTHING
```

### Range Proofs

```
Problem: Pedersen commitments hide amounts, but what stops
someone from committing to a NEGATIVE amount?

  C_evil = (-1000000)·G + r·H
  
This would create money from nothing.

Solution: Range proofs

For each output commitment, the sender proves:
  "The committed amount is between 0 and 2^64"
  without revealing the actual amount.

Construction: Lattice-based Bulletproof analog
  - Logarithmic proof size: O(log n) for n-bit range
  - No trusted setup
  - Post-quantum (lattice math)
  - Proof size: ~1-2 KB per output
  - Verification: O(log n)
```

### Post-Quantum Pedersen Commitments

```
Traditional Pedersen: Uses elliptic curve points
  C = v·G + r·H on secp256k1
  → Broken by quantum computers (Shor)

NPChain Pedersen: Uses lattice-based generators
  C = v·G + r·H where G, H are Module-LWE vectors
  → Quantum-resistant (no known quantum attack on LWE)
  → Same homomorphic properties
  → Same verification logic
  → Just different math underneath
```

---

## 3. Dandelion++ — Hide the IP

### What It Is

Dandelion++ is a transaction relay protocol that prevents network observers from determining which node originated a transaction. Proven in Grin since 2019, proposed for Bitcoin.

### How It Works

```
Normal P2P gossip:
  Node A creates tx → broadcasts to ALL peers → they broadcast to ALL
  → Network observer with multiple nodes easily traces back to Node A

Dandelion++ two-phase relay:

STEM PHASE (anonymous):
  Node A creates tx
  → Sends to ONE random peer (not all)
  → That peer sends to ONE random peer
  → Continues for ~10 hops
  → Each hop has a small chance to "fluff"

FLUFF PHASE (broadcast):
  After stem phase, the last node broadcasts normally
  → All peers see the transaction
  → Origin appears to be the fluff node, NOT Node A
  → Even with many observer nodes, can't trace back through stem

┌─────┐    stem     ┌─────┐    stem     ┌─────┐    stem     ┌─────┐
│  A  │───────────→│  B  │───────────→│  C  │───────────→│  D  │
│(src)│            │     │            │     │            │     │
└─────┘            └─────┘            └─────┘            └──┬──┘
                                                           │ fluff!
                                                           ▼
                                                    ┌─────────────┐
                                                    │  BROADCAST   │
                                                    │  to everyone │
                                                    │  (looks like │
                                                    │   D sent it) │
                                                    └─────────────┘
```

### NPChain Implementation

```
Parameters (configurable via governance):
  Stem probability: 90% (continue stem) / 10% (fluff)
  Max stem length: 10 hops
  Stem timeout: 30 seconds (auto-fluff if stem stalls)
  Embargo period: 45 seconds before re-announce

Integration:
  - Built into P2P layer (p2p.hpp)
  - Transparent to miners — just affects how txs propagate
  - No additional cryptography needed
  - Zero performance overhead (same data, different routing)
```

---

## 4. Stealth Mining Addresses — Hide the Miner

### Problem

If you mine 1000 blocks with the same address, anyone can:
- Calculate your exact earnings
- Track when you're online/offline
- Identify your mining patterns
- Target you for attacks

### Solution

```
1. Wallet generates MASTER MINING KEYPAIR
   master_pk, master_sk = Dilithium.keygen()

2. For each block, derive ONE-TIME mining address:
   entropy = SHA3(prev_block_hash || block_height || random_nonce)
   child_sk = DeriveChild(master_sk, entropy)
   child_pk = DerivePublicKey(child_sk)
   mining_address = DeriveAddr(child_pk)

3. Block reward goes to the one-time address

4. On-chain, every block shows a DIFFERENT address
   Block #100: cert1aaa... (reward: 47564 Certs)
   Block #101: cert1bbb... (reward: 47564 Certs)  ← same miner!
   Block #102: cert1ccc... (reward: 47564 Certs)  ← same miner!
   → Nobody can tell these are the same person

5. Wallet scans all addresses using master_sk
   "I own cert1aaa, cert1bbb, cert1ccc..."
   Total balance: 142,694 Certs

6. To spend: sign with the child_sk for each address
   Can consolidate in a single private transaction
```

---

## 5. Combined Privacy Flow

### Full Private Transaction

```
Alice (hidden) sends hidden amount to Bob (stealth address):

1. STEALTH ADDRESS (hide receiver)
   Alice derives one-time address for Bob using Kyber KEM
   → On-chain: random address, only Bob can detect

2. RING SIGNATURE (hide sender)
   Alice signs with ring of 11 keys (hers + 10 decoys)
   → On-chain: "one of these 11 addresses sent this"

3. PEDERSEN COMMITMENT (hide amount)
   Alice commits to amount with blinding factor
   Range proof proves amount is valid
   → On-chain: opaque commitments, no amounts visible

4. KEY IMAGE (prevent double-spend)
   Alice publishes key image for her UTXO
   → On-chain: unique tag, prevents reuse

5. DANDELION++ (hide IP)
   Transaction relayed through stem phase
   → Network: can't trace which node originated tx

Result:
  ✓ Sender: hidden in ring of 11
  ✓ Receiver: one-time stealth address
  ✓ Amount: Pedersen commitment
  ✓ Double-spend: key image prevents
  ✓ IP address: Dandelion++ hides origin
  ✓ Quantum-safe: all lattice-based
  ✓ No ZK: no trusted setup, no ceremonies
```

---

## 6. L2 Privacy

### Private L2 with Ring Signatures + Commitments

```
L2 transactions use the same privacy stack:

1. L2 user submits private transaction:
   - Ring signature (ring of L2 addresses)
   - Pedersen commitment to amount
   - Key image for double-spend prevention

2. L2 Sequencer batches private transactions:
   - Validates ring signatures
   - Verifies commitment arithmetic (inputs = outputs)
   - Checks key images against spent set

3. Batch compiled to SAT constraints for L1:
   - Commitment balance constraints (homomorphic)
   - Key image uniqueness constraints
   - Ring signature validity (verification circuit → CNF)

4. L1 miner solves combined instance:
   - Witness proves all L2 private txs are valid
   - No actual amounts or senders revealed
   - O(n) verification on L1

L2 privacy is inherited from L1 — same math, same guarantees,
just batched and proven through the NP-witness system.
```

---

## 7. Privacy Levels

Users choose their privacy level per transaction:

| Level | Sender | Amount | Receiver | IP | Extra Size | Extra Fee |
|-------|--------|--------|----------|-----|------------|-----------|
| **Transparent** | Visible | Visible | Visible | Dandelion++ | 0 | Base fee |
| **Basic** | Visible | Visible | Stealth | Dandelion++ | +200 bytes | +10% |
| **Standard** | Ring (11) | Hidden | Stealth | Dandelion++ | +6 KB | +50% |
| **Maximum** | Ring (21) | Hidden | Stealth | Dandelion++ | +12 KB | +100% |

Default: **Standard** — ring signature + Pedersen commitment + stealth address.

Transparent mode exists for exchanges, auditors, and regulatory compliance. Users can prove their transactions to specific parties by revealing blinding factors.

---

## 8. Why Not ZK?

| Concern | ZK-Proofs | NPChain's Approach |
|---------|-----------|-------------------|
| Trusted setup | Often required (SNARKs) | Not needed |
| Complexity | Very high (circuit design) | Moderate (ring sigs well-understood) |
| Proof generation | Expensive (seconds to minutes) | Fast (signing is instant) |
| Audit difficulty | Hard (novel math) | Easier (decades of research) |
| Production track record | Limited (2-3 years) | Extensive (10+ years Monero) |
| Post-quantum | Mostly unsolved | Lattice ring sigs are quantum-safe |
| Bug surface | Large (complex proofs) | Smaller (simpler primitives) |

ZK is powerful but immature. Ring signatures + Pedersen commitments are the AK-47 of crypto privacy — simple, reliable, battle-tested. NPChain upgrades them to post-quantum and calls it done.

---

## 9. Implementation Roadmap

### Phase 1: Dandelion++ (Testnet)
- [ ] Stem/fluff routing in P2P layer
- [ ] Configurable stem probability
- [ ] Embargo timer for re-announcement

### Phase 2: Stealth Mining Addresses (Testnet)
- [ ] Master keypair in wallet
- [ ] One-time address derivation per block
- [ ] Wallet scanning for owned addresses
- [ ] Balance aggregation

### Phase 3: Stealth Payment Addresses (Pre-Mainnet)
- [ ] Scan key + spend key generation
- [ ] Kyber-based shared secret for one-time addresses
- [ ] Recipient detection scanning

### Phase 4: Pedersen Commitments (Mainnet)
- [ ] Lattice-based Pedersen commitment scheme
- [ ] Range proofs (lattice Bulletproof analog)
- [ ] Homomorphic balance verification
- [ ] Privacy level selection in wallet

### Phase 5: Ring Signatures (Mainnet)
- [ ] Lattice-based ring signature construction
- [ ] Key image generation and tracking
- [ ] Ring member selection algorithm
- [ ] Spent key image database

### Phase 6: L2 Private Batches (Post-Mainnet)
- [ ] Private L2 transaction format
- [ ] Ring sig + commitment batch compilation to SAT
- [ ] Private state root commitments

---

## 10. Security Guarantees

```
Against a classical adversary:
  Sender anonymity:    1-in-N (ring size N, default 11)
  Amount privacy:      Information-theoretic (Pedersen hiding)
  Receiver privacy:    Computational (Kyber KEM)
  IP privacy:          Statistical (Dandelion++ routing)
  Double-spend:        Impossible (key images)

Against a quantum adversary:
  Sender anonymity:    1-in-N (lattice ring sigs, Grover irrelevant)
  Amount privacy:      Information-theoretic (unchanged)
  Receiver privacy:    Computational (Kyber post-quantum)
  IP privacy:          Statistical (unchanged, no crypto involved)
  Double-spend:        Impossible (SHA3 key images, Grover gives √ speedup only)

All privacy features survive quantum computers.
No emergency upgrade needed when quantum arrives.
```

---

*NPChain Privacy: Proven technology. Post-quantum math. No experiments.*

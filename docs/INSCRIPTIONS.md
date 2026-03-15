# NPChain Inscriptions & Knowledge Layer

## Making Every Block Educational, Every Coin Unique

---

## 1. CS Knowledge Hints

Every block on NPChain contains a computer science hint — a breadcrumb that teaches miners and explorers about the mathematics powering the chain.

### How It Works

The hint is deterministically selected based on the block hash, so it's verifiable and consistent across all nodes:

```
hint_index = SHA3(block_hash || "knowledge") mod TOTAL_HINTS
hint = KNOWLEDGE_BASE[hint_index]
```

### Hint Categories

Each hint maps to the NP-problem type solved in that block:

```
Block hash mod 4 = 0 (k-SAT):
  Hints about Boolean logic, satisfiability, Cook-Levin theorem,
  DPLL algorithm, circuit complexity, P vs NP

Block hash mod 4 = 1 (Subset-Sum):
  Hints about number theory, dynamic programming, knapsack problems,
  meet-in-the-middle, public-key cryptography origins

Block hash mod 4 = 2 (Graph Coloring):
  Hints about graph theory, four-color theorem, chromatic polynomials,
  register allocation, scheduling problems

Block hash mod 4 = 3 (Hamiltonian Path):
  Hints about combinatorics, traveling salesman, Euler vs Hamilton,
  DNA computing, network routing
```

### Example Hints

```
SAT Hints:
  "The Cook-Levin theorem (1971) proved SAT was the first NP-complete problem."
  "DPLL, invented in 1962, is still the basis of modern SAT solvers."
  "Your miner just solved a problem that no known algorithm can solve in polynomial time."
  "The P vs NP question carries a $1,000,000 Clay Millennium Prize."
  "Every NP problem can be reduced to SAT — that's what NP-completeness means."
  "CDCL (Conflict-Driven Clause Learning) revolutionized SAT solving in 1996."
  "The satisfiability threshold for random 3-SAT is approximately 4.267 clauses per variable."
  "Boolean satisfiability has applications in hardware verification, AI planning, and cryptanalysis."

Subset-Sum Hints:
  "Subset-Sum is the basis of several early public-key cryptosystems."
  "The knapsack problem — Subset-Sum's cousin — optimizes everything from cargo loading to investments."
  "Meet-in-the-middle splits the problem in half: O(2^n) becomes O(2^(n/2))."
  "Merkle and Hellman proposed a knapsack-based cryptosystem in 1978."
  "Dynamic programming solves Subset-Sum in pseudo-polynomial time: O(n·W)."
  "The density of a Subset-Sum instance determines its hardness."

Graph Coloring Hints:
  "The four-color theorem: every planar map needs at most 4 colors. Proved by computer in 1976."
  "Graph coloring assigns CPU registers in compilers — your code runs faster because of this math."
  "The chromatic number of a graph is the minimum colors needed. Finding it is NP-hard."
  "Sudoku is a graph coloring problem on a 9-coloring of 81 vertices."
  "Frequency assignment in cell networks is graph coloring in disguise."
  "Brooks' theorem: every connected graph can be colored with Δ colors (max degree) except complete graphs and odd cycles."

Hamiltonian Path Hints:
  "Hamilton invented this problem in 1857 as a puzzle game called the Icosian Game."
  "The Traveling Salesman Problem is Hamiltonian Path with distances — one of the most studied problems in CS."
  "DNA computing solved a Hamiltonian Path instance in 1994 — the first biological computer."
  "Ore's theorem: if deg(u)+deg(v) >= n for all non-adjacent u,v, a Hamiltonian cycle exists."
  "Every tournament (complete directed graph) contains a Hamiltonian path."
  "Package delivery routes, circuit board drilling, and genome sequencing all reduce to Hamiltonian Path."

General CS Hints:
  "This block's witness proves P ≠ NP would be false — if finding witnesses were easy."
  "Your miner did something a quantum computer can only speed up quadratically. Not exponentially."
  "NP doesn't mean 'Non-Polynomial.' It means 'Nondeterministic Polynomial' — verifiable in poly time."
  "Alan Turing proved in 1936 that some problems are fundamentally unsolvable by any computer."
  "Stephen Cook and Leonid Levin independently proved NP-completeness in 1971."
  "There are more NP-complete problems than atoms in the observable universe."
  "Every time you mine a block, you demonstrate the P vs NP asymmetry: hard to find, easy to verify."
```

### Storage in Block

```
Block structure addition:

struct Block {
    // ... existing fields ...
    
    // Knowledge layer
    uint16_t hint_index;        // Index into knowledge base
    std::string hint_text;      // The actual hint (for convenience)
    
    // Computed deterministically:
    // hint_index = SHA3(hash || "knowledge") mod NUM_HINTS
    // hint_text = KNOWLEDGE_BASE[hint_index]
};
```

---

## 2. Coin Inscriptions

Every block reward on mainnet carries a unique inscription — a mathematical fingerprint derived from the NP-instance that was solved to mine it.

### The Inscription

```
Each mined batch of Certs carries an inscription with:

1. PROBLEM SIGNATURE
   A compact representation of the NP-instance that was solved:
   
   For SAT:    "SAT-{vars}v-{clauses}c-{satisfying_vars}"
   For SS:     "SS-{set_size}n-{target}-{subset_hash}"
   For GC:     "GC-{vertices}v-{edges}e-{colors}k"
   For HAM:    "HAM-{vertices}v-{path_hash}"

2. WITNESS FINGERPRINT
   First 8 bytes of SHA3(witness_data):
   "WIT:a7f3b2c1..."
   
   This is unique to the exact solution the miner found.
   Two miners solving the same instance would have different fingerprints
   (if they found different valid witnesses).

3. DIFFICULTY STAMP
   "D{difficulty}" — the difficulty level when this block was mined
   
4. GENESIS DISTANCE
   "G+{height}" — blocks since genesis

Full inscription example:
  "SAT-142v-604c | WIT:a7f3b2c1 | D47 | G+150000 | cert1abc..."
```

### Why This Matters

```
1. PROVENANCE
   Every Cert has a traceable mathematical origin.
   "These 190,258 Certs were born from solving a 142-variable 
   SAT instance at difficulty 47, block 150,000."
   
   Like a gemstone with a certificate of origin.

2. UNIQUENESS  
   No two blocks produce the same inscription.
   Even if two miners solve the same instance, their witnesses differ.
   Each witness fingerprint is unique.

3. COLLECTIBILITY
   Early blocks with low difficulty = "genesis era" Certs
   High difficulty blocks = "harder earned" Certs
   Rare problem types at certain heights = special editions
   
   This creates a natural collector culture around block provenance.

4. EDUCATION
   Every Cert tells a story about computation.
   "I own Certs from a 500-variable Graph Coloring problem."
   People learn about NP-completeness by holding the coins.

5. AUDIT TRAIL
   The inscription links directly to the block's NP-instance.
   Anyone can regenerate the instance and verify the witness.
   Mathematical proof of work is embedded in the currency itself.
```

### Inscription Format (On-Chain)

```
struct CoinInscription {
    // Problem identity
    uint8_t problem_type;       // 0=SAT, 1=SS, 2=GC, 3=HAM
    uint32_t problem_size;      // Variables/vertices/set_size
    uint32_t problem_complexity; // Clauses/edges/target_bits
    
    // Witness fingerprint (first 8 bytes of SHA3(witness))
    uint8_t witness_fp[8];
    
    // Mining context
    uint32_t difficulty;
    uint64_t height;
    uint64_t timestamp;
    
    // Miner mark (first 8 bytes of miner address hash)
    uint8_t miner_fp[8];
    
    // Human-readable inscription string
    std::string to_string() const {
        const char* types[] = {"SAT","SS","GC","HAM"};
        char buf[256];
        snprintf(buf, sizeof(buf), 
            "%s-%uv-%uc | WIT:%s | D%u | G+%lu",
            types[problem_type],
            problem_size, problem_complexity,
            hex(witness_fp, 8).c_str(),
            difficulty, height);
        return std::string(buf);
    }
    
    // Compact 32-byte hash of full inscription
    Hash256 inscription_hash() const {
        // SHA3 of all inscription fields
        // This is stored on-chain with the UTXO
    }
};
```

### RPC Endpoint

```
GET /api/v1/inscription/{height}

Response:
{
  "height": 150000,
  "inscription": "SAT-142v-604c | WIT:a7f3b2c1 | D47 | G+150000",
  "problem_type": "k-SAT",
  "problem_size": 142,
  "problem_complexity": 604,
  "witness_fingerprint": "a7f3b2c1e5d9f087",
  "difficulty": 47,
  "miner": "cert1abc...",
  "hint": "CDCL (Conflict-Driven Clause Learning) revolutionized SAT solving in 1996.",
  "reward": 190258.42,
  "timestamp": 1710456789
}
```

### Explorer Integration

```
Block Explorer shows for each block:

┌──────────────────────────────────────────────────────┐
│  Block #150,000                                       │
│  ─────────────────────────────────────────────        │
│  Problem:    k-SAT (142 variables, 604 clauses)       │
│  Witness:    a7f3b2c1e5d9f087                         │
│  Difficulty: 47                                       │
│  Reward:     190,258.42 Certs                         │
│  Miner:      cert1abc...                              │
│                                                       │
│  ┌─────────────────────────────────────────────────┐  │
│  │  📖 "CDCL (Conflict-Driven Clause Learning)     │  │
│  │   revolutionized SAT solving in 1996."          │  │
│  └─────────────────────────────────────────────────┘  │
│                                                       │
│  Inscription: SAT-142v-604c | WIT:a7f3b2c1 | D47     │
└──────────────────────────────────────────────────────┘
```

---

## 3. Wallet Integration

### Balance View with Inscriptions

```
Wallet shows your Certs grouped by origin:

┌──────────────────────────────────────────────────────┐
│  Your Certs                                           │
│  ─────────────────────────                            │
│  Total: 1,426,940.56 Certs                            │
│                                                       │
│  Origin Breakdown:                                    │
│  ├── SAT blocks:     571,234.00 (12 blocks)           │
│  ├── Subset-Sum:     380,516.00 (8 blocks)            │
│  ├── Graph Color:    285,122.56 (6 blocks)            │
│  └── Hamiltonian:    190,068.00 (4 blocks)            │
│                                                       │
│  Rarest inscription:                                  │
│  "HAM-89v | WIT:01a2b3c4 | D1 | G+7" (Genesis era!)  │
└──────────────────────────────────────────────────────┘
```

### Transaction History with Inscriptions

```
Each transaction shows the inscription of the Certs being sent:

Sent 50,000 Certs to cert1def...
  Origin: Block #247 | SAT-42v-178c | WIT:ff91a2b3 | D3
  Hint: "Every NP problem can be reduced to SAT."
```

---

## 4. Special Inscriptions

### Genesis Block

```
Block #0 inscription:
  "GENESIS | The P vs NP question remains open. | 2026"
  
This inscription is unique and can never be replicated.
Genesis Certs are the most valuable by definition.
```

### Milestone Blocks

```
Block #1,000:     "1K | First thousand blocks mined."
Block #10,000:    "10K | Ten thousand proofs of NP-witness."
Block #100,000:   "100K | One hundred thousand computational challenges solved."
Block #1,000,000: "1M | A million witnesses to the hardness of NP."
Block #525,600:   "1 YEAR | 525,600 blocks. One year of proving P ≠ NP (probably)."
```

### Difficulty Milestones

```
First block at difficulty 10:   "D10 FIRST | Difficulty frontier pushed."
First block at difficulty 100:  "D100 FIRST | Problems getting serious."
First block at difficulty 1000: "D1000 FIRST | Industrial-grade computation."
```

### Problem Type Records

```
Largest SAT instance solved:     "SAT RECORD | {vars}v-{clauses}c"
Largest Graph Coloring solved:   "GC RECORD | {vertices}v-{colors}k"
Fastest solve at difficulty N:   "SPEED RECORD | D{N} in {ms}ms"
```

---

## 5. Implementation

### Knowledge Base (embedded in node)

```cpp
// Array of CS hints, indexed by SHA3(block_hash || "knowledge") mod size
static const std::vector<std::string> KNOWLEDGE_BASE = {
    // SAT hints (indices 0-49)
    "The Cook-Levin theorem (1971) proved SAT was the first NP-complete problem.",
    "DPLL, invented in 1962, is still the basis of modern SAT solvers.",
    // ... 200+ hints covering all four problem types + general CS ...
};

// Inscription generation
CoinInscription generate_inscription(const Block& block) {
    CoinInscription insc;
    insc.problem_type = block.prev_hash[0] % 4;
    insc.problem_size = block.num_variables;
    insc.problem_complexity = block.num_clauses;
    
    // Witness fingerprint
    auto wh = sha3_256(block.witness_data);
    std::memcpy(insc.witness_fp, wh.data(), 8);
    
    insc.difficulty = block.difficulty;
    insc.height = block.height;
    insc.timestamp = block.timestamp;
    
    // Miner fingerprint
    auto mh = sha3_256(block.miner_address);
    std::memcpy(insc.miner_fp, mh.data(), 8);
    
    return insc;
}

// Knowledge hint selection
std::string get_hint(const Hash256& block_hash) {
    Bytes seed(block_hash.begin(), block_hash.end());
    seed.insert(seed.end(), {'k','n','o','w'});
    auto h = sha3_256(ByteSpan{seed.data(), seed.size()});
    uint32_t index;
    std::memcpy(&index, h.data(), 4);
    return KNOWLEDGE_BASE[index % KNOWLEDGE_BASE.size()];
}
```

### Block Structure Addition

```cpp
struct Block {
    // ... existing fields ...
    
    // Inscription (computed, not stored separately)
    CoinInscription inscription() const {
        return generate_inscription(*this);
    }
    
    // Knowledge hint (computed from hash)
    std::string hint() const {
        return get_hint(hash);
    }
};
```

### RPC Addition

```
Existing block response gains two new fields:

GET /api/v1/blocks
{
  "blocks": [{
    "height": 150,
    "hash": "...",
    "miner": "cert1...",
    "reward": 47564688000,
    // NEW:
    "inscription": "SAT-10v-30c | WIT:a7f3b2c1 | D1 | G+150",
    "hint": "The P vs NP question carries a $1,000,000 Clay Millennium Prize."
  }]
}
```

---

## 6. Cultural Impact

This isn't just a technical feature. It's a cultural layer:

**Education:** Every block teaches something about computer science. Miners learn about NP-completeness, graph theory, number theory, and algorithms just by watching their node output.

**Identity:** "I mine on NPChain" becomes "I solve NP-complete problems for a living." The inscriptions prove it.

**Collectibility:** Early genesis-era Certs with low difficulty inscriptions become collector items. Milestone block inscriptions become legendary.

**Storytelling:** Every Cert has a mathematical biography. Where it was born, what problem was solved, how hard it was. Money with a story.

**Community:** "Did you see the hint on block 500,000?" becomes a conversation starter. CS education embedded in culture.

NPChain doesn't just process transactions. It teaches.

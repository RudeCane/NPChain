# Block & Transaction Structure

## Block Fields

| Field | Type | Description |
|-------|------|-------------|
| version | uint32 | Block format version |
| height | uint64 | Block number (0 = genesis) |
| prev_hash | Hash256 | SHA3-256 of previous block |
| merkle_root | Hash256 | Merkle root of transactions |
| timestamp | uint64 | Unix timestamp |
| difficulty | uint32 | Current mining difficulty |
| num_variables | uint32 | NP-instance variable count |
| num_clauses | uint32 | NP-instance clause count |
| witness | vector<bool> | Solution to the NP-instance |
| witness_hash | Hash256 | SHA3-256 of the witness |
| reward | uint64 | Block reward in base units |
| miner_address | string | cert1... address of miner |
| hash | Hash256 | SHA3-256 of the block |

## Block Hash Computation

```
hash = SHA3-256(
    version || height || prev_hash || merkle_root ||
    timestamp || difficulty || witness_hash || reward ||
    miner_address
)
```

## Genesis Block

The genesis block (height 0) has:
- `prev_hash` = all zeros
- First block reward (~47,564 Certs on testnet)
- Valid NP-instance witness generated from the zero hash

## Chain Persistence

Blocks are saved to `npchain_blocks.dat` in binary format with magic header `NPCB`. On load, every block is fully re-validated including witness verification.

# P2P Protocol

## Overview

NPChain nodes communicate over a binary TCP protocol on port 19333 (testnet).

## Message Types

| Type | ID | Purpose |
|------|------|---------|
| HELLO | 0x01 | Exchange chain height, hash, miner address |
| GET_BLOCKS | 0x02 | Request blocks from a specific height |
| BLOCKS | 0x03 | Send requested blocks |
| NEW_BLOCK | 0x04 | Broadcast a freshly mined block |
| PING | 0x05 | Keepalive |
| PONG | 0x06 | Keepalive response |

## Connection Flow

1. Node A connects to Node B via TCP
2. Both exchange HELLO messages (chain height, tip hash, miner address)
3. If one has a longer chain, the shorter node requests blocks
4. Once synced, both mine competitively and broadcast new blocks

## Anti-Sybil Protection

- **Max 2 connections per IP** — prevents connection flooding
- **One HELLO per peer** — prevents HELLO spam loops
- **Exponential backoff** on sync failures (1s → 2s → 4s → 8s → 30s max)

## Wire Format

Each message: `[4-byte type][4-byte length][payload]`

Blocks are serialized with all fields (height, prev_hash, witness, miner address, etc.) in a compact binary format.

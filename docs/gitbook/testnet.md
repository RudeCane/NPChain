# Testnet Info

## Current Testnet

| Parameter | Value |
|-----------|-------|
| Chain ID | TCR |
| Block time | 15 seconds |
| Block reward | ~47,564 Certs |
| P2P port | 19333 |
| RPC port | 18333 |
| Seed node | 47.197.198.200:19333 |
| Difficulty adjustment | Every 36 blocks |

## Join the Testnet

1. Build the node (see [Installation](installation.md))
2. Create a wallet (see [Web Wallet Guide](wallet.md))
3. Start mining:

```bash
./npchain_testnet.exe --address cert1YOUR_ADDRESS --seed 47.197.198.200:19333
```

## Testnet Certs

Testnet Certs are real — they will convert to mainnet Certs at a 1000:1 ratio when mainnet launches. Mine early to earn your allocation.

## Chain Persistence

Your node saves the chain to disk (`npchain_blocks.dat`) after every block and on shutdown. When you restart, it loads the chain from disk and resumes mining.

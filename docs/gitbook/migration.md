# Testnet to Mainnet Migration

## Overview

Testnet miners earn Certs that convert to mainnet Certs at a **1000:1 ratio**.

## Migration Flow

1. **Mine on testnet** — accumulate Certs
2. **Snapshot** — admin freezes all balances at a specific block height
3. **KYC verification** — bind your testnet wallet to your identity
4. **Mainnet genesis** — verified balances included as initial allocation
5. **Mainnet launch** — claim your converted Certs

## Conversion

| Testnet Earned | Mainnet Received |
|---------------|-----------------|
| 47,564 Certs | ~47 Certs |
| 500,000 Certs | 500 Certs |
| 5,000,000 Certs | 5,000 Certs |

## Eligibility

- Minimum ~47,564 testnet Certs (1 block reward)
- KYC verification completed
- Wallet included in snapshot

## Security

- Snapshot protected by admin password
- Merkle tree computed over all balances
- Merkle root embedded in mainnet genesis block
- Full cryptographic proof chain from testnet to mainnet

## Check Your Status

```bash
curl http://localhost:18333/api/v1/migration/check/cert1YOUR_ADDRESS
```

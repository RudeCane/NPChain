# RPC API Reference

The node runs an HTTP API on port 18333 (testnet).

## Chain Endpoints

| Endpoint | Method | Returns |
|----------|--------|---------|
| `/api/v1/status` | GET | Chain height, difficulty, supply, peers, version |
| `/api/v1/blocks` | GET | Recent blocks with miner, reward, hash |
| `/api/v1/balance/<address>` | GET | Balance for any cert1... address |
| `/api/v1/peers` | GET | Connected peer list |
| `/api/v1/miner` | GET | Current miner address, balance, blocks mined |

## Governance Endpoints

| Endpoint | Method | Returns |
|----------|--------|---------|
| `/api/v1/governance/proposals` | GET | All proposals with vote tallies |
| `/api/v1/governance/proposal/<id>` | GET | Single proposal details |
| `/api/v1/governance/submit` | POST | Submit a new proposal |
| `/api/v1/governance/vote` | POST | Cast a vote |
| `/api/v1/governance/config` | GET | Governance parameters |

## Migration Endpoints

| Endpoint | Method | Returns |
|----------|--------|---------|
| `/api/v1/migration/status` | GET | Migration status, eligible wallets |
| `/api/v1/migration/check/<address>` | GET | Check mainnet allocation |
| `/api/v1/migration/snapshot` | POST | Take balance snapshot (admin) |
| `/api/v1/migration/kyc` | POST | KYC verify a wallet (admin) |
| `/api/v1/migration/genesis` | GET | Export mainnet genesis allocation |

## Example

```bash
curl http://localhost:18333/api/v1/status
```

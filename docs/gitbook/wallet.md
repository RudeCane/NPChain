# Web Wallet Guide

## Create Your Wallet

1. Start the wallet web server:
```bash
cd web
python3 -m http.server 8888
```

2. Open `http://localhost:8888` in your browser
3. Create a password (8+ characters)
4. Your `cert1...` address is generated
5. Copy the mining command shown on screen

## Dashboard

Once your node is mining, the dashboard shows:
- **Balance** — your total Certs earned
- **Chain Status** — height, difficulty, supply
- **Blocks You Mined** — your block count
- **Recent Blocks** — highlights blocks you mined
- **Mining Command** — copy button for quick restart

## Security

Your wallet keys are encrypted with your password using SHA3-256 key derivation (5000 rounds). Nothing leaves your browser. The wallet is fully client-side.

## Returning Users

When you come back, enter your password to unlock. Your address is stored locally — your balance is fetched from the running node's RPC.

## Links

From the wallet dashboard:
- **Governance & Voting** — vote on proposals
- **Block Explorer** — browse all blocks

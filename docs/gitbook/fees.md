# Fee Structure

## EIP-1559 Model

NPChain uses Ethereum's EIP-1559 fee mechanism:

- **Base fee** — algorithmically determined, burned (destroyed)
- **Tip** — optional, paid to miner
- **Total fee** = base fee + tip

## Base Fee Adjustment

The base fee adjusts based on block utilization:
- Block > 50% full → base fee increases
- Block < 50% full → base fee decreases
- Empty blocks → base fee trends toward minimum

## Burn Mechanics

100% of the base fee is burned. This creates deflationary pressure as transaction volume grows. At high enough usage, the burn rate exceeds block rewards, making Certs deflationary.

## Current Status

Transaction fees are not yet active on testnet. The fee mechanism will be enabled before mainnet launch.

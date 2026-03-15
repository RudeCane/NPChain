# Hardware Guide

## CPU is King

NPChain mining is CPU-bound. SAT solvers use branch prediction, cache lookups, and sequential logic — not parallel hash grinding. GPUs and ASICs provide no advantage.

## Recommended Builds

| Tier | CPU | RAM | Cost |
|------|-----|-----|------|
| Budget | AMD Ryzen 5 7600 | 32 GB DDR5 | ~$600 |
| Best Value | AMD Ryzen 7 7800X3D | 64 GB DDR5 | ~$1,000 |
| High-End | AMD Ryzen 9 7950X | 128 GB DDR5 | ~$2,500 |
| Professional | AMD Threadripper 7980X | 256 GB DDR5 | ~$8,000 |

## Why AMD Ryzen 7 7800X3D?

The 96MB 3D V-Cache keeps clause data close to the CPU, dramatically speeding up WalkSAT and CDCL algorithms. Best mining performance per dollar.

## Don't Buy

- **GPUs** — SAT solvers don't benefit from GPU parallelism
- **ASICs** — impossible to build for rotating NP-complete problems
- **High-core-count Xeons** — single-thread speed matters more than core count

## Current Recommendation

Don't buy specialized hardware yet. Testnet difficulty is trivial. Mine on your current PC. Only invest when mainnet launches and difficulty justifies it.

#pragma once

#include "utils/types.hpp"

namespace npchain::testnet {

// ═══════════════════════════════════════════════════════════════════
//  TESTNET CONFIGURATION
//
//  Overrides for faster block times, easier mining, and smaller
//  committee sizes. These let you test the full consensus pipeline
//  on commodity hardware without waiting hours.
//
//  Key differences from mainnet:
//    - 10-second blocks (vs 60s mainnet)
//    - Lower starting difficulty (solvable on a laptop)
//    - Smaller validator committee (3-of-5 vs 14-of-21)
//    - Shorter checkpoint interval
//    - Testnet chain ID (prevents replay on mainnet)
//    - 100x emission rate (for faucet testing)
//    - Relaxed peer diversity (run multiple nodes on localhost)
// ═══════════════════════════════════════════════════════════════════

constexpr uint32_t TESTNET_CHAIN_ID           = 0x544352;   // "TCR" (Testnet Certs)
constexpr uint32_t TESTNET_PROTOCOL_VERSION   = 1;

// ─── Timing ───
constexpr uint64_t TESTNET_BLOCK_TIME_SEC     = 10;          // 10 seconds (6x faster)
constexpr uint64_t TESTNET_DIFFICULTY_WINDOW   = 30;          // ~5 minutes of blocks
constexpr double   TESTNET_MAX_DIFFICULTY_ADJ  = 2.0;         // Allow faster swings

// ─── Emission (10x mainnet rate for testing) ───
constexpr uint64_t TESTNET_BLOCKS_PER_YEAR    = 3'153'600;   // 10s blocks × 365.25 days
constexpr uint64_t TESTNET_ANNUAL_CAP         = 525'000'000ULL * 1'000'000'000ULL; // 525M tCerts/year
constexpr uint64_t TESTNET_BLOCK_REWARD       = TESTNET_ANNUAL_CAP / TESTNET_BLOCKS_PER_YEAR;

// ─── Consensus (smaller committee for testing) ───
constexpr uint32_t TESTNET_COMMITTEE_SIZE     = 5;
constexpr uint32_t TESTNET_QUORUM             = 3;
constexpr uint32_t TESTNET_CHECKPOINT_INTERVAL = 100;

// ─── Network (relaxed for local testing) ───
constexpr uint32_t TESTNET_MIN_PEER_SUBNETS   = 1;           // Allow all on localhost
constexpr uint16_t TESTNET_DEFAULT_PORT        = 19333;       // Different from mainnet 9333
constexpr uint16_t TESTNET_RPC_PORT            = 18332;       // JSON-RPC for wallets/explorers

// ─── Mining (easy enough for a single CPU core) ───
constexpr uint64_t TESTNET_GENESIS_DIFFICULTY  = 1;           // Trivially easy
constexpr uint32_t TESTNET_MAX_SAT_VARS       = 20;          // Cap problem size for speed
constexpr uint32_t TESTNET_VDF_ITERATIONS      = 1000;        // Much less than mainnet

// ─── Faucet ───
constexpr uint64_t TESTNET_FAUCET_DRIP        = 1000ULL * 1'000'000'000ULL;  // 1000 tCerts per request
constexpr uint64_t TESTNET_FAUCET_COOLDOWN_SEC = 60;          // 1 request per minute

// ─── Genesis Accounts (pre-funded for testing) ───
// These get tCerts at genesis for immediate testing
constexpr uint64_t TESTNET_PREFUND_AMOUNT     = 10'000'000ULL * 1'000'000'000ULL; // 10M tCerts each

// ─── Helper: check if running testnet ───
inline bool is_testnet(uint32_t chain_id) noexcept {
    return chain_id == TESTNET_CHAIN_ID;
}

} // namespace npchain::testnet

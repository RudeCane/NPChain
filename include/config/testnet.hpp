#pragma once

#include "utils/types.hpp"

namespace npchain::testnet {

// ═══════════════════════════════════════════════════════════════════
//  TESTNET CONFIGURATION
//
//  Modified parameters that make testing practical:
//    - Faster block times (15s instead of 60s)
//    - Lower difficulty (solve in seconds, not minutes)
//    - Smaller committee (3 validators instead of 21)
//    - Separate chain ID (prevents tx replay to mainnet)
//    - Faucet integration for free test coins
//    - Reduced crypto sizes optional (faster key generation)
// ═══════════════════════════════════════════════════════════════════

// ─── Testnet Overrides ─────────────────────────────────────────────

constexpr uint32_t TESTNET_CHAIN_ID          = 0x544352;  // "TCR" (Test Certs)
constexpr uint32_t TESTNET_PROTOCOL_VERSION  = 1;
constexpr uint64_t TESTNET_BLOCK_TIME_SEC    = 15;         // 4x faster than mainnet
constexpr uint64_t TESTNET_DIFFICULTY_WINDOW = 36;          // Adjust every ~9 minutes
constexpr double   TESTNET_MAX_DIFFICULTY_ADJ = 2.0;       // Allow bigger swings (test flexibility)

// ─── Testnet Emission (still unlimited, just faster for testing) ───
constexpr uint64_t TESTNET_BLOCKS_PER_YEAR     = 2'102'400;          // 15s × 2,102,400 = 1 year
constexpr uint64_t TESTNET_ANNUAL_EMISSION     = 100'000'000'000ULL * 1'000'000ULL; // 100B Certs/year (base units)
constexpr uint64_t TESTNET_BLOCK_REWARD        = TESTNET_ANNUAL_EMISSION / TESTNET_BLOCKS_PER_YEAR;
constexpr uint64_t TESTNET_COMPLEXITY_FRAGS    = TESTNET_ANNUAL_EMISSION % TESTNET_BLOCKS_PER_YEAR;

// ─── Testnet Consensus ─────────────────────────────────────────────
constexpr uint32_t TESTNET_THRESHOLD_COMMITTEE = 3;        // Small committee for testing
constexpr uint32_t TESTNET_THRESHOLD_QUORUM    = 2;        // 2-of-3
constexpr uint32_t TESTNET_CHECKPOINT_INTERVAL = 100;      // Frequent checkpoints
constexpr uint32_t TESTNET_MIN_PEER_SUBNETS    = 1;        // Relaxed for local testing
constexpr uint32_t TESTNET_MAX_BLOCK_SIZE      = 2'097'152; // 2 MB

// ─── Testnet Genesis Parameters ────────────────────────────────────
constexpr uint64_t TESTNET_GENESIS_TIMESTAMP   = 0;        // Set at launch time
constexpr uint64_t TESTNET_GENESIS_DIFFICULTY  = 1;        // Trivially easy at start

// Faucet drip amount (test coins are free)
constexpr uint64_t TESTNET_FAUCET_DRIP         = 10'000ULL * 1'000'000'000ULL; // 10,000 Certs per drip
constexpr uint64_t TESTNET_FAUCET_COOLDOWN_SEC = 3600;     // 1 hour between drips per address

// ─── Network Seeds ─────────────────────────────────────────────────
// These get populated by the deployment script
struct TestnetSeeds {
    static constexpr uint16_t DEFAULT_PORT = 19333;  // Different from mainnet 9333

    // Seed node addresses (set during deployment)
    // Format: "host:port"
    static inline std::vector<std::string> seed_nodes = {};
};

// ─── Helper: Get testnet-adjusted value ────────────────────────────
// Use these instead of the mainnet constants when running in testnet mode

struct TestnetConfig {
    bool     enabled            = false;
    uint32_t chain_id           = TESTNET_CHAIN_ID;
    uint64_t block_time         = TESTNET_BLOCK_TIME_SEC;
    uint64_t difficulty_window  = TESTNET_DIFFICULTY_WINDOW;
    uint64_t block_reward       = TESTNET_BLOCK_REWARD;
    uint32_t committee_size     = TESTNET_THRESHOLD_COMMITTEE;
    uint32_t quorum             = TESTNET_THRESHOLD_QUORUM;
    uint32_t min_subnets        = TESTNET_MIN_PEER_SUBNETS;
    uint16_t default_port       = TestnetSeeds::DEFAULT_PORT;
    std::string data_dir        = "./npchain_testnet_data";
    std::vector<std::string> seed_nodes;

    /// Load testnet config (returns mainnet defaults if not testnet)
    static TestnetConfig load(bool is_testnet = true);
};

} // namespace npchain::testnet

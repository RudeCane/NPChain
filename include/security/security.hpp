#pragma once

#include "utils/types.hpp"
#include "crypto/dilithium.hpp"
#include "crypto/hash.hpp"

#include <deque>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace npchain::security {

using namespace npchain::crypto;

// ═══════════════════════════════════════════════════════════════════
//  Threshold Signature Manager
//
//  Implements t-of-n Dilithium threshold signing for block finality.
//  A block is finalized when THRESHOLD_QUORUM (14) of
//  THRESHOLD_COMMITTEE (21) validators co-sign it.
// ═══════════════════════════════════════════════════════════════════

class ThresholdSigManager {
public:
    struct CommitteeMember {
        uint32_t index;
        std::array<uint8_t, DILITHIUM_PK_SIZE> pubkey;
        Amount   stake;
        uint64_t ponw_contributions;  // Recent PoNW mining blocks
    };

    /// Initialize with the current committee
    void set_committee(
        uint32_t epoch,
        std::vector<CommitteeMember> members
    );

    /// Produce a partial signature for a block hash (as a committee member)
    [[nodiscard]] Result<DilithiumSignature> sign_block(
        const Hash256& block_hash,
        uint32_t my_index,
        const SecureArray<DILITHIUM_SK_SIZE>& my_secret_key
    );

    /// Collect and verify a partial signature from another committee member
    [[nodiscard]] bool add_partial_signature(
        const Hash256& block_hash,
        uint32_t signer_index,
        const DilithiumSignature& signature
    );

    /// Check if we have quorum for a block
    [[nodiscard]] bool has_quorum(const Hash256& block_hash) const;

    /// Assemble the threshold signature set
    [[nodiscard]] std::optional<ThresholdSignatureSet> assemble(
        const Hash256& block_hash
    ) const;

    /// Verify a complete threshold signature set
    [[nodiscard]] bool verify_threshold_sigs(
        const Hash256& block_hash,
        const ThresholdSignatureSet& sigs
    ) const;

private:
    uint32_t epoch_ = 0;
    std::vector<CommitteeMember> committee_;
    // block_hash → (signer_index → signature)
    std::unordered_map<Hash256, std::map<uint32_t, DilithiumSignature>,
        struct Hash256Hasher {
            size_t operator()(const Hash256& h) const noexcept {
                size_t result;
                std::memcpy(&result, h.data(), sizeof(result));
                return result;
            }
        }> collected_sigs_;
    mutable std::mutex mutex_;
};

// ═══════════════════════════════════════════════════════════════════
//  Anti-Flood Protection (Memory-Hard Auxiliary Puzzle)
//
//  Before a block can be submitted to the network, the miner must
//  solve a secondary memory-hard puzzle (Argon2id-based).
//  This prevents block flooding / DoS attacks where an attacker
//  rapidly broadcasts garbage blocks.
//
//  This is NOT part of consensus — it's a network admission filter.
// ═══════════════════════════════════════════════════════════════════

class AntiFloodGuard {
public:
    struct Config {
        uint32_t argon2_time_cost   = 3;        // Iterations
        uint32_t argon2_memory_kb   = 65536;    // 64 MB
        uint32_t argon2_parallelism = 1;        // Sequential (anti-parallel)
        uint32_t difficulty_bits    = 16;        // Leading zero bits required
    };

    explicit AntiFloodGuard(Config cfg = {});

    /// Compute anti-flood proof for a block
    struct FloodProof {
        Hash256  output;
        uint64_t nonce;
    };

    [[nodiscard]] FloodProof compute_proof(const Hash256& block_hash);

    /// Verify anti-flood proof
    [[nodiscard]] bool verify_proof(
        const Hash256& block_hash,
        const FloodProof& proof
    ) const noexcept;

private:
    Config config_;
};

// ═══════════════════════════════════════════════════════════════════
//  Peer Reputation / Scoring System
//
//  Tracks peer behavior and assigns trust scores.
//  Used for: Sybil resistance, eclipse attack prevention,
//  selfish mining detection, network health.
// ═══════════════════════════════════════════════════════════════════

class PeerScoring {
public:
    struct PeerRecord {
        std::string peer_id;
        std::string ip_subnet;         // /16 subnet for diversity
        uint32_t    autonomous_system;  // AS number for AS-diversity

        double   score = 100.0;        // Start at 100, decay on bad behavior
        uint64_t valid_blocks_relayed   = 0;
        uint64_t invalid_blocks_relayed = 0;
        uint64_t valid_txs_relayed      = 0;
        uint64_t invalid_txs_relayed    = 0;
        uint64_t connection_uptime_sec  = 0;
        uint64_t protocol_violations    = 0;

        std::chrono::system_clock::time_point first_seen;
        std::chrono::system_clock::time_point last_seen;

        [[nodiscard]] bool is_banned() const noexcept { return score < 0.0; }
    };

    /// Record positive behavior
    void record_valid_block(const std::string& peer_id);
    void record_valid_tx(const std::string& peer_id);
    void record_uptime(const std::string& peer_id, uint64_t seconds);

    /// Record negative behavior (score penalties)
    void record_invalid_block(const std::string& peer_id);     // -20 points
    void record_invalid_tx(const std::string& peer_id);        // -5 points
    void record_protocol_violation(const std::string& peer_id);// -50 points
    void record_eclipse_attempt(const std::string& peer_id);   // -100 points (instant ban)

    /// Get peer score
    [[nodiscard]] double get_score(const std::string& peer_id) const;

    /// Check AS-diversity requirement (anti-eclipse)
    [[nodiscard]] bool check_subnet_diversity(
        const std::vector<std::string>& connected_subnets
    ) const;

    /// Get list of banned peers
    [[nodiscard]] std::vector<std::string> get_banned_peers() const;

    /// Decay all scores towards neutral over time
    void decay_scores(double factor = 0.99);

private:
    std::unordered_map<std::string, PeerRecord> peers_;
    mutable std::mutex mutex_;
};

// ═══════════════════════════════════════════════════════════════════
//  Replay Attack Guard
//
//  Every transaction includes chain_id + epoch_nonce.
//  Epoch nonces rotate every N blocks, preventing replay of old txs.
// ═══════════════════════════════════════════════════════════════════

class ReplayGuard {
public:
    /// Get the current epoch nonce for a given block height
    [[nodiscard]] static uint64_t epoch_nonce(BlockHeight height) noexcept;

    /// Validate that a transaction's epoch_nonce is current
    [[nodiscard]] static bool validate_epoch(
        uint64_t tx_epoch_nonce,
        BlockHeight current_height
    ) noexcept;

    /// Check chain_id matches
    [[nodiscard]] static bool validate_chain_id(uint32_t tx_chain_id) noexcept;
};

// ═══════════════════════════════════════════════════════════════════
//  Signature Canonicalization
//
//  Prevents signature malleation attacks by enforcing canonical form.
// ═══════════════════════════════════════════════════════════════════

class SignatureCanonicalizer {
public:
    /// Check if a Dilithium signature is in canonical form
    [[nodiscard]] static bool is_canonical(
        const DilithiumSignature& sig
    ) noexcept;

    /// Force a signature into canonical form
    [[nodiscard]] static DilithiumSignature canonicalize(
        const DilithiumSignature& sig
    );
};

// ═══════════════════════════════════════════════════════════════════
//  Checkpoint System
//
//  Periodic finality checkpoints to prevent long-range attacks.
//  Every CHECKPOINT_INTERVAL blocks, the chain state is checkpointed.
//  Nodes will not reorg past a checkpoint.
// ═══════════════════════════════════════════════════════════════════

class CheckpointManager {
public:
    struct Checkpoint {
        BlockHeight height;
        Hash256     block_hash;
        Hash256     state_root;
        uint64_t    timestamp;
    };

    /// Add a new checkpoint
    void add_checkpoint(Checkpoint cp);

    /// Verify a chain doesn't violate any checkpoints
    [[nodiscard]] bool validate_chain(
        const std::vector<std::pair<BlockHeight, Hash256>>& chain_hashes
    ) const;

    /// Get the latest checkpoint
    [[nodiscard]] std::optional<Checkpoint> latest() const;

    /// Check if a reorg would cross a checkpoint (forbidden)
    [[nodiscard]] bool reorg_crosses_checkpoint(
        BlockHeight fork_point,
        BlockHeight current_height
    ) const;

private:
    std::deque<Checkpoint> checkpoints_;
    mutable std::mutex mutex_;
};

} // namespace npchain::security

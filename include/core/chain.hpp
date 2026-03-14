#pragma once

#include "utils/types.hpp"
#include "core/block.hpp"
#include "consensus/ponw_engine.hpp"
#include "security/security.hpp"

#include <shared_mutex>
#include <unordered_set>
#include <functional>

namespace npchain::core {

// ═══════════════════════════════════════════════════════════════════
//  UTXO Set (Commitment-Based)
//
//  Instead of traditional UTXOs with visible amounts, NPChain
//  tracks Pedersen commitments. Spent outputs are identified by
//  nullifiers (not by UTXO reference, preserving privacy).
// ═══════════════════════════════════════════════════════════════════

class UTXOSet {
public:
    struct CommitmentEntry {
        Hash256    commitment_hash;
        Hash256    stealth_address_hash;
        BlockHeight created_height;
    };

    /// Add a new commitment (from a transaction output)
    void add(const CommitmentEntry& entry);

    /// Mark a nullifier as spent
    bool spend(const Hash256& nullifier);

    /// Check if a nullifier has been spent
    [[nodiscard]] bool is_spent(const Hash256& nullifier) const;

    /// Check if a commitment exists in the set
    [[nodiscard]] bool commitment_exists(const Hash256& commitment_hash) const;

    /// Get the Merkle root of all unspent commitments
    [[nodiscard]] Hash256 merkle_root() const;

    /// Get the Merkle root of all spent nullifiers
    [[nodiscard]] Hash256 nullifier_root() const;

    /// Get total count of unspent outputs
    [[nodiscard]] size_t size() const noexcept;

    /// Snapshot for rollback during reorg
    struct Snapshot {
        size_t commitment_count;
        size_t nullifier_count;
        Hash256 commitment_root;
        Hash256 nullifier_root;
    };

    [[nodiscard]] Snapshot snapshot() const;
    void restore(const Snapshot& snap);

private:
    // In production: these would be backed by a Merkle Patricia Trie on disk
    std::unordered_set<Hash256, security::CheckpointManager::/* reuse hasher */
        struct H256Hash {
            size_t operator()(const Hash256& h) const noexcept {
                size_t r; std::memcpy(&r, h.data(), sizeof(r)); return r;
            }
        }> commitments_;
    std::unordered_set<Hash256, H256Hash> nullifiers_;
    mutable std::shared_mutex mutex_;
};

// ═══════════════════════════════════════════════════════════════════
//  Mempool (Transaction Pool)
// ═══════════════════════════════════════════════════════════════════

class Mempool {
public:
    struct Config {
        size_t   max_size       = 50'000;  // Max pending transactions
        size_t   max_bytes      = 64 * 1024 * 1024;  // 64 MB
        uint64_t expiry_seconds = 3600;    // 1 hour TTL
    };

    explicit Mempool(Config cfg = {});

    /// Add a validated transaction
    enum class AddResult { ACCEPTED, DUPLICATE, POOL_FULL, INVALID, DOUBLE_SPEND };
    [[nodiscard]] AddResult add(Transaction tx);

    /// Remove transactions that are now in a block
    void remove_confirmed(const std::vector<Hash256>& tx_ids);

    /// Get the best transactions for a block template (by fee priority)
    [[nodiscard]] std::vector<Transaction> get_block_template(
        size_t max_transactions = 5000,
        size_t max_bytes = MAX_BLOCK_SIZE - 8192  // Leave room for header + coinbase
    );

    /// Remove expired transactions
    void evict_expired();

    /// Current pool size
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] size_t bytes() const noexcept;

private:
    Config config_;
    struct TxEntry {
        Transaction tx;
        uint64_t    added_time;
        Amount      fee_estimate;  // Estimated fee for priority
    };
    std::vector<TxEntry> pool_;
    std::unordered_set<Hash256, UTXOSet::H256Hash> tx_ids_;
    mutable std::shared_mutex mutex_;
};

// ═══════════════════════════════════════════════════════════════════
//  Blockchain State Manager
// ═══════════════════════════════════════════════════════════════════

class Chain {
public:
    struct Config {
        std::string data_dir = "./npchain_data";
        bool        prune    = false;         // Prune old block data
        uint64_t    prune_keep_blocks = 10000; // Keep last N blocks if pruning
    };

    explicit Chain(Config cfg = {});
    ~Chain();

    // ─── Block Operations ───

    /// Process and append a new block
    enum class AcceptResult {
        ACCEPTED,
        ALREADY_KNOWN,
        INVALID,
        ORPHAN,            // Parent not found — request parent
        CHECKPOINT_VIOLATION,
        REORG_TRIGGERED,
    };

    [[nodiscard]] AcceptResult accept_block(Block block);

    /// Get the current chain tip
    [[nodiscard]] const Block& tip() const;
    [[nodiscard]] Hash256 tip_hash() const;
    [[nodiscard]] BlockHeight height() const noexcept;
    [[nodiscard]] Difficulty current_difficulty() const;

    /// Get a block by hash
    [[nodiscard]] std::optional<Block> get_block(const Hash256& hash) const;

    /// Get a block by height
    [[nodiscard]] std::optional<Block> get_block_at(BlockHeight height) const;

    // ─── State Queries ───

    [[nodiscard]] const UTXOSet& utxo_set() const noexcept { return utxo_set_; }
    [[nodiscard]] const Mempool& mempool() const noexcept { return mempool_; }
    [[nodiscard]] Mempool& mempool() noexcept { return mempool_; }

    // ─── Mining Support ───

    /// Create a block template for mining
    [[nodiscard]] Block create_block_template(
        ByteSpan miner_pubkey,
        const TxOutput& coinbase_output
    );

    // ─── Callbacks ───
    using BlockAcceptedCallback = std::function<void(const Block&)>;
    using ReorgCallback = std::function<void(BlockHeight fork_point, const std::vector<Block>& new_chain)>;

    void on_block_accepted(BlockAcceptedCallback cb);
    void on_reorg(ReorgCallback cb);

private:
    Config config_;
    UTXOSet utxo_set_;
    Mempool mempool_;
    security::CheckpointManager checkpoints_;
    consensus::ConsensusEngine consensus_;

    // Chain storage (simplified — production would use LevelDB/RocksDB)
    struct ChainState;
    std::unique_ptr<ChainState> state_;
    mutable std::shared_mutex chain_mutex_;

    /// Handle chain reorganization
    AcceptResult handle_reorg(const Block& new_block);

    /// Apply a block to the UTXO set
    bool apply_block(const Block& block);

    /// Revert a block from the UTXO set
    bool revert_block(const Block& block);

    std::vector<BlockAcceptedCallback> on_accepted_;
    std::vector<ReorgCallback> on_reorg_;
};

} // namespace npchain::core

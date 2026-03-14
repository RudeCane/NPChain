#pragma once

#include "utils/types.hpp"
#include "core/block.hpp"
#include "crypto/hash.hpp"

namespace npchain::consensus {

using namespace npchain::core;
using namespace npchain::crypto;

// ═══════════════════════════════════════════════════════════════════
//  PROOF-OF-NP-WITNESS (PoNW) CONSENSUS ENGINE
//
//  This is the novel consensus algorithm. No existing blockchain
//  uses this mechanism. The core insight:
//
//    Finding a witness to an NP-complete problem is hard (exponential)
//    Verifying a witness is easy (polynomial / linear)
//
//  This mirrors the economic asymmetry needed for consensus:
//    Mining = expensive (find witness)
//    Validation = cheap (check witness)
//
//  Unlike hash-based PoW:
//    - Problem structure changes every block (ASIC-resistant)
//    - Quantum speedup is only quadratic (Grover), not exponential
//    - Mining work has potential scientific value
// ═══════════════════════════════════════════════════════════════════

// ─── NP Instance Definitions ───────────────────────────────────────

/// k-SAT instance: Find variable assignments satisfying all clauses
struct SATInstance {
    uint32_t num_variables;
    uint32_t num_clauses;
    uint32_t k;  // literals per clause (typically 3)

    // Each clause: vector of signed integers (positive = var, negative = NOT var)
    std::vector<std::vector<int32_t>> clauses;

    /// Verify that an assignment satisfies all clauses — O(n·k)
    [[nodiscard]] bool verify(const std::vector<bool>& assignment) const noexcept;
};

/// Subset-Sum instance: Find a subset that sums to target
struct SubsetSumInstance {
    std::vector<uint64_t> elements;
    uint64_t target;
    uint32_t required_subset_size;  // 0 = any size

    /// Verify subset sums to target — O(|subset|)
    [[nodiscard]] bool verify(const std::vector<uint32_t>& indices) const noexcept;
};

/// Graph Coloring instance: Color vertices with k colors, no adjacent same color
struct GraphColorInstance {
    uint32_t num_vertices;
    uint32_t num_colors;
    std::vector<std::pair<uint32_t, uint32_t>> edges;  // adjacency list

    /// Verify coloring is valid — O(|E|)
    [[nodiscard]] bool verify(const std::vector<uint8_t>& colors) const noexcept;
};

/// Hamiltonian Path instance: Find a path visiting every vertex exactly once
struct HamiltonianInstance {
    uint32_t num_vertices;
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    // Adjacency set for O(1) edge lookup during verification
    // Built from edges during construction

    /// Verify the path is Hamiltonian — O(|V|)
    [[nodiscard]] bool verify(const std::vector<uint32_t>& path) const noexcept;
};

using NPInstance = std::variant<
    SATInstance, SubsetSumInstance, GraphColorInstance, HamiltonianInstance>;

// ═══════════════════════════════════════════════════════════════════
//  Instance Generator
//  Deterministically creates NP-complete instances from block hash
// ═══════════════════════════════════════════════════════════════════

class InstanceGenerator {
public:
    /// Generate an NP instance from the previous block hash + difficulty
    [[nodiscard]] static NPInstance generate(
        const Hash256& prev_block_hash,
        Difficulty difficulty
    );

    /// Select problem type from hash
    [[nodiscard]] static ProblemType select_problem_type(
        const Hash256& prev_block_hash
    ) noexcept;

    /// Encode instance for transmission
    [[nodiscard]] static ProblemParams encode_instance(
        const NPInstance& instance,
        const Hash256& prev_block_hash,
        Difficulty difficulty
    );

    /// Decode instance from params (for validators)
    [[nodiscard]] static Result<NPInstance> decode_instance(
        const ProblemParams& params
    );

private:
    // ─── Instance builders (each guaranteed to have at least one solution) ───

    [[nodiscard]] static SATInstance generate_sat(
        DeterministicRNG& rng, Difficulty difficulty);

    [[nodiscard]] static SubsetSumInstance generate_subset_sum(
        DeterministicRNG& rng, Difficulty difficulty);

    [[nodiscard]] static GraphColorInstance generate_graph_color(
        DeterministicRNG& rng, Difficulty difficulty);

    [[nodiscard]] static HamiltonianInstance generate_hamiltonian(
        DeterministicRNG& rng, Difficulty difficulty);

    /// Map difficulty to problem size parameters
    struct SizeParams {
        uint32_t primary;    // Main size (variables, vertices, elements)
        uint32_t secondary;  // Constraint size (clauses, edges, target bits)
    };

    [[nodiscard]] static SizeParams difficulty_to_size(
        ProblemType type, Difficulty difficulty) noexcept;
};

// ═══════════════════════════════════════════════════════════════════
//  Witness Verifier
//  O(n) verification — the key P vs NP asymmetry
// ═══════════════════════════════════════════════════════════════════

class WitnessVerifier {
public:
    /// Verify that a witness is a valid solution to the given instance
    /// This MUST run in polynomial time (linear in practice)
    [[nodiscard]] static bool verify(
        const NPInstance& instance,
        const Witness& witness
    );

    /// Verify a complete block's PoNW fields
    [[nodiscard]] static Block::ValidationResult verify_block_ponw(
        const Block& block,
        const Hash256& prev_block_hash,
        Difficulty expected_difficulty
    );
};

// ═══════════════════════════════════════════════════════════════════
//  Difficulty Adjustment
// ═══════════════════════════════════════════════════════════════════

class DifficultyAdjuster {
public:
    /// Calculate the next difficulty based on recent block times
    [[nodiscard]] static Difficulty calculate_next_difficulty(
        Difficulty current_difficulty,
        const std::vector<uint64_t>& recent_block_timestamps,  // last DIFFICULTY_WINDOW
        uint64_t target_block_time = TARGET_BLOCK_TIME_SEC
    ) noexcept;

    /// Get the difficulty for the genesis block
    [[nodiscard]] static Difficulty genesis_difficulty() noexcept;
};

// ═══════════════════════════════════════════════════════════════════
//  VDF Engine (Verifiable Delay Function)
//
//  After finding a witness, miner must compute VDF to prove elapsed time.
//  This prevents "instant" block withholding attacks.
// ═══════════════════════════════════════════════════════════════════

class VDFEngine {
public:
    /// Compute VDF: repeated squaring in RSA group
    /// Input: witness hash. Output: VDF result + proof.
    /// This is intentionally NON-parallelizable.
    [[nodiscard]] static VDFProof compute(
        const Hash256& input,
        uint64_t iterations
    );

    /// Verify VDF proof in O(log T) time (Wesolowski scheme)
    [[nodiscard]] static bool verify(
        const Hash256& input,
        const VDFProof& proof
    ) noexcept;

    /// Calculate required VDF iterations for current difficulty
    [[nodiscard]] static uint64_t required_iterations(
        Difficulty difficulty
    ) noexcept;
};

// ═══════════════════════════════════════════════════════════════════
//  Full Consensus Orchestrator
// ═══════════════════════════════════════════════════════════════════

class ConsensusEngine {
public:
    ConsensusEngine() = default;

    /// Validate a candidate block (full pipeline)
    [[nodiscard]] Block::ValidationResult validate_block(
        const Block& block,
        const Hash256& prev_block_hash,
        Difficulty expected_difficulty,
        BlockHeight expected_height
    ) const;

    /// Check if a block achieves finality (has threshold signatures)
    [[nodiscard]] bool check_finality(
        const Block& block,
        const std::vector<std::array<uint8_t, DILITHIUM_PK_SIZE>>& committee_pubkeys
    ) const;

    /// Select next validator committee based on stake + PoNW history
    struct ValidatorInfo {
        std::array<uint8_t, DILITHIUM_PK_SIZE> pubkey;
        Amount   stake;
        uint64_t recent_ponw_blocks;  // blocks mined in last epoch
    };

    [[nodiscard]] std::vector<uint32_t> select_committee(
        const std::vector<ValidatorInfo>& candidates,
        const Hash256& epoch_seed
    ) const;

private:
    /// Verify all transactions in a block
    [[nodiscard]] bool verify_transactions(
        const Block& block
    ) const;

    /// Verify the Merkle roots match
    [[nodiscard]] bool verify_merkle_roots(
        const Block& block
    ) const;
};

} // namespace npchain::consensus

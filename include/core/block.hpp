#pragma once

#include "utils/types.hpp"
#include "core/transaction.hpp"
#include "crypto/dilithium.hpp"

namespace npchain::core {

// ═══════════════════════════════════════════════════════════════════
//  NP Problem Instance Parameters
//  Generated deterministically from previous block hash
// ═══════════════════════════════════════════════════════════════════

struct ProblemParams {
    ProblemType type;
    uint32_t    size;           // Primary size parameter (vars, vertices, set size)
    uint32_t    secondary;      // Secondary param (clauses, edges, target bits)
    Hash256     instance_seed;  // Deterministic seed for full instance generation
    Bytes       encoded_instance; // Full encoded problem instance

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<ProblemParams> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  Witness (the miner's solution to the NP-complete problem)
// ═══════════════════════════════════════════════════════════════════

struct Witness {
    ProblemType type;
    Bytes       solution_data;  // Encoded witness/solution

    // Type-specific accessors
    struct SATWitness {
        std::vector<bool> variable_assignments;  // True/false for each variable
    };

    struct SubsetSumWitness {
        std::vector<uint32_t> selected_indices;  // Which elements are included
    };

    struct GraphColorWitness {
        std::vector<uint8_t> vertex_colors;      // Color assignment per vertex
    };

    struct HamiltonianWitness {
        std::vector<uint32_t> path;              // Vertex ordering
    };

    using DecodedWitness = std::variant<
        SATWitness, SubsetSumWitness, GraphColorWitness, HamiltonianWitness>;

    [[nodiscard]] Result<DecodedWitness> decode() const;
    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<Witness> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  VDF Proof (Verifiable Delay Function for temporal ordering)
// ═══════════════════════════════════════════════════════════════════

struct VDFProof {
    Hash256  output;          // VDF output (result of T sequential squarings)
    Bytes    proof;           // Wesolowski/Pietrzak proof (O(log T) verification)
    uint64_t iterations;      // T = number of sequential steps

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<VDFProof> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  Threshold Signature (t-of-n validator committee)
// ═══════════════════════════════════════════════════════════════════

struct ThresholdSignatureSet {
    uint32_t epoch;                                      // Committee epoch
    std::vector<uint32_t> signer_indices;                // Which validators signed
    std::vector<DilithiumSignature> partial_signatures;  // Their signatures

    [[nodiscard]] bool has_quorum() const noexcept {
        return signer_indices.size() >= THRESHOLD_QUORUM;
    }

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<ThresholdSignatureSet> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  Block Header
// ═══════════════════════════════════════════════════════════════════

struct BlockHeader {
    uint32_t    version         = PROTOCOL_VERSION;
    Hash256     prev_block_hash = {};
    Hash256     merkle_root     = {};     // Merkle root of transactions
    Hash256     state_root      = {};     // Merkle root of UTXO set / state
    Hash256     nullifier_root  = {};     // Merkle root of spent nullifiers
    uint64_t    timestamp       = 0;
    BlockHeight height          = 0;
    Difficulty  difficulty      = 0;

    // ─── PoNW-specific fields ───
    ProblemParams problem;                // The NP instance to solve
    Hash256       witness_hash  = {};     // SHA3-256 of witness data

    // ─── VDF fields ───
    VDFProof    vdf_proof;

    // ─── Miner identity ───
    std::array<uint8_t, DILITHIUM_PK_SIZE> miner_pubkey{};
    DilithiumSignature                     miner_signature;

    [[nodiscard]] Hash256 hash() const;           // SHA3-256 of header (excl. signatures)
    [[nodiscard]] Hash256 full_hash() const;      // SHA3-256 of everything
    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<BlockHeader> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  Full Block
// ═══════════════════════════════════════════════════════════════════

struct Block {
    BlockHeader                  header;
    CoinbaseTx                   coinbase;
    std::vector<Transaction>     transactions;
    Witness                      witness;           // The NP-complete solution
    ThresholdSignatureSet        finality_sigs;     // Validator committee attestation

    // ─── Core operations ───
    [[nodiscard]] Hash256 hash() const { return header.full_hash(); }
    [[nodiscard]] size_t  size_bytes() const;
    [[nodiscard]] Bytes   serialize() const;
    [[nodiscard]] static Result<Block> deserialize(ByteSpan data);

    // ─── Comprehensive validation ───
    struct ValidationResult {
        bool valid = false;
        std::string error;

        enum class Code {
            OK,
            INVALID_HEADER,
            INVALID_WITNESS,
            INVALID_VDF,
            INVALID_MERKLE_ROOT,
            INVALID_TRANSACTIONS,
            INVALID_COINBASE,
            INVALID_SIGNATURE,
            INVALID_THRESHOLD_SIGS,
            BLOCK_TOO_LARGE,
            TIMESTAMP_DRIFT,
            DIFFICULTY_MISMATCH,
        };
        Code code = Code::OK;
    };

    [[nodiscard]] ValidationResult validate(
        const Hash256& expected_prev_hash,
        Difficulty expected_difficulty,
        BlockHeight expected_height
    ) const;
};

} // namespace npchain::core

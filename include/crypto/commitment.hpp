#pragma once

#include "utils/types.hpp"

namespace npchain::crypto {

// ═══════════════════════════════════════════════════════════════════
//  Lattice-Based Pedersen Commitment
//
//  Commit(v, r) = v·G + r·H  (over lattice group)
//  - Perfectly hiding: commitment reveals nothing about v
//  - Computationally binding: cannot open to different v
//  - Homomorphic: Commit(a) + Commit(b) = Commit(a+b)
// ═══════════════════════════════════════════════════════════════════

struct Commitment {
    Hash256 data{};  // Compressed commitment point

    [[nodiscard]] bool operator==(const Commitment& other) const noexcept = default;
};

struct BlindingFactor {
    SecureArray<HASH_SIZE> data;
};

/// Create a Pedersen commitment to an amount
[[nodiscard]] Commitment pedersen_commit(Amount value, const BlindingFactor& blinding);

/// Generate a random blinding factor
[[nodiscard]] BlindingFactor random_blinding();

/// Verify that commitments balance: sum(inputs) - sum(outputs) - fee_commit == 0
[[nodiscard]] bool verify_commitment_balance(
    const std::vector<Commitment>& inputs,
    const std::vector<Commitment>& outputs,
    const Commitment& fee_commitment
) noexcept;

// ─── Range Proofs (Bulletproof-analog over lattices) ───────────────

struct RangeProof {
    Bytes proof_data;  // Variable-length proof

    /// Proof that committed value is in [0, 2^64)
    static constexpr uint32_t RANGE_BITS = 64;
};

/// Generate a range proof that the committed value is non-negative
[[nodiscard]] Result<RangeProof> create_range_proof(
    Amount value,
    const BlindingFactor& blinding,
    const Commitment& commitment
);

/// Verify a range proof
[[nodiscard]] bool verify_range_proof(
    const Commitment& commitment,
    const RangeProof& proof
) noexcept;

/// Batch-verify multiple range proofs (more efficient)
[[nodiscard]] bool batch_verify_range_proofs(
    const std::vector<std::pair<Commitment, RangeProof>>& proofs
) noexcept;

} // namespace npchain::crypto

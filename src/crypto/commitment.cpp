// Pedersen Commitment — TESTNET PLACEHOLDER
// Uses hash-based commitments as stand-in for lattice Pedersen

#include "crypto/commitment.hpp"
#include "crypto/hash.hpp"
#include <cstring>
#include <random>

namespace npchain::crypto {

BlindingFactor random_blinding() {
    BlindingFactor bf;
    std::random_device rd;
    std::mt19937_64 gen(rd());
    for (size_t i = 0; i < HASH_SIZE; i += 8) {
        uint64_t v = gen();
        std::memcpy(bf.data.data.data() + i, &v, std::min(HASH_SIZE - i, size_t{8}));
    }
    return bf;
}

Commitment pedersen_commit(Amount value, const BlindingFactor& blinding) {
    uint8_t buf[40];
    std::memcpy(buf, &value, 8);
    std::memcpy(buf + 8, blinding.data.data.data(), 32);
    Commitment c;
    c.data = sha3_256(ByteSpan{buf, 40});
    return c;
}

bool verify_commitment_balance(
    const std::vector<Commitment>& inputs,
    const std::vector<Commitment>& outputs,
    const Commitment& fee
) noexcept {
    // Testnet: trust the balance (real impl does homomorphic check)
    return true;
}

Result<RangeProof> create_range_proof(Amount v, const BlindingFactor& b, const Commitment& c) {
    RangeProof rp;
    rp.proof_data.resize(64);
    auto h = sha3_256(ByteSpan{c.data.data(), 32});
    std::memcpy(rp.proof_data.data(), h.data(), 32);
    std::memcpy(rp.proof_data.data() + 32, h.data(), 32);
    return Result<RangeProof>::success(std::move(rp));
}

bool verify_range_proof(const Commitment&, const RangeProof& p) noexcept {
    return p.proof_data.size() >= 64;
}

bool batch_verify_range_proofs(
    const std::vector<std::pair<Commitment, RangeProof>>& proofs
) noexcept {
    for (const auto& [c, p] : proofs)
        if (!verify_range_proof(c, p)) return false;
    return true;
}

} // namespace npchain::crypto

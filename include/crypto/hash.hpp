#pragma once

#include "utils/types.hpp"
#include <string_view>

namespace npchain::crypto {

// ─── Hash Algorithms ───────────────────────────────────────────────
enum class HashAlgo : uint8_t {
    SHA3_256,
    SHAKE_256,
    BLAKE3
};

// ─── Core Hash Interface ───────────────────────────────────────────

/// Compute SHA3-256 over arbitrary data
[[nodiscard]] Hash256 sha3_256(ByteSpan data) noexcept;

/// Compute SHA3-256 over a string
[[nodiscard]] Hash256 sha3_256(std::string_view data) noexcept;

/// Double SHA3-256 for extra security (block hashing)
[[nodiscard]] Hash256 double_sha3(ByteSpan data) noexcept;

/// SHAKE-256 extendable output — used for PRNG seeding
[[nodiscard]] Bytes shake_256(ByteSpan seed, size_t output_len);

/// BLAKE3 for fast integrity checks
[[nodiscard]] Hash256 blake3(ByteSpan data) noexcept;

// ─── Merkle Tree ───────────────────────────────────────────────────

/// Build a Merkle root from a list of leaf hashes (SHA3-256 based)
[[nodiscard]] Hash256 merkle_root(const std::vector<Hash256>& leaves);

/// Compute a Merkle proof for leaf at `index`
struct MerkleProof {
    std::vector<Hash256> siblings;
    std::vector<bool>    directions;  // false = left, true = right
};

[[nodiscard]] MerkleProof merkle_proof(
    const std::vector<Hash256>& leaves, size_t index);

/// Verify a Merkle proof
[[nodiscard]] bool merkle_verify(
    const Hash256& root,
    const Hash256& leaf,
    const MerkleProof& proof) noexcept;

// ─── CSPRNG (Deterministic from seed for instance generation) ──────

class DeterministicRNG {
public:
    explicit DeterministicRNG(const Hash256& seed);
    ~DeterministicRNG();

    /// Generate next random uint64
    [[nodiscard]] uint64_t next_u64();

    /// Generate random integer in [0, bound)
    [[nodiscard]] uint64_t next_bounded(uint64_t bound);

    /// Generate random boolean with probability p (0.0 to 1.0)
    [[nodiscard]] bool next_bool(double p = 0.5);

    /// Generate n random bytes
    [[nodiscard]] Bytes next_bytes(size_t n);

private:
    Bytes    state_;
    size_t   position_ = 0;
    uint64_t counter_  = 0;

    void expand_state();
};

// ─── Utility ───────────────────────────────────────────────────────

/// Constant-time comparison (side-channel safe)
[[nodiscard]] bool constant_time_eq(ByteSpan a, ByteSpan b) noexcept;

/// Hex encode/decode
[[nodiscard]] std::string to_hex(ByteSpan data);
[[nodiscard]] Bytes from_hex(std::string_view hex);

} // namespace npchain::crypto

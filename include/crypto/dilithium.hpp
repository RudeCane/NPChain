#pragma once

#include "utils/types.hpp"

namespace npchain::crypto {

// ═══════════════════════════════════════════════════════════════════
//  CRYSTALS-Dilithium — NIST Post-Quantum Digital Signature
//  Security Level 5 (AES-256 equivalent)
//
//  Resistant to: Shor's algorithm, Grover's algorithm
//  Basis: Module-LWE (Learning With Errors) over polynomial lattices
// ═══════════════════════════════════════════════════════════════════

struct DilithiumKeypair {
    std::array<uint8_t, DILITHIUM_PK_SIZE> public_key{};
    SecureArray<DILITHIUM_SK_SIZE>         secret_key;
};

struct DilithiumSignature {
    std::array<uint8_t, DILITHIUM_SIG_SIZE> data{};
};

/// Generate a new Dilithium keypair
[[nodiscard]] Result<DilithiumKeypair> dilithium_keygen();

/// Sign a message using the secret key
[[nodiscard]] Result<DilithiumSignature> dilithium_sign(
    ByteSpan message,
    const SecureArray<DILITHIUM_SK_SIZE>& secret_key
);

/// Verify a signature against a public key
[[nodiscard]] bool dilithium_verify(
    ByteSpan message,
    const DilithiumSignature& signature,
    ByteSpan public_key
) noexcept;

/// Derive a child public key deterministically (for HD wallet)
[[nodiscard]] Result<std::array<uint8_t, DILITHIUM_PK_SIZE>> dilithium_derive_child(
    ByteSpan parent_public_key,
    const Hash256& chain_code,
    uint32_t index
);

// ─── Address Derivation ────────────────────────────────────────────

/// Derive a wallet address from a Dilithium public key
/// Address = SHAKE-256(public_key)[0:20] with version prefix + checksum
struct Address {
    static constexpr size_t SIZE = 25;  // 1 version + 20 hash + 4 checksum
    std::array<uint8_t, SIZE> data{};

    [[nodiscard]] std::string to_bech32() const;
    [[nodiscard]] static Result<Address> from_bech32(std::string_view encoded);
    [[nodiscard]] bool operator==(const Address& other) const noexcept = default;
};

[[nodiscard]] Address derive_address(ByteSpan public_key);

} // namespace npchain::crypto

#pragma once

#include "utils/types.hpp"

namespace npchain::crypto {

// ═══════════════════════════════════════════════════════════════════
//  CRYSTALS-Kyber — NIST Post-Quantum Key Encapsulation Mechanism
//  Kyber-1024 (AES-256 equivalent security)
//
//  Used for: P2P encrypted transport, stealth address derivation
//  Basis: Module-LWE over polynomial lattices
// ═══════════════════════════════════════════════════════════════════

struct KyberKeypair {
    std::array<uint8_t, KYBER_PK_SIZE> public_key{};
    SecureArray<KYBER_SK_SIZE>         secret_key;
};

struct KyberCiphertext {
    std::array<uint8_t, KYBER_CT_SIZE> data{};
};

struct KyberSharedSecret {
    SecureArray<KYBER_SS_SIZE> data;
};

/// Generate a Kyber keypair
[[nodiscard]] Result<KyberKeypair> kyber_keygen();

/// Encapsulate: sender creates (ciphertext, shared_secret) from recipient's public key
struct KyberEncapsResult {
    KyberCiphertext   ciphertext;
    KyberSharedSecret shared_secret;
};

[[nodiscard]] Result<KyberEncapsResult> kyber_encaps(
    ByteSpan recipient_public_key
);

/// Decapsulate: recipient recovers shared_secret from ciphertext + own secret key
[[nodiscard]] Result<KyberSharedSecret> kyber_decaps(
    const KyberCiphertext& ciphertext,
    const SecureArray<KYBER_SK_SIZE>& secret_key
);

} // namespace npchain::crypto

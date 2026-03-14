// CRYSTALS-Kyber — TESTNET PLACEHOLDER
// Uses SHAKE-256 key derivation as stand-in for lattice KEM

#include "crypto/kyber.hpp"
#include "crypto/hash.hpp"
#include <cstring>
#include <random>

namespace npchain::crypto {

Result<KyberKeypair> kyber_keygen() {
    KyberKeypair kp;
    std::random_device rd;
    std::mt19937_64 gen(rd());
    auto fill = [&](uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i += 8) {
            uint64_t v = gen();
            std::memcpy(buf + i, &v, std::min(len - i, size_t{8}));
        }
    };
    fill(kp.secret_key.data.data(), KYBER_SK_SIZE);
    auto pk = shake_256(ByteSpan{kp.secret_key.data.data(), KYBER_SK_SIZE}, KYBER_PK_SIZE);
    std::memcpy(kp.public_key.data(), pk.data(), KYBER_PK_SIZE);
    return Result<KyberKeypair>::success(std::move(kp));
}

Result<KyberEncapsResult> kyber_encaps(ByteSpan recipient_public_key) {
    KyberEncapsResult r;
    auto combined = shake_256(recipient_public_key, KYBER_CT_SIZE + KYBER_SS_SIZE);
    std::memcpy(r.ciphertext.data.data(), combined.data(), KYBER_CT_SIZE);
    std::memcpy(r.shared_secret.data.data.data(), combined.data() + KYBER_CT_SIZE, KYBER_SS_SIZE);
    return Result<KyberEncapsResult>::success(std::move(r));
}

Result<KyberSharedSecret> kyber_decaps(
    const KyberCiphertext& ct, const SecureArray<KYBER_SK_SIZE>& sk
) {
    Bytes input(KYBER_CT_SIZE + KYBER_SK_SIZE);
    std::memcpy(input.data(), ct.data.data(), KYBER_CT_SIZE);
    std::memcpy(input.data() + KYBER_CT_SIZE, sk.data.data(), KYBER_SK_SIZE);
    auto ss = shake_256(ByteSpan{input.data(), input.size()}, KYBER_SS_SIZE);
    KyberSharedSecret result;
    std::memcpy(result.data.data.data(), ss.data(), KYBER_SS_SIZE);
    secure_zero(input.data(), input.size());
    return Result<KyberSharedSecret>::success(std::move(result));
}

} // namespace npchain::crypto

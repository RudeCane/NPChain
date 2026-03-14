// SHA3-256 / SHAKE-256 — Pure C++ Keccak Sponge Implementation
// Zero external dependencies. FIPS 202 compliant.

#include "crypto/hash.hpp"
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace npchain::crypto {

namespace {

constexpr uint64_t KECCAK_RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808AULL,
    0x8000000080008000ULL, 0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008AULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL,
};

constexpr int KECCAK_ROT[5][5] = {
    { 0,  1, 62, 28, 27}, {36, 44,  6, 55, 20}, { 3, 10, 43, 25, 39},
    {41, 45, 15, 21,  8}, {18,  2, 61, 56, 14},
};

constexpr uint64_t rotl64(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

void keccak_f1600(uint64_t state[25]) {
    for (int round = 0; round < 24; ++round) {
        uint64_t C[5], D[5], B[25];
        for (int x = 0; x < 5; ++x)
            C[x] = state[x] ^ state[x+5] ^ state[x+10] ^ state[x+15] ^ state[x+20];
        for (int x = 0; x < 5; ++x)
            D[x] = C[(x+4)%5] ^ rotl64(C[(x+1)%5], 1);
        for (int i = 0; i < 25; ++i)
            state[i] ^= D[i % 5];
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 5; ++y)
                B[y + 5*((2*x + 3*y) % 5)] = rotl64(state[x + 5*y], KECCAK_ROT[y][x]);
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 5; ++y)
                state[x + 5*y] = B[x + 5*y] ^ ((~B[(x+1)%5 + 5*y]) & B[(x+2)%5 + 5*y]);
        state[0] ^= KECCAK_RC[round];
    }
}

class KeccakSponge {
public:
    KeccakSponge(size_t rate_bytes, uint8_t suffix)
        : rate_(rate_bytes), suffix_(suffix) {
        std::memset(state_, 0, sizeof(state_));
        buf_pos_ = 0;
    }

    void absorb(const uint8_t* data, size_t len) {
        auto* sb = reinterpret_cast<uint8_t*>(state_);
        for (size_t i = 0; i < len; ++i) {
            sb[buf_pos_++] ^= data[i];
            if (buf_pos_ == rate_) { keccak_f1600(state_); buf_pos_ = 0; }
        }
    }

    void finalize() {
        auto* sb = reinterpret_cast<uint8_t*>(state_);
        sb[buf_pos_] ^= suffix_;
        sb[rate_ - 1] ^= 0x80;
        keccak_f1600(state_);
        buf_pos_ = 0;
        finalized_ = true;
    }

    void squeeze(uint8_t* out, size_t len) {
        if (!finalized_) finalize();
        auto* sb = reinterpret_cast<uint8_t*>(state_);
        size_t off = 0;
        while (off < len) {
            if (buf_pos_ == rate_) { keccak_f1600(state_); buf_pos_ = 0; }
            size_t chunk = std::min(len - off, rate_ - buf_pos_);
            std::memcpy(out + off, sb + buf_pos_, chunk);
            buf_pos_ += chunk;
            off += chunk;
        }
    }

private:
    uint64_t state_[25];
    size_t rate_, buf_pos_;
    uint8_t suffix_;
    bool finalized_ = false;
};

} // anon namespace

// ─── Public API ────────────────────────────────────────────────────

Hash256 sha3_256(ByteSpan data) noexcept {
    KeccakSponge sponge(136, 0x06);
    sponge.absorb(data.data(), data.size());
    Hash256 r{};
    sponge.squeeze(r.data(), 32);
    return r;
}

Hash256 sha3_256(std::string_view data) noexcept {
    return sha3_256(ByteSpan{reinterpret_cast<const uint8_t*>(data.data()), data.size()});
}

Hash256 double_sha3(ByteSpan data) noexcept {
    Hash256 first = sha3_256(data);
    return sha3_256(ByteSpan{first.data(), first.size()});
}

Bytes shake_256(ByteSpan seed, size_t output_len) {
    KeccakSponge sponge(136, 0x1F);
    sponge.absorb(seed.data(), seed.size());
    Bytes r(output_len);
    sponge.squeeze(r.data(), output_len);
    return r;
}

Hash256 blake3(ByteSpan data) noexcept {
    // Domain-separated SHA3 standing in for BLAKE3 on testnet
    uint8_t prefix = 0xB3;
    KeccakSponge sponge(136, 0x06);
    sponge.absorb(&prefix, 1);
    sponge.absorb(data.data(), data.size());
    Hash256 r{};
    sponge.squeeze(r.data(), 32);
    return r;
}

// ─── Merkle Tree ───────────────────────────────────────────────────

Hash256 merkle_root(const std::vector<Hash256>& leaves) {
    if (leaves.empty()) return Hash256{};
    if (leaves.size() == 1) return leaves[0];

    std::vector<Hash256> cur = leaves;
    while (cur.size() > 1) {
        std::vector<Hash256> next;
        for (size_t i = 0; i < cur.size(); i += 2) {
            uint8_t combined[64];
            std::memcpy(combined, cur[i].data(), 32);
            size_t j = (i + 1 < cur.size()) ? i + 1 : i;
            std::memcpy(combined + 32, cur[j].data(), 32);
            next.push_back(sha3_256(ByteSpan{combined, 64}));
        }
        cur = std::move(next);
    }
    return cur[0];
}

MerkleProof merkle_proof(const std::vector<Hash256>& leaves, size_t index) {
    MerkleProof proof;
    if (leaves.empty() || index >= leaves.size()) return proof;
    std::vector<Hash256> cur = leaves;
    size_t idx = index;
    while (cur.size() > 1) {
        std::vector<Hash256> next;
        for (size_t i = 0; i < cur.size(); i += 2) {
            size_t j = (i + 1 < cur.size()) ? i + 1 : i;
            if (i == idx || j == idx) {
                size_t sib = (i == idx) ? j : i;
                proof.siblings.push_back(cur[sib]);
                proof.directions.push_back(i != idx);
            }
            uint8_t combined[64];
            std::memcpy(combined, cur[i].data(), 32);
            std::memcpy(combined + 32, cur[j].data(), 32);
            next.push_back(sha3_256(ByteSpan{combined, 64}));
        }
        idx /= 2;
        cur = std::move(next);
    }
    return proof;
}

bool merkle_verify(const Hash256& root, const Hash256& leaf, const MerkleProof& proof) noexcept {
    Hash256 cur = leaf;
    for (size_t i = 0; i < proof.siblings.size(); ++i) {
        uint8_t combined[64];
        if (proof.directions[i]) {
            std::memcpy(combined, proof.siblings[i].data(), 32);
            std::memcpy(combined + 32, cur.data(), 32);
        } else {
            std::memcpy(combined, cur.data(), 32);
            std::memcpy(combined + 32, proof.siblings[i].data(), 32);
        }
        cur = sha3_256(ByteSpan{combined, 64});
    }
    return cur == root;
}

// ─── Deterministic RNG ─────────────────────────────────────────────

DeterministicRNG::DeterministicRNG(const Hash256& seed) {
    state_ = shake_256(ByteSpan{seed.data(), seed.size()}, 1024);
    position_ = 0;
    counter_ = 0;
}

DeterministicRNG::~DeterministicRNG() { secure_zero(state_.data(), state_.size()); }

void DeterministicRNG::expand_state() {
    ++counter_;
    Bytes input(8 + state_.size());
    std::memcpy(input.data(), &counter_, 8);
    std::memcpy(input.data() + 8, state_.data(), state_.size());
    state_ = shake_256(ByteSpan{input.data(), input.size()}, 1024);
    position_ = 0;
}

uint64_t DeterministicRNG::next_u64() {
    if (position_ + 8 > state_.size()) expand_state();
    uint64_t r;
    std::memcpy(&r, state_.data() + position_, 8);
    position_ += 8;
    return r;
}

uint64_t DeterministicRNG::next_bounded(uint64_t bound) {
    if (bound == 0) return 0;
    uint64_t threshold = (~bound + 1) % bound;
    while (true) { uint64_t r = next_u64(); if (r >= threshold) return r % bound; }
}

bool DeterministicRNG::next_bool(double p) {
    return static_cast<double>(next_u64()) / static_cast<double>(UINT64_MAX) < p;
}

Bytes DeterministicRNG::next_bytes(size_t n) {
    Bytes r(n);
    for (size_t i = 0; i < n; ++i) {
        if (position_ >= state_.size()) expand_state();
        r[i] = state_[position_++];
    }
    return r;
}

// ─── Utility ───────────────────────────────────────────────────────

bool constant_time_eq(ByteSpan a, ByteSpan b) noexcept {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

std::string to_hex(ByteSpan data) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < data.size(); ++i) ss << std::setw(2) << static_cast<int>(data[i]);
    return ss.str();
}

Bytes from_hex(std::string_view hex) {
    Bytes r;
    r.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        uint8_t b = 0;
        for (int j = 0; j < 2; ++j) {
            char c = hex[i+j]; b <<= 4;
            if (c >= '0' && c <= '9') b |= (c - '0');
            else if (c >= 'a' && c <= 'f') b |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') b |= (c - 'A' + 10);
        }
        r.push_back(b);
    }
    return r;
}

} // namespace npchain::crypto

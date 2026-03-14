#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <chrono>
#include <optional>
#include <memory>
#include <variant>

namespace npchain {

// ─── Fundamental Constants ─────────────────────────────────────────
constexpr uint32_t CHAIN_ID              = 0x435254;  // "CRT" (Certs)
constexpr uint32_t PROTOCOL_VERSION      = 1;
constexpr uint64_t TARGET_BLOCK_TIME_SEC = 60;
constexpr uint64_t DIFFICULTY_WINDOW     = 144;
constexpr double   MAX_DIFFICULTY_ADJ    = 1.25;
// ─── Unlimited Supply / Annual Cap Emission Model ──────────────
// No hard supply cap. Fixed Certs emitted per year. Inflation rate
// declines asymptotically (supply grows, emission stays constant).
constexpr uint64_t BLOCKS_PER_YEAR       = 525'600;           // 60s blocks × 525,600 min/year
constexpr uint64_t BASE_UNITS_PER_CERT   = 1'000'000ULL;     // 6 decimal places (like USDC)
constexpr uint64_t ANNUAL_EMISSION_CAP   = 100'000'000'000ULL * BASE_UNITS_PER_CERT; // 100B Certs/year (base units)
constexpr uint64_t BLOCK_REWARD          = ANNUAL_EMISSION_CAP / BLOCKS_PER_YEAR; // ≈190,258 Certs/block
constexpr uint64_t COMPLEXITY_FRAGS      = ANNUAL_EMISSION_CAP % BLOCKS_PER_YEAR; // Complexity Frags: remainder units distributed to final blocks
constexpr uint32_t MAX_BLOCK_SIZE        = 4'194'304;        // 4 MB
constexpr uint32_t THRESHOLD_COMMITTEE   = 21;
constexpr uint32_t THRESHOLD_QUORUM      = 14;
constexpr uint32_t CHECKPOINT_INTERVAL   = 1000;
constexpr uint32_t MIN_PEER_SUBNETS      = 4;

// ─── Cryptographic Sizes (Dilithium Level 5 + Kyber-1024) ──────────
constexpr size_t HASH_SIZE               = 32;   // SHA3-256 / SHAKE-256
constexpr size_t DILITHIUM_PK_SIZE       = 2592; // Level 5
constexpr size_t DILITHIUM_SK_SIZE       = 4896;
constexpr size_t DILITHIUM_SIG_SIZE      = 4595;
constexpr size_t KYBER_PK_SIZE           = 1568; // Kyber-1024
constexpr size_t KYBER_SK_SIZE           = 3168;
constexpr size_t KYBER_CT_SIZE           = 1568;
constexpr size_t KYBER_SS_SIZE           = 32;   // Shared secret

// ─── Type Aliases ──────────────────────────────────────────────────
using Hash256       = std::array<uint8_t, HASH_SIZE>;
using Bytes         = std::vector<uint8_t>;
using ByteSpan      = std::span<const uint8_t>;
using Timestamp     = std::chrono::system_clock::time_point;
using BlockHeight   = uint64_t;
using Difficulty     = uint64_t;
using Amount        = uint64_t;  // Base unit (1 Cert = 10^9 base units)

// ─── NP Problem Types ──────────────────────────────────────────────
enum class ProblemType : uint8_t {
    K_SAT           = 0,
    SUBSET_SUM      = 1,
    GRAPH_COLORING  = 2,
    HAMILTONIAN_PATH = 3,
    COUNT           = 4
};

[[nodiscard]] constexpr const char* problem_type_name(ProblemType t) noexcept {
    switch (t) {
        case ProblemType::K_SAT:            return "k-SAT";
        case ProblemType::SUBSET_SUM:       return "Subset-Sum";
        case ProblemType::GRAPH_COLORING:   return "Graph-Coloring";
        case ProblemType::HAMILTONIAN_PATH: return "Hamiltonian-Path";
        default:                            return "Unknown";
    }
}

// ─── Result Type ───────────────────────────────────────────────────
template <typename T>
struct Result {
    std::optional<T> value;
    std::string      error;

    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
    [[nodiscard]] const T& get() const     { return value.value(); }

    static Result success(T v) { return {std::move(v), {}}; }
    static Result failure(std::string e) { return {std::nullopt, std::move(e)}; }
};

// ─── Secure Memory Wipe ────────────────────────────────────────────
inline void secure_zero(void* ptr, size_t len) noexcept {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (len--) *p++ = 0;
}

// ─── RAII Secure Buffer ────────────────────────────────────────────
template <size_t N>
struct SecureArray {
    std::array<uint8_t, N> data{};
    ~SecureArray() { secure_zero(data.data(), N); }

    SecureArray() = default;
    SecureArray(const SecureArray&) = delete;
    SecureArray& operator=(const SecureArray&) = delete;
    SecureArray(SecureArray&& o) noexcept : data(o.data) { secure_zero(o.data.data(), N); }
    SecureArray& operator=(SecureArray&& o) noexcept {
        if (this != &o) {
            secure_zero(data.data(), N);
            data = o.data;
            secure_zero(o.data.data(), N);
        }
        return *this;
    }

    [[nodiscard]] uint8_t* begin() noexcept { return data.data(); }
    [[nodiscard]] const uint8_t* begin() const noexcept { return data.data(); }
    [[nodiscard]] size_t size() const noexcept { return N; }
};

} // namespace npchain

#pragma once

#include "utils/types.hpp"
#include <unordered_map>
#include <functional>
#include <shared_mutex>
#include <memory>

namespace npchain::crypto {

// ═══════════════════════════════════════════════════════════════════
//  CRYPTOGRAPHIC AGILITY FRAMEWORK
//
//  Problem: Cryptographic algorithms have shelf lives. MD5 was
//  secure in 1992, broken by 2004. SHA-1 was secure in 1995,
//  broken by 2017. RSA-2048 is secure today, dead when quantum
//  computers scale. Any blockchain that hardcodes its crypto
//  is building on a foundation with an expiration date.
//
//  Solution: Every cryptographic operation in NPChain goes through
//  an abstraction layer (CryptoRegistry) that maps algorithm IDs
//  to implementations. Upgrading crypto means:
//    1. NIST/industry publishes new standard
//    2. Community implements the new algorithm as a CryptoProvider
//    3. Governance proposal activates it at a future block height
//    4. New transactions use the new algorithm
//    5. Old transactions remain valid (backward compatibility)
//    6. Migration window gives users time to move funds
//
//  Every signature, hash, key, and proof on-chain is tagged with
//  its algorithm version ID, so the chain can verify both old and
//  new crypto forever.
// ═══════════════════════════════════════════════════════════════════

// ─── Algorithm Categories ──────────────────────────────────────────

enum class CryptoCategory : uint8_t {
    SIGNATURE       = 0x01,  // Digital signatures (Dilithium, future: FALCON, SQIsign)
    KEM             = 0x02,  // Key encapsulation (Kyber, future: BIKE, HQC)
    HASH            = 0x03,  // Hash functions (SHA3-256, BLAKE3, future: ?)
    COMMITMENT      = 0x04,  // Commitment schemes (Pedersen-lattice)
    ZK_PROOF        = 0x05,  // Zero-knowledge proof systems
    VDF             = 0x06,  // Verifiable delay functions
    SYMMETRIC       = 0x07,  // Symmetric encryption (AES-256-GCM, ChaCha20)
    RNG             = 0x08,  // PRNG for instance generation
};

// ─── Algorithm Version IDs ─────────────────────────────────────────
// Each algorithm gets a unique 16-bit ID. The high byte is the
// category, the low byte is the version within that category.
// These IDs are stored ON-CHAIN in every transaction and block
// header, so validators know which algorithm to use for verification.

struct AlgorithmID {
    uint16_t id;

    [[nodiscard]] CryptoCategory category() const noexcept {
        return static_cast<CryptoCategory>(id >> 8);
    }

    [[nodiscard]] uint8_t version() const noexcept {
        return static_cast<uint8_t>(id & 0xFF);
    }

    static constexpr AlgorithmID make(CryptoCategory cat, uint8_t ver) {
        return {static_cast<uint16_t>((static_cast<uint8_t>(cat) << 8) | ver)};
    }

    bool operator==(const AlgorithmID& other) const noexcept = default;
};

// ─── Predefined Algorithm IDs (Genesis Set) ────────────────────────

namespace algorithms {
    // Signatures
    constexpr AlgorithmID DILITHIUM_5     = AlgorithmID::make(CryptoCategory::SIGNATURE, 0x01);
    constexpr AlgorithmID FALCON_1024     = AlgorithmID::make(CryptoCategory::SIGNATURE, 0x02);  // Reserved
    constexpr AlgorithmID SPHINCS_256F    = AlgorithmID::make(CryptoCategory::SIGNATURE, 0x03);  // Reserved: hash-based
    // Future: SQIsign, MAYO, etc. get 0x04, 0x05...

    // Key Encapsulation
    constexpr AlgorithmID KYBER_1024      = AlgorithmID::make(CryptoCategory::KEM, 0x01);
    constexpr AlgorithmID HQC_256         = AlgorithmID::make(CryptoCategory::KEM, 0x02);        // Reserved
    constexpr AlgorithmID BIKE_L5         = AlgorithmID::make(CryptoCategory::KEM, 0x03);        // Reserved

    // Hashing
    constexpr AlgorithmID SHA3_256        = AlgorithmID::make(CryptoCategory::HASH, 0x01);
    constexpr AlgorithmID BLAKE3          = AlgorithmID::make(CryptoCategory::HASH, 0x02);
    constexpr AlgorithmID SHAKE_256       = AlgorithmID::make(CryptoCategory::HASH, 0x03);

    // Commitments
    constexpr AlgorithmID PEDERSEN_LATTICE = AlgorithmID::make(CryptoCategory::COMMITMENT, 0x01);

    // ZK Proofs
    constexpr AlgorithmID ZK_LATTICE_SIGMA = AlgorithmID::make(CryptoCategory::ZK_PROOF, 0x01);

    // Symmetric
    constexpr AlgorithmID AES_256_GCM      = AlgorithmID::make(CryptoCategory::SYMMETRIC, 0x01);
    constexpr AlgorithmID CHACHA20_POLY    = AlgorithmID::make(CryptoCategory::SYMMETRIC, 0x02);
}

// ─── Algorithm Lifecycle Status ────────────────────────────────────

enum class AlgorithmStatus : uint8_t {
    RESERVED    = 0,   // ID allocated, implementation not yet available
    PROPOSED    = 1,   // Implementation submitted, under review
    APPROVED    = 2,   // Governance approved, waiting for activation height
    ACTIVE      = 3,   // Currently accepted for new transactions
    PREFERRED   = 4,   // Recommended for new transactions (latest standard)
    DEPRECATED  = 5,   // Still valid for verification, not for new transactions
    SUNSET      = 6,   // Migration deadline approaching — wallets must move funds
    RETIRED     = 7,   // No longer accepted even for verification (emergency only)
};

// ─── Algorithm Metadata ────────────────────────────────────────────

struct AlgorithmInfo {
    AlgorithmID     id;
    std::string     name;               // "CRYSTALS-Dilithium Level 5"
    std::string     standard;           // "NIST FIPS 204"
    AlgorithmStatus status;
    BlockHeight     activation_height;  // Block at which this became ACTIVE
    BlockHeight     deprecation_height; // Block at which this becomes DEPRECATED (0 = never)
    BlockHeight     sunset_height;      // Block at which this enters SUNSET (0 = never)
    BlockHeight     retirement_height;  // Block at which this is RETIRED (0 = never)

    // Size information (for validation)
    size_t public_key_size;
    size_t secret_key_size;
    size_t signature_size;   // or ciphertext_size for KEMs
    size_t output_size;      // hash output size, shared secret size, etc.

    // Security metadata
    uint32_t classical_security_bits;   // e.g., 256
    uint32_t quantum_security_bits;     // e.g., 128 (post-Grover)
    std::string security_basis;         // "Module-LWE", "SHA3-Keccak", etc.
};

// ═══════════════════════════════════════════════════════════════════
//  Crypto Provider Interface
//
//  Every cryptographic algorithm implements this interface.
//  New algorithms are plugged in by implementing a provider and
//  registering it with the CryptoRegistry.
// ═══════════════════════════════════════════════════════════════════

class ISignatureProvider {
public:
    virtual ~ISignatureProvider() = default;
    [[nodiscard]] virtual AlgorithmID algorithm_id() const noexcept = 0;
    [[nodiscard]] virtual Result<Bytes> generate_keypair() = 0;      // Returns serialized (pk, sk)
    [[nodiscard]] virtual Result<Bytes> sign(ByteSpan message, ByteSpan secret_key) = 0;
    [[nodiscard]] virtual bool verify(ByteSpan message, ByteSpan signature, ByteSpan public_key) noexcept = 0;
    [[nodiscard]] virtual const AlgorithmInfo& info() const noexcept = 0;
};

class IKEMProvider {
public:
    virtual ~IKEMProvider() = default;
    [[nodiscard]] virtual AlgorithmID algorithm_id() const noexcept = 0;
    [[nodiscard]] virtual Result<Bytes> generate_keypair() = 0;
    [[nodiscard]] virtual Result<std::pair<Bytes, Bytes>> encapsulate(ByteSpan public_key) = 0; // (ciphertext, shared_secret)
    [[nodiscard]] virtual Result<Bytes> decapsulate(ByteSpan ciphertext, ByteSpan secret_key) = 0; // shared_secret
    [[nodiscard]] virtual const AlgorithmInfo& info() const noexcept = 0;
};

class IHashProvider {
public:
    virtual ~IHashProvider() = default;
    [[nodiscard]] virtual AlgorithmID algorithm_id() const noexcept = 0;
    [[nodiscard]] virtual Hash256 hash(ByteSpan data) noexcept = 0;
    [[nodiscard]] virtual Bytes hash_xof(ByteSpan data, size_t output_len) = 0;  // Extendable output
    [[nodiscard]] virtual const AlgorithmInfo& info() const noexcept = 0;
};

// ═══════════════════════════════════════════════════════════════════
//  Crypto Registry — The Central Switchboard
//
//  All cryptographic operations go through here. The registry
//  maps algorithm IDs to provider implementations and tracks
//  the lifecycle status of each algorithm.
//
//  When the chain needs to verify a signature, it reads the
//  algorithm ID from the transaction, looks up the provider,
//  and delegates. This means blocks from 2025 using Dilithium
//  and blocks from 2035 using some future algorithm both verify
//  correctly through the same pipeline.
// ═══════════════════════════════════════════════════════════════════

class CryptoRegistry {
public:
    static CryptoRegistry& instance();

    // ─── Provider Registration ───

    void register_signature_provider(std::unique_ptr<ISignatureProvider> provider);
    void register_kem_provider(std::unique_ptr<IKEMProvider> provider);
    void register_hash_provider(std::unique_ptr<IHashProvider> provider);

    // ─── Provider Lookup ───

    [[nodiscard]] ISignatureProvider* get_signature_provider(AlgorithmID id) const;
    [[nodiscard]] IKEMProvider* get_kem_provider(AlgorithmID id) const;
    [[nodiscard]] IHashProvider* get_hash_provider(AlgorithmID id) const;

    // ─── Preferred Algorithm (what new transactions should use) ───

    [[nodiscard]] AlgorithmID preferred_signature() const noexcept;
    [[nodiscard]] AlgorithmID preferred_kem() const noexcept;
    [[nodiscard]] AlgorithmID preferred_hash() const noexcept;

    // ─── Lifecycle Management ───

    /// Check if an algorithm is valid for NEW transactions at a given height
    [[nodiscard]] bool is_active_for_new(AlgorithmID id, BlockHeight height) const;

    /// Check if an algorithm is valid for VERIFICATION at a given height
    /// (this stays true longer than is_active_for_new — backward compat)
    [[nodiscard]] bool is_valid_for_verification(AlgorithmID id, BlockHeight height) const;

    /// Get the status of an algorithm at a given height
    [[nodiscard]] AlgorithmStatus status_at_height(AlgorithmID id, BlockHeight height) const;

    /// Get all registered algorithms and their info
    [[nodiscard]] std::vector<AlgorithmInfo> list_algorithms() const;

    /// Get algorithms approaching deprecation (for wallet warnings)
    [[nodiscard]] std::vector<AlgorithmInfo> get_deprecation_warnings(BlockHeight current_height) const;

    // ─── Governance: Schedule a lifecycle transition ───

    struct UpgradeProposal {
        AlgorithmID      algorithm;
        AlgorithmStatus  new_status;
        BlockHeight      effective_height;
        std::string      rationale;        // "NIST updated standard", "vulnerability found", etc.
        Hash256          governance_vote_hash;  // Hash of the governance vote that approved this
    };

    /// Apply an approved upgrade proposal
    [[nodiscard]] bool apply_upgrade(const UpgradeProposal& proposal);

    /// Get pending/scheduled upgrades
    [[nodiscard]] std::vector<UpgradeProposal> scheduled_upgrades() const;

private:
    CryptoRegistry();

    std::unordered_map<uint16_t, std::unique_ptr<ISignatureProvider>> sig_providers_;
    std::unordered_map<uint16_t, std::unique_ptr<IKEMProvider>>       kem_providers_;
    std::unordered_map<uint16_t, std::unique_ptr<IHashProvider>>      hash_providers_;
    std::unordered_map<uint16_t, AlgorithmInfo>                       algorithm_info_;
    std::vector<UpgradeProposal>                                      scheduled_upgrades_;

    AlgorithmID preferred_sig_  = algorithms::DILITHIUM_5;
    AlgorithmID preferred_kem_  = algorithms::KYBER_1024;
    AlgorithmID preferred_hash_ = algorithms::SHA3_256;

    mutable std::shared_mutex mutex_;

    void register_genesis_algorithms();
};

// ═══════════════════════════════════════════════════════════════════
//  On-Chain Algorithm Tag
//
//  Every cryptographic object on-chain carries this tag so the
//  verifier knows which algorithm to use. This is the key to
//  backward compatibility — old blocks use old algorithms,
//  new blocks use new ones, all verify correctly.
// ═══════════════════════════════════════════════════════════════════

struct CryptoTag {
    AlgorithmID signature_algo;    // Which signature scheme was used
    AlgorithmID kem_algo;          // Which KEM was used (for stealth addresses)
    AlgorithmID hash_algo;         // Which hash function was used

    [[nodiscard]] Bytes serialize() const {
        return {
            static_cast<uint8_t>(signature_algo.id >> 8),
            static_cast<uint8_t>(signature_algo.id & 0xFF),
            static_cast<uint8_t>(kem_algo.id >> 8),
            static_cast<uint8_t>(kem_algo.id & 0xFF),
            static_cast<uint8_t>(hash_algo.id >> 8),
            static_cast<uint8_t>(hash_algo.id & 0xFF),
        };
    }

    [[nodiscard]] static Result<CryptoTag> deserialize(ByteSpan data) {
        if (data.size() < 6) return Result<CryptoTag>::failure("CryptoTag too short");
        return Result<CryptoTag>::success({
            {static_cast<uint16_t>((data[0] << 8) | data[1])},
            {static_cast<uint16_t>((data[2] << 8) | data[3])},
            {static_cast<uint16_t>((data[4] << 8) | data[5])},
        });
    }

    /// Create a tag using the current preferred algorithms
    [[nodiscard]] static CryptoTag current() {
        auto& reg = CryptoRegistry::instance();
        return {
            reg.preferred_signature(),
            reg.preferred_kem(),
            reg.preferred_hash(),
        };
    }
};

// ═══════════════════════════════════════════════════════════════════
//  Migration Manager
//
//  When an algorithm is deprecated, users holding funds secured
//  by that algorithm need to migrate to the new one. This system
//  tracks migration progress and can enforce deadlines.
// ═══════════════════════════════════════════════════════════════════

class MigrationManager {
public:
    struct MigrationWindow {
        AlgorithmID      old_algo;
        AlgorithmID      new_algo;
        BlockHeight      start_height;        // When migration opens
        BlockHeight      recommended_by;      // When wallets should warn
        BlockHeight      deadline_height;      // After this, old algo UTXOs frozen
        bool             fee_waiver;           // Free migration transactions
    };

    /// Get active migration windows
    [[nodiscard]] std::vector<MigrationWindow> active_windows(BlockHeight current) const;

    /// Check if a specific UTXO's algorithm needs migration
    [[nodiscard]] bool needs_migration(AlgorithmID algo, BlockHeight current) const;

    /// Check if migration transactions get fee waivers
    [[nodiscard]] bool is_fee_waived(AlgorithmID old_algo, BlockHeight current) const;

    /// Create a migration transaction (moves funds from old algo keys to new algo keys)
    /// This is a special transaction type that:
    ///   - Signs with the OLD algorithm (proving ownership)
    ///   - Creates outputs under the NEW algorithm
    ///   - Gets priority inclusion in blocks
    ///   - May have waived fees during the migration window
    [[nodiscard]] Result<Bytes> create_migration_tx(
        ByteSpan old_secret_key,
        AlgorithmID old_algo,
        ByteSpan new_public_key,
        AlgorithmID new_algo,
        const std::vector<Hash256>& utxo_commitments
    );

    /// Add a new migration window (from governance)
    void add_window(MigrationWindow window);

private:
    std::vector<MigrationWindow> windows_;
    mutable std::shared_mutex mutex_;
};

// ═══════════════════════════════════════════════════════════════════
//  Annual Crypto Health Check (Automated)
//
//  Runs periodic checks against known vulnerability databases
//  and NIST recommendations. Emits warnings when algorithms
//  approach their expected security lifetime.
// ═══════════════════════════════════════════════════════════════════

struct CryptoHealthReport {
    struct AlgorithmHealth {
        AlgorithmID     id;
        std::string     name;
        AlgorithmStatus status;
        uint32_t        years_in_service;
        uint32_t        estimated_safe_years;  // Conservative estimate
        std::string     latest_nist_status;    // "Recommended", "Under Review", etc.
        bool            known_vulnerabilities;
        std::string     recommendation;        // "No action", "Plan migration", "Migrate now"
    };

    std::vector<AlgorithmHealth> algorithms;
    BlockHeight                  checked_at_height;
    uint64_t                     checked_at_timestamp;

    /// Generate a human-readable summary
    [[nodiscard]] std::string summary() const;
};

class CryptoHealthMonitor {
public:
    /// Generate a health report for all active algorithms
    [[nodiscard]] CryptoHealthReport generate_report(BlockHeight current_height) const;

    /// Check if any algorithm needs urgent attention
    [[nodiscard]] bool has_urgent_warnings(BlockHeight current_height) const;
};

} // namespace npchain::crypto

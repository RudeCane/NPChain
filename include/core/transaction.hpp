#pragma once

#include "utils/types.hpp"
#include "crypto/hash.hpp"
#include "crypto/dilithium.hpp"
#include "crypto/kyber.hpp"
#include "crypto/commitment.hpp"
#include "crypto/agility.hpp"

namespace npchain::core {

using namespace npchain::crypto;

// ═══════════════════════════════════════════════════════════════════
//  Transaction Input (Privacy-Preserving)
//
//  Uses nullifiers + zk proofs instead of explicit UTXO references.
//  This hides which UTXO is being spent (transaction graph privacy).
// ═══════════════════════════════════════════════════════════════════

struct TxInput {
    Hash256 nullifier;      // Unique identifier derived from UTXO + secret key
    Commitment commitment;  // Pedersen commitment to the input amount
    Bytes   zk_proof;       // Zero-knowledge proof of:
                            //   1. UTXO exists in the set (Merkle membership)
                            //   2. Nullifier is correctly derived
                            //   3. Signer owns the private key

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<TxInput> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  Transaction Output (Stealth Address + Confidential Amount)
// ═══════════════════════════════════════════════════════════════════

struct TxOutput {
    Bytes      stealth_address;   // One-time address (recipient only can detect)
    Commitment commitment;        // Pedersen commitment to amount
    RangeProof range_proof;       // Proof that amount ∈ [0, 2^64)
    Bytes      ephemeral_pubkey;  // Kyber ephemeral key for stealth derivation
    Bytes      encrypted_amount;  // Amount encrypted to recipient (for their records)

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<TxOutput> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  Full Transaction
// ═══════════════════════════════════════════════════════════════════

struct Transaction {
    // ─── Header ───
    uint8_t  version    = 1;
    uint32_t chain_id   = CHAIN_ID;
    uint64_t epoch_nonce = 0;         // Replay protection: changes each epoch
    uint64_t timestamp  = 0;

    // ─── Crypto Agility Tag ───
    // Tells validators which algorithms this transaction uses.
    // Enables seamless verification of transactions from any era.
    CryptoTag crypto_tag = CryptoTag::current();

    // ─── Body ───
    std::vector<TxInput>  inputs;
    std::vector<TxOutput> outputs;
    Commitment            fee_commitment;  // Hidden fee amount

    // ─── Auth ───
    // Signature algorithm determined by crypto_tag.signature_algo
    // At genesis: Dilithium-5. After governance upgrade: whatever is current.
    Bytes signature_bytes;                 // Algorithm-agnostic signature storage

    // ─── Computed ───
    [[nodiscard]] Hash256 tx_id() const;   // SHA3-256 of serialized tx (excl. signature)
    [[nodiscard]] Hash256 hash() const;    // SHA3-256 of full serialized tx
    [[nodiscard]] Bytes   serialize() const;
    [[nodiscard]] static Result<Transaction> deserialize(ByteSpan data);

    // ─── Validation (crypto-agile: uses CryptoTag to pick algorithm) ───
    [[nodiscard]] bool verify_signature(ByteSpan sender_pubkey) const noexcept;
    [[nodiscard]] bool verify_commitments() const noexcept;
    [[nodiscard]] bool verify_range_proofs() const noexcept;
    [[nodiscard]] bool verify_zk_proofs() const noexcept;

    struct ValidationResult {
        bool valid = false;
        std::string error;
    };

    /// Full transaction validation
    [[nodiscard]] ValidationResult validate() const;
};

// ═══════════════════════════════════════════════════════════════════
//  Coinbase Transaction (Block Reward)
// ═══════════════════════════════════════════════════════════════════

struct CoinbaseTx {
    BlockHeight height;
    Amount      reward;              // Calculated from annual emission cap
    Amount      total_fees;          // Sum of fees from block transactions
    TxOutput    miner_output;        // Reward sent to miner's stealth address
    Bytes       extra_data;          // Up to 256 bytes (miner tag, etc.)

    [[nodiscard]] Hash256 hash() const;
    [[nodiscard]] Bytes serialize() const;

    /// Calculate the block reward for a given height.
    ///
    /// Unlimited supply model with a fixed annual emission cap:
    ///   - Every block receives ANNUAL_EMISSION_CAP / BLOCKS_PER_YEAR (≈190,258 Certs)
    ///   - Complexity Frags: remainder units distributed to final blocks of each year
    ///   - Total emission per year never exceeds ANNUAL_EMISSION_CAP
    ///   - No halving, no hard supply cap
    ///   - Inflation rate = ANNUAL_EMISSION_CAP / current_total_supply
    ///     → declines asymptotically toward 0% but never reaches it
    ///
    [[nodiscard]] static Amount calculate_reward(BlockHeight height) noexcept {
        uint64_t year_position = height % BLOCKS_PER_YEAR;
        Amount reward = BLOCK_REWARD;
        uint64_t cfrag_start = BLOCKS_PER_YEAR - COMPLEXITY_FRAGS;
        if (year_position >= cfrag_start) {
            reward += 1;
        }
        return reward;
    }

    /// Calculate cumulative supply at a given block height.
    /// Returns base units (1 Cert = 10^6 base units).
    [[nodiscard]] static Amount cumulative_supply(BlockHeight height) noexcept {
        uint64_t full_years = height / BLOCKS_PER_YEAR;
        uint64_t remaining  = height % BLOCKS_PER_YEAR;
        __uint128_t supply = static_cast<__uint128_t>(full_years) * ANNUAL_EMISSION_CAP;
        supply += static_cast<__uint128_t>(remaining) * BLOCK_REWARD;
        uint64_t cfrag_start = BLOCKS_PER_YEAR - COMPLEXITY_FRAGS;
        if (remaining > cfrag_start) {
            supply += (remaining - cfrag_start);
        }
        if (supply > UINT64_MAX) return UINT64_MAX;
        return static_cast<Amount>(supply);
    }

    /// Calculate cumulative supply in whole Cert units
    [[nodiscard]] static uint64_t cumulative_supply_certs(BlockHeight height) noexcept {
        uint64_t full_years = height / BLOCKS_PER_YEAR;
        constexpr uint64_t CAP_CERTS = ANNUAL_EMISSION_CAP / BASE_UNITS_PER_CERT; // 100,000,000,000
        return full_years * CAP_CERTS;
    }

    /// Calculate the current annual inflation rate as a percentage × 1000
    [[nodiscard]] static uint64_t inflation_rate_millipct(BlockHeight height) noexcept {
        uint64_t supply_certs = cumulative_supply_certs(height);
        if (supply_certs == 0) return 0;
        constexpr uint64_t CAP_CERTS = ANNUAL_EMISSION_CAP / BASE_UNITS_PER_CERT;
        return (CAP_CERTS * 100'000) / supply_certs;
    }
};

} // namespace npchain::core

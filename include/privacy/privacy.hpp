#pragma once

#include "utils/types.hpp"
#include "crypto/dilithium.hpp"
#include "crypto/kyber.hpp"
#include "crypto/commitment.hpp"
#include "core/transaction.hpp"

namespace npchain::privacy {

using namespace npchain::crypto;
using namespace npchain::core;

// ═══════════════════════════════════════════════════════════════════
//  Post-Quantum Stealth Address System
//
//  Allows payments to be received at one-time addresses that only
//  the recipient can detect and spend from. Uses Kyber KEM for
//  the key exchange (quantum-safe).
//
//  Protocol:
//  1. Recipient publishes (scan_key_pub, spend_key_pub) - Dilithium keys
//  2. Sender generates ephemeral Kyber keypair
//  3. Sender encapsulates shared_secret using recipient's spend_key
//  4. One-time address derived: P = Hash(shared_secret || scan_key) → Dilithium pubkey
//  5. Only recipient can scan (using scan_key_secret) and spend (using spend_key_secret)
// ═══════════════════════════════════════════════════════════════════

struct StealthKeyBundle {
    // Public keys (published to network)
    std::array<uint8_t, DILITHIUM_PK_SIZE> scan_pubkey;
    std::array<uint8_t, DILITHIUM_PK_SIZE> spend_pubkey;

    // Secret keys (kept by recipient)
    SecureArray<DILITHIUM_SK_SIZE> scan_secret;
    SecureArray<DILITHIUM_SK_SIZE> spend_secret;
};

struct StealthPaymentData {
    Bytes      one_time_address;       // The stealth address to send to
    Bytes      ephemeral_pubkey;       // Published on-chain (Kyber public key)
    Bytes      encrypted_metadata;     // Encrypted amount + memo (AES-GCM with shared secret)
};

class StealthAddressEngine {
public:
    /// Generate a new stealth key bundle for a recipient
    [[nodiscard]] static Result<StealthKeyBundle> generate_keys();

    /// Sender: create a stealth payment to a recipient
    [[nodiscard]] static Result<StealthPaymentData> create_payment(
        ByteSpan recipient_scan_pubkey,
        ByteSpan recipient_spend_pubkey,
        Amount amount,
        ByteSpan memo = {}
    );

    /// Recipient: scan a transaction to check if it's for us
    [[nodiscard]] static bool scan_transaction(
        const TxOutput& output,
        const SecureArray<DILITHIUM_SK_SIZE>& scan_secret,
        ByteSpan spend_pubkey
    );

    /// Recipient: derive the spending key for a detected payment
    [[nodiscard]] static Result<SecureArray<DILITHIUM_SK_SIZE>> derive_spending_key(
        const TxOutput& output,
        const SecureArray<DILITHIUM_SK_SIZE>& scan_secret,
        const SecureArray<DILITHIUM_SK_SIZE>& spend_secret
    );

    /// Recipient: decrypt the amount and memo from a payment
    struct PaymentInfo {
        Amount      amount;
        Bytes       memo;
        BlindingFactor blinding;  // For reconstructing the commitment
    };

    [[nodiscard]] static Result<PaymentInfo> decrypt_payment(
        const TxOutput& output,
        const SecureArray<DILITHIUM_SK_SIZE>& scan_secret
    );
};

// ═══════════════════════════════════════════════════════════════════
//  Confidential Transaction Builder
//
//  Constructs transactions where amounts are hidden in Pedersen
//  commitments and proven valid with range proofs.
// ═══════════════════════════════════════════════════════════════════

class ConfidentialTxBuilder {
public:
    struct InputDescription {
        Hash256        utxo_commitment_hash;  // Reference to existing commitment
        Amount         amount;                // Known to sender
        BlindingFactor blinding;              // Known to sender
        Bytes          merkle_proof;          // Proof UTXO exists in set
        // Spending authorization
        SecureArray<DILITHIUM_SK_SIZE>* spending_key;
    };

    struct OutputDescription {
        ByteSpan recipient_scan_pubkey;
        ByteSpan recipient_spend_pubkey;
        Amount   amount;
        Bytes    memo;
    };

    /// Build a complete confidential transaction
    [[nodiscard]] static Result<Transaction> build(
        const std::vector<InputDescription>& inputs,
        const std::vector<OutputDescription>& outputs,
        Amount fee,
        uint64_t epoch_nonce,
        const SecureArray<DILITHIUM_SK_SIZE>& sender_key
    );

private:
    /// Ensure sum(input blindings) = sum(output blindings) + fee_blinding
    static void balance_blindings(
        const std::vector<BlindingFactor>& input_blindings,
        std::vector<BlindingFactor>& output_blindings,
        BlindingFactor& fee_blinding
    );
};

// ═══════════════════════════════════════════════════════════════════
//  Zero-Knowledge Proof System (Lattice-Based)
//
//  Proves UTXO ownership and Merkle set membership without
//  revealing which UTXO is being spent.
//
//  Proof system: Lattice-based Σ-protocol → NIZK (Fiat-Shamir + SHAKE)
// ═══════════════════════════════════════════════════════════════════

class ZKProofSystem {
public:
    struct OwnershipStatement {
        Hash256    nullifier;            // Public: the nullifier being revealed
        Hash256    merkle_root;          // Public: UTXO set root
        Commitment commitment;           // Public: the commitment being spent
    };

    struct OwnershipWitness {
        Hash256        utxo_hash;        // Private: which UTXO
        Amount         amount;           // Private: the amount
        BlindingFactor blinding;         // Private: the blinding factor
        Bytes          merkle_path;      // Private: Merkle proof of membership
        SecureArray<DILITHIUM_SK_SIZE>* key;  // Private: spending key
    };

    /// Generate a zero-knowledge proof of UTXO ownership
    [[nodiscard]] static Result<Bytes> prove(
        const OwnershipStatement& statement,
        const OwnershipWitness& witness
    );

    /// Verify a zero-knowledge proof
    [[nodiscard]] static bool verify(
        const OwnershipStatement& statement,
        ByteSpan proof
    ) noexcept;

    /// Batch-verify multiple proofs (more efficient)
    [[nodiscard]] static bool batch_verify(
        const std::vector<std::pair<OwnershipStatement, Bytes>>& proofs
    ) noexcept;
};

} // namespace npchain::privacy

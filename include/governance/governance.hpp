#pragma once

#include "utils/types.hpp"
#include "crypto/agility.hpp"
#include "crypto/dilithium.hpp"

#include <vector>
#include <shared_mutex>

namespace npchain::governance {

using namespace npchain::crypto;

// ═══════════════════════════════════════════════════════════════════
//  ON-CHAIN GOVERNANCE FOR CRYPTOGRAPHIC UPGRADES
//
//  How a new algorithm gets adopted:
//
//  ┌─────────────┐    ┌──────────────┐    ┌────────────────┐
//  │  NIST/IEEE  │───▶│  Developer   │───▶│  Submit NIP    │
//  │  publishes  │    │  implements  │    │  (NPChain      │
//  │  standard   │    │  provider    │    │  Improvement    │
//  │             │    │              │    │  Proposal)      │
//  └─────────────┘    └──────────────┘    └───────┬────────┘
//                                                  │
//           ┌──────────────────────────────────────┘
//           ▼
//  ┌─────────────────┐    ┌──────────────┐    ┌──────────────┐
//  │  Review Period  │───▶│  Validator   │───▶│  Activation  │
//  │  (90 days min)  │    │  Vote        │    │  at height N │
//  │  audit + test   │    │  (75% quorum)│    │  + migration │
//  └─────────────────┘    └──────────────┘    └──────────────┘
//
//  This is NOT a "move fast and break things" governance model.
//  Cryptographic upgrades are SLOW AND DELIBERATE by design.
//  Minimum 90-day review. Multiple independent audits required.
//  Activation delay gives all wallets/nodes time to update.
// ═══════════════════════════════════════════════════════════════════

// ─── NPChain Improvement Proposal (NIP) ───────────────────────────

enum class NIPType : uint8_t {
    CRYPTO_ADDITION    = 0x01,  // Add new algorithm to registry
    CRYPTO_DEPRECATION = 0x02,  // Deprecate an existing algorithm
    CRYPTO_RETIREMENT  = 0x03,  // Emergency retire (vulnerability found)
    CRYPTO_PREFERRED   = 0x04,  // Change which algorithm is "preferred"
    PARAM_CHANGE       = 0x10,  // Change consensus parameters
    MIGRATION_WINDOW   = 0x20,  // Open a migration window for users
};

enum class NIPStatus : uint8_t {
    DRAFT        = 0,  // Submitted, not yet in review
    REVIEW       = 1,  // Under review (90+ day clock started)
    VOTING       = 2,  // Review complete, voting open
    APPROVED     = 3,  // Vote passed, awaiting activation
    ACTIVATED    = 4,  // Live on chain
    REJECTED     = 5,  // Vote failed
    WITHDRAWN    = 6,  // Author withdrew
    EMERGENCY    = 7,  // Fast-tracked (vulnerability response)
};

struct NIP {
    // ─── Identity ───
    uint32_t    nip_number;
    Hash256     proposal_hash;        // SHA3-256 of full proposal content
    NIPType     type;
    NIPStatus   status;

    // ─── Content ───
    std::string title;                // "NIP-007: Add FALCON-1024 Signature Support"
    std::string summary;              // 2-3 sentence description
    std::string rationale;            // Why this change is needed
    std::string specification;        // Technical details
    std::string security_analysis;    // Impact on chain security

    // ─── Cryptographic Upgrade Details (if applicable) ───
    std::optional<AlgorithmInfo>               new_algorithm;
    std::optional<AlgorithmID>                 affected_algorithm;
    std::optional<CryptoRegistry::UpgradeProposal> upgrade;
    std::optional<MigrationManager::MigrationWindow> migration;

    // ─── Timeline ───
    BlockHeight submitted_height;
    BlockHeight review_start_height;
    BlockHeight review_end_height;     // Must be >= submitted + REVIEW_PERIOD
    BlockHeight vote_start_height;
    BlockHeight vote_end_height;       // Voting window
    BlockHeight activation_height;     // When it goes live (if approved)

    // ─── Author ───
    std::array<uint8_t, DILITHIUM_PK_SIZE> author_pubkey;
    DilithiumSignature author_signature;

    // ─── Audit Requirements ───
    struct AuditRecord {
        std::string auditor_name;      // "Trail of Bits", "NCC Group", etc.
        Hash256     report_hash;       // Hash of the audit report
        bool        passed;
        std::array<uint8_t, DILITHIUM_PK_SIZE> auditor_pubkey;
        DilithiumSignature auditor_signature;
    };

    std::vector<AuditRecord> audits;
    static constexpr uint32_t MIN_AUDITS = 2;  // At least 2 independent audits

    [[nodiscard]] bool has_sufficient_audits() const {
        uint32_t passed = 0;
        for (const auto& a : audits) if (a.passed) ++passed;
        return passed >= MIN_AUDITS;
    }

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<NIP> deserialize(ByteSpan data);
};

// ─── Governance Constants ──────────────────────────────────────────

namespace rules {
    constexpr uint64_t REVIEW_PERIOD_BLOCKS   = 525'600 / 4;  // ~90 days
    constexpr uint64_t VOTING_PERIOD_BLOCKS   = 525'600 / 12; // ~30 days
    constexpr uint64_t ACTIVATION_DELAY       = 525'600 / 6;  // ~60 days after approval
    constexpr uint64_t MIGRATION_WINDOW_MIN   = 525'600;      // ~1 year minimum
    constexpr double   APPROVAL_THRESHOLD     = 0.75;         // 75% of voting power
    constexpr double   EMERGENCY_THRESHOLD    = 0.90;         // 90% for fast-track

    // Emergency proposals skip the review period but need higher approval
    constexpr uint64_t EMERGENCY_VOTE_PERIOD  = 525'600 / 52; // ~1 week
    constexpr uint64_t EMERGENCY_ACTIVATION   = 525'600 / 52; // ~1 week after approval
}

// ─── Vote ──────────────────────────────────────────────────────────

struct Vote {
    enum class Choice : uint8_t { APPROVE = 1, REJECT = 2, ABSTAIN = 3 };

    uint32_t   nip_number;
    Choice     choice;
    Amount     voting_power;           // Stake-weighted
    std::array<uint8_t, DILITHIUM_PK_SIZE> voter_pubkey;
    DilithiumSignature signature;
    BlockHeight cast_at_height;

    [[nodiscard]] Bytes serialize() const;
};

struct VoteTally {
    uint32_t nip_number;
    Amount   total_approve = 0;
    Amount   total_reject  = 0;
    Amount   total_abstain = 0;
    Amount   total_eligible = 0;       // Total possible voting power

    [[nodiscard]] double approval_rate() const {
        Amount voted = total_approve + total_reject;
        if (voted == 0) return 0.0;
        return static_cast<double>(total_approve) / static_cast<double>(voted);
    }

    [[nodiscard]] double participation_rate() const {
        if (total_eligible == 0) return 0.0;
        return static_cast<double>(total_approve + total_reject + total_abstain)
             / static_cast<double>(total_eligible);
    }

    [[nodiscard]] bool passes_normal() const {
        return approval_rate() >= rules::APPROVAL_THRESHOLD
            && participation_rate() >= 0.33;  // 33% minimum participation
    }

    [[nodiscard]] bool passes_emergency() const {
        return approval_rate() >= rules::EMERGENCY_THRESHOLD
            && participation_rate() >= 0.50;  // 50% minimum for emergency
    }
};

// ═══════════════════════════════════════════════════════════════════
//  Governance Engine
// ═══════════════════════════════════════════════════════════════════

class GovernanceEngine {
public:
    GovernanceEngine() = default;

    // ─── Proposal Lifecycle ───

    /// Submit a new proposal
    [[nodiscard]] Result<uint32_t> submit_proposal(NIP proposal);

    /// Add an audit record to a proposal
    [[nodiscard]] bool add_audit(uint32_t nip_number, NIP::AuditRecord audit);

    /// Advance proposal status (called by consensus when heights are reached)
    void process_height(BlockHeight height);

    /// Submit a vote
    [[nodiscard]] bool cast_vote(Vote vote);

    /// Get current tally for a proposal
    [[nodiscard]] VoteTally get_tally(uint32_t nip_number) const;

    // ─── Query ───

    [[nodiscard]] std::optional<NIP> get_proposal(uint32_t nip_number) const;
    [[nodiscard]] std::vector<NIP> get_active_proposals() const;
    [[nodiscard]] std::vector<NIP> get_proposals_by_status(NIPStatus status) const;

    // ─── Integration with CryptoRegistry ───

    /// Apply all approved proposals that have reached their activation height
    void activate_approved(BlockHeight current_height, CryptoRegistry& registry);

    // ─── Annual Crypto Review Trigger ───

    /// Called once per year (every BLOCKS_PER_YEAR blocks).
    /// Generates a CryptoHealthReport and auto-creates deprecation
    /// proposals for any algorithm that NIST has flagged.
    void annual_crypto_review(
        BlockHeight current_height,
        const CryptoHealthMonitor& monitor
    );

private:
    std::vector<NIP> proposals_;
    std::unordered_map<uint32_t, std::vector<Vote>> votes_;
    uint32_t next_nip_number_ = 1;
    mutable std::shared_mutex mutex_;

    void transition_proposal(NIP& nip, NIPStatus new_status);
};

// ═══════════════════════════════════════════════════════════════════
//  Upgrade Timeline Example
//
//  Year 0:  Genesis with Dilithium-5, Kyber-1024, SHA3-256
//
//  Year 3:  NIST standardizes FALCON-1024 as additional option
//           → NIP submitted to add FALCON-1024
//           → 90 day review + 2 audits
//           → 30 day vote (passes at 82%)
//           → 60 day activation delay
//           → FALCON-1024 now ACTIVE alongside Dilithium-5
//           → Wallets can choose either
//
//  Year 7:  Lattice cryptanalysis advances reduce Dilithium-5
//           confidence margin. Still safe but less margin.
//           → NIP to make FALCON-1024 PREFERRED
//           → Dilithium-5 remains ACTIVE (still safe)
//           → New wallets default to FALCON
//
//  Year 12: New PQC algorithm "FutureSign" passes NIST evaluation
//           → NIP to add FutureSign
//           → NIP to deprecate Dilithium-5 (still verifiable)
//           → 1-year migration window opens (fee-waived)
//           → After migration: Dilithium-5 → SUNSET
//
//  Year 14: Dilithium-5 RETIRED (extreme case, only if broken)
//           → Any remaining Dilithium UTXOs frozen
//           → Governance can vote to unfreeze if needed
//
//  At NO POINT does the chain hard-fork. Old blocks with old
//  algorithms remain valid forever. The CryptoTag in each
//  transaction tells validators which provider to use.
// ═══════════════════════════════════════════════════════════════════

} // namespace npchain::governance

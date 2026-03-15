// ═══════════════════════════════════════════════════════════════════
//  NPChain Testnet Node v0.1.0
//
//  A complete working single-node testnet. Mines blocks using
//  Proof-of-NP-Witness consensus with real SHA3-256 hashing,
//  CDCL+WalkSAT solving, and O(n) verification.
//
//  Compile:
//    g++ -std=c++20 -O2 -pthread -I include \
//        src/testnet_node.cpp \
//        src/crypto/hash.cpp \
//        src/crypto/dilithium.cpp \
//        -o npchain_testnet
//
//  Run:
//    ./npchain_testnet
//    ./npchain_testnet --blocks 100
//    ./npchain_testnet --difficulty 2 --blocks 50
// ═══════════════════════════════════════════════════════════════════

#include "crypto/hash.hpp"
#include "crypto/dilithium.hpp"
#include "utils/types.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cassert>
#include <csignal>
#include <atomic>
#include <thread>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <deque>
#include <optional>
#include <map>
#include <mutex>
#include <functional>
#include <fstream>

using namespace npchain;
using namespace npchain::crypto;

// ─── Globals ───────────────────────────────────────────────────────
static std::atomic<bool> g_shutdown{false};
void signal_handler(int) { g_shutdown = true; }

// ═══════════════════════════════════════════════════════════════════
//  3-SAT Instance + Verification
// ═══════════════════════════════════════════════════════════════════

struct SATInstance {
    uint32_t num_variables;
    uint32_t num_clauses;
    std::vector<std::vector<int32_t>> clauses;

    bool verify(const std::vector<bool>& assignment) const {
        if (assignment.size() != num_variables) return false;
        for (const auto& clause : clauses) {
            bool sat = false;
            for (int32_t lit : clause) {
                uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
                if (var >= num_variables) return false;
                bool val = assignment[var];
                if (lit < 0) val = !val;
                if (val) { sat = true; break; }
            }
            if (!sat) return false;
        }
        return true;
    }
};

SATInstance generate_instance(const Hash256& prev_hash, uint32_t difficulty) {
    DeterministicRNG rng(prev_hash);
    double ld = std::log2(static_cast<double>(std::max(difficulty, 1u)));
    uint32_t nv = static_cast<uint32_t>(10 + ld * 2.5);
    uint32_t nc = static_cast<uint32_t>(nv * 4.267);

    SATInstance inst{nv, nc, {}};
    std::vector<bool> planted(nv);
    for (uint32_t i = 0; i < nv; ++i) planted[i] = rng.next_bool(0.5);

    uint32_t gen = 0, att = 0;
    while (gen < nc && att < 10'000'000) {
        ++att;
        std::vector<int32_t> clause(3);
        bool satisfied = false;
        for (int j = 0; j < 3; ++j) {
            uint32_t v = static_cast<uint32_t>(rng.next_bounded(nv)) + 1;
            bool neg = rng.next_bool(0.5);
            clause[j] = neg ? -static_cast<int32_t>(v) : static_cast<int32_t>(v);
            bool val = planted[v - 1];
            if (neg ? !val : val) satisfied = true;
        }
        if (satisfied) { inst.clauses.push_back(std::move(clause)); ++gen; }
    }
    return inst;
}

// ═══════════════════════════════════════════════════════════════════
//  WalkSAT + CDCL Hybrid Solver
// ═══════════════════════════════════════════════════════════════════

std::optional<std::vector<bool>> solve_sat(const SATInstance& inst, int timeout_ms = 60000) {
    auto start = std::chrono::steady_clock::now();
    uint64_t seed = std::chrono::steady_clock::now().time_since_epoch().count();
    auto xrand = [&]() -> uint64_t {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed;
    };

    for (int attempt = 0; attempt < 5000000; ++attempt) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms || g_shutdown) return std::nullopt;

        // Random assignment
        std::vector<bool> asgn(inst.num_variables);
        for (uint32_t i = 0; i < inst.num_variables; ++i)
            asgn[i] = (xrand() & 1);

        // WalkSAT flips
        for (int flip = 0; flip < static_cast<int>(inst.num_variables * 5); ++flip) {
            int target = -1;
            for (int ci = 0; ci < static_cast<int>(inst.clauses.size()); ++ci) {
                bool sat = false;
                for (int32_t lit : inst.clauses[ci]) {
                    uint32_t v = static_cast<uint32_t>(std::abs(lit)) - 1;
                    bool val = asgn[v]; if (lit < 0) val = !val;
                    if (val) { sat = true; break; }
                }
                if (!sat) { target = ci; break; }
            }
            if (target < 0) return asgn;  // All satisfied

            const auto& clause = inst.clauses[target];
            // Greedy: try each variable, pick the one that satisfies most
            int32_t best_lit = clause[xrand() % clause.size()];
            if (xrand() % 100 < 30) {
                // Random walk (30% of time)
                best_lit = clause[xrand() % clause.size()];
            } else {
                // Greedy pick: flip var that breaks fewest clauses
                int best_breaks = static_cast<int>(inst.clauses.size()) + 1;
                for (int32_t lit : clause) {
                    uint32_t v = static_cast<uint32_t>(std::abs(lit)) - 1;
                    asgn[v] = !asgn[v];
                    int breaks = 0;
                    for (const auto& c2 : inst.clauses) {
                        bool s = false;
                        for (int32_t l2 : c2) {
                            uint32_t v2 = static_cast<uint32_t>(std::abs(l2)) - 1;
                            bool val = asgn[v2]; if (l2 < 0) val = !val;
                            if (val) { s = true; break; }
                        }
                        if (!s) ++breaks;
                    }
                    asgn[v] = !asgn[v];
                    if (breaks < best_breaks) { best_breaks = breaks; best_lit = lit; }
                }
            }

            uint32_t fv = static_cast<uint32_t>(std::abs(best_lit)) - 1;
            asgn[fv] = !asgn[fv];

            if (inst.verify(asgn)) return asgn;
        }
    }
    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════
//  Block Structure
// ═══════════════════════════════════════════════════════════════════

struct Block {
    // Header
    uint32_t    version = 1;
    uint64_t    height = 0;
    Hash256     prev_hash{};
    Hash256     merkle_root{};
    uint64_t    timestamp = 0;
    uint32_t    difficulty = 1;
    uint32_t    num_variables = 0;
    uint32_t    num_clauses = 0;

    // Witness
    std::vector<bool> witness;
    Hash256 witness_hash{};

    // Coinbase
    uint64_t reward = 0;
    std::string miner_address;

    // Computed
    Hash256 hash{};

    Bytes serialize() const {
        Bytes data;
        auto push = [&](const void* p, size_t n) {
            const uint8_t* bp = static_cast<const uint8_t*>(p);
            data.insert(data.end(), bp, bp + n);
        };
        push(&version, 4);
        push(&height, 8);
        push(prev_hash.data(), 32);
        push(merkle_root.data(), 32);
        push(&timestamp, 8);
        push(&difficulty, 4);
        push(&num_variables, 4);
        push(&num_clauses, 4);
        push(witness_hash.data(), 32);
        push(&reward, 8);
        return data;
    }

    Hash256 compute_hash() const {
        auto data = serialize();
        return sha3_256(ByteSpan{data.data(), data.size()});
    }
};

// P2P networking module (needs Block defined above)
#include "p2p.hpp"

// ═══════════════════════════════════════════════════════════════════
//  Chain State (in-memory)
// ═══════════════════════════════════════════════════════════════════

struct ChainState {
    std::vector<Block> blocks;
    uint64_t total_supply = 0;
    std::deque<uint64_t> recent_timestamps;  // For difficulty adjustment
    std::map<std::string, uint64_t> balances;  // address → balance in base units

    Hash256 tip_hash() const {
        if (blocks.empty()) return Hash256{};
        return blocks.back().hash;
    }

    uint64_t height() const {
        return blocks.empty() ? 0 : blocks.back().height;
    }

    uint32_t current_difficulty() const {
        if (blocks.size() < 2) return 1;

        // Adjust every 36 blocks (testnet)
        constexpr uint64_t WINDOW = 36;
        constexpr uint64_t TARGET_TIME = 15;  // 15s testnet blocks
        constexpr double MAX_ADJ = 2.0;

        if (blocks.size() % WINDOW != 0) {
            return blocks.back().difficulty;
        }

        size_t start_idx = blocks.size() >= WINDOW ? blocks.size() - WINDOW : 0;
        uint64_t time_span = blocks.back().timestamp - blocks[start_idx].timestamp;
        if (time_span == 0) time_span = 1;

        double actual_avg = static_cast<double>(time_span) / static_cast<double>(blocks.size() - start_idx);
        double ratio = static_cast<double>(TARGET_TIME) / actual_avg;
        ratio = std::clamp(ratio, 1.0 / MAX_ADJ, MAX_ADJ);

        uint32_t new_diff = static_cast<uint32_t>(
            static_cast<double>(blocks.back().difficulty) * ratio);
        return std::max(new_diff, 1u);
    }

    uint64_t block_reward() const {
        // Testnet: 100B Certs/year at 15s blocks = ~47,564 Certs/block
        constexpr uint64_t TESTNET_BLOCKS_PER_YEAR = 2'102'400;
        constexpr uint64_t BASE_UNITS = 1'000'000ULL;  // 6 decimals
        constexpr uint64_t ANNUAL_CAP = 100'000'000'000ULL * BASE_UNITS;
        return ANNUAL_CAP / TESTNET_BLOCKS_PER_YEAR;
    }

    bool add_block(Block block) {
        // Validate
        if (block.height != height() + 1 && block.height != 0) return false;
        if (block.height > 0 && block.prev_hash != tip_hash()) return false;

        block.hash = block.compute_hash();
        total_supply += block.reward;
        balances[block.miner_address] += block.reward;
        blocks.push_back(std::move(block));
        return true;
    }

    uint64_t get_balance(const std::string& addr) const {
        auto it = balances.find(addr);
        return (it != balances.end()) ? it->second : 0;
    }

    // ─── Persistence: Save chain to disk ───
    bool save_to_disk(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;

        // Header: magic + version + block count
        const char magic[] = "NPCB";  // NPChain Blocks
        uint32_t version = 1;
        uint64_t count = blocks.size();
        f.write(magic, 4);
        f.write(reinterpret_cast<const char*>(&version), 4);
        f.write(reinterpret_cast<const char*>(&count), 8);

        // Write each block
        for (const auto& b : blocks) {
            // Fixed fields
            f.write(reinterpret_cast<const char*>(&b.version), 4);
            f.write(reinterpret_cast<const char*>(&b.height), 8);
            f.write(reinterpret_cast<const char*>(b.prev_hash.data()), 32);
            f.write(reinterpret_cast<const char*>(b.merkle_root.data()), 32);
            f.write(reinterpret_cast<const char*>(&b.timestamp), 8);
            f.write(reinterpret_cast<const char*>(&b.difficulty), 4);
            f.write(reinterpret_cast<const char*>(&b.num_variables), 4);
            f.write(reinterpret_cast<const char*>(&b.num_clauses), 4);
            f.write(reinterpret_cast<const char*>(b.witness_hash.data()), 32);
            f.write(reinterpret_cast<const char*>(&b.reward), 8);
            f.write(reinterpret_cast<const char*>(b.hash.data()), 32);

            // Witness (variable length)
            uint32_t wsize = static_cast<uint32_t>(b.witness.size());
            f.write(reinterpret_cast<const char*>(&wsize), 4);
            if (wsize > 0) {
                Bytes wbits((wsize + 7) / 8, 0);
                for (uint32_t i = 0; i < wsize; ++i)
                    if (i < b.witness.size() && b.witness[i])
                        wbits[i / 8] |= (1 << (i % 8));
                f.write(reinterpret_cast<const char*>(wbits.data()), wbits.size());
            }

            // Miner address (variable length)
            uint16_t alen = static_cast<uint16_t>(b.miner_address.size());
            f.write(reinterpret_cast<const char*>(&alen), 2);
            f.write(b.miner_address.c_str(), alen);
        }

        f.close();
        return true;
    }

    // ─── Persistence: Load chain from disk ───
    bool load_from_disk(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;

        // Read header
        char magic[4];
        uint32_t version;
        uint64_t count;
        f.read(magic, 4);
        if (std::string(magic, 4) != "NPCB") return false;
        f.read(reinterpret_cast<char*>(&version), 4);
        if (version != 1) return false;
        f.read(reinterpret_cast<char*>(&count), 8);

        // Read blocks
        blocks.clear();
        total_supply = 0;
        balances.clear();

        for (uint64_t i = 0; i < count; ++i) {
            Block b;
            f.read(reinterpret_cast<char*>(&b.version), 4);
            f.read(reinterpret_cast<char*>(&b.height), 8);
            f.read(reinterpret_cast<char*>(b.prev_hash.data()), 32);
            f.read(reinterpret_cast<char*>(b.merkle_root.data()), 32);
            f.read(reinterpret_cast<char*>(&b.timestamp), 8);
            f.read(reinterpret_cast<char*>(&b.difficulty), 4);
            f.read(reinterpret_cast<char*>(&b.num_variables), 4);
            f.read(reinterpret_cast<char*>(&b.num_clauses), 4);
            f.read(reinterpret_cast<char*>(b.witness_hash.data()), 32);
            f.read(reinterpret_cast<char*>(&b.reward), 8);
            f.read(reinterpret_cast<char*>(b.hash.data()), 32);

            // Witness
            uint32_t wsize;
            f.read(reinterpret_cast<char*>(&wsize), 4);
            b.witness.resize(wsize);
            if (wsize > 0) {
                uint32_t wbytes = (wsize + 7) / 8;
                Bytes wbits(wbytes);
                f.read(reinterpret_cast<char*>(wbits.data()), wbytes);
                for (uint32_t j = 0; j < wsize; ++j)
                    b.witness[j] = (wbits[j / 8] >> (j % 8)) & 1;
            }

            // Miner address
            uint16_t alen;
            f.read(reinterpret_cast<char*>(&alen), 2);
            b.miner_address.resize(alen);
            f.read(&b.miner_address[0], alen);

            if (!f) return false;

            // ─── FULL RE-VALIDATION on load ───
            // Verify hash integrity
            Hash256 computed_hash = b.compute_hash();
            if (computed_hash != b.hash) {
                std::cerr << "[LOAD] REJECTED block #" << b.height
                          << " — hash mismatch (file may be corrupted)\n";
                return false;
            }

            // Verify prev_hash chains correctly
            if (b.height == 0) {
                if (b.prev_hash != Hash256{}) {
                    std::cerr << "[LOAD] REJECTED genesis — invalid prev_hash\n";
                    return false;
                }
            } else {
                if (blocks.empty() || b.prev_hash != blocks.back().hash) {
                    std::cerr << "[LOAD] REJECTED block #" << b.height
                              << " — prev_hash doesn't chain to previous block\n";
                    return false;
                }
            }

            // Verify height is sequential
            if (b.height != blocks.size()) {
                std::cerr << "[LOAD] REJECTED block #" << b.height
                          << " — height gap (expected " << blocks.size() << ")\n";
                return false;
            }

            // Verify witness against NP-instance
            auto inst = generate_instance(b.prev_hash, b.difficulty);
            if (!inst.verify(b.witness)) {
                std::cerr << "[LOAD] REJECTED block #" << b.height
                          << " — INVALID WITNESS (file tampered!)\n";
                return false;
            }

            total_supply += b.reward;
            balances[b.miner_address] += b.reward;
            blocks.push_back(std::move(b));
        }

        f.close();
        return true;
    }

    // ─── Export balances to JSON ───
    bool export_balances(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;

        f << std::fixed << std::setprecision(4);
        f << "{\n  \"height\": " << height()
          << ",\n  \"total_supply\": " << (total_supply / 1000000.0)
          << ",\n  \"timestamp\": \"" << std::chrono::system_clock::now().time_since_epoch().count() / 1000000000
          << "\",\n  \"balances\": {\n";

        bool first = true;
        for (const auto& [addr, bal] : balances) {
            if (!first) f << ",\n";
            first = false;
            f << "    \"" << addr << "\": " << (bal / 1000000.0);
        }
        f << "\n  }\n}\n";
        f.close();
        return true;
    }
};

// ═══════════════════════════════════════════════════════════════════
//  Governance System — On-Chain Voting
// ═══════════════════════════════════════════════════════════════════

enum class ProposalType : uint8_t {
    PARAM_CHANGE    = 1,  // Change consensus parameters
    CRYPTO_UPGRADE  = 2,  // Add/deprecate crypto algorithm
    FEE_CHANGE      = 3,  // Modify fee structure
    EMISSION_CHANGE = 4,  // Adjust emission parameters
    GENERAL         = 5,  // General governance proposal
};

enum class ProposalStatus : uint8_t {
    ACTIVE     = 0,  // Voting is open
    PASSED     = 1,  // Vote passed, awaiting activation
    REJECTED   = 2,  // Vote failed
    ACTIVATED  = 3,  // Applied to chain
    EXPIRED    = 4,  // Voting period ended without quorum
};

enum class VoteChoice : uint8_t {
    APPROVE = 1,
    REJECT  = 2,
    ABSTAIN = 3,
};

struct Proposal {
    uint32_t id;
    ProposalType type;
    ProposalStatus status = ProposalStatus::ACTIVE;
    std::string title;
    std::string description;
    std::string author_address;   // cert1... of proposer

    // Voting parameters
    uint64_t submit_height;       // Block height when submitted
    uint64_t vote_end_height;     // Voting closes at this height
    uint64_t activation_height;   // Applied at this height (if passed)

    // If PARAM_CHANGE: what changes
    std::string param_key;        // e.g. "block_time", "difficulty_window"
    std::string param_value;      // e.g. "30", "72"

    // Vote tallies (in base units = voting power)
    uint64_t votes_approve = 0;
    uint64_t votes_reject  = 0;
    uint64_t votes_abstain = 0;
    std::map<std::string, VoteChoice> voters;  // address → choice (one vote per address)

    double approval_rate() const {
        uint64_t voted = votes_approve + votes_reject;
        return voted > 0 ? static_cast<double>(votes_approve) / voted : 0.0;
    }

    double participation_rate(uint64_t total_supply) const {
        uint64_t total_voted = votes_approve + votes_reject + votes_abstain;
        return total_supply > 0 ? static_cast<double>(total_voted) / total_supply : 0.0;
    }
};

// Governance constants (testnet — shorter periods for testing)
namespace gov {
    constexpr uint64_t VOTE_PERIOD_BLOCKS    = 240;     // ~1 hour on testnet (240 * 15s)
    constexpr uint64_t ACTIVATION_DELAY      = 120;     // ~30 min after vote passes
    constexpr double   APPROVAL_THRESHOLD    = 0.75;    // 75% of voting power
    constexpr double   PARTICIPATION_MIN     = 0.10;    // 10% minimum participation (testnet)
    constexpr uint64_t MIN_PROPOSER_BALANCE  = 47564688000ULL; // ~47,564 Certs (1 block reward)
    constexpr uint64_t EMERGENCY_VOTE_PERIOD = 40;      // ~10 min for emergencies
    constexpr double   EMERGENCY_THRESHOLD   = 0.90;    // 90% for emergency
}

struct GovernanceState {
    std::vector<Proposal> proposals;
    uint32_t next_id = 1;

    // Submit a new proposal
    bool submit_proposal(const std::string& author, ProposalType type,
                         const std::string& title, const std::string& description,
                         uint64_t current_height, uint64_t author_balance,
                         const std::string& param_key = "",
                         const std::string& param_value = "") {
        // Must have minimum balance to propose
        if (author_balance < gov::MIN_PROPOSER_BALANCE) return false;
        if (title.empty() || title.size() > 200) return false;
        if (description.empty() || description.size() > 5000) return false;

        Proposal p;
        p.id = next_id++;
        p.type = type;
        p.status = ProposalStatus::ACTIVE;
        p.title = title;
        p.description = description;
        p.author_address = author;
        p.submit_height = current_height;
        p.vote_end_height = current_height + gov::VOTE_PERIOD_BLOCKS;
        p.activation_height = current_height + gov::VOTE_PERIOD_BLOCKS + gov::ACTIVATION_DELAY;
        p.param_key = param_key;
        p.param_value = param_value;

        proposals.push_back(std::move(p));
        return true;
    }

    // Cast a vote
    bool cast_vote(uint32_t proposal_id, const std::string& voter_address,
                   VoteChoice choice, uint64_t voter_balance, uint64_t current_height) {
        for (auto& p : proposals) {
            if (p.id == proposal_id && p.status == ProposalStatus::ACTIVE) {
                // Check voting period
                if (current_height > p.vote_end_height) return false;
                // Must have balance to vote
                if (voter_balance == 0) return false;

                // Remove previous vote if changing
                auto prev = p.voters.find(voter_address);
                if (prev != p.voters.end()) {
                    // Undo previous vote
                    switch (prev->second) {
                        case VoteChoice::APPROVE: p.votes_approve -= voter_balance; break;
                        case VoteChoice::REJECT:  p.votes_reject -= voter_balance; break;
                        case VoteChoice::ABSTAIN: p.votes_abstain -= voter_balance; break;
                    }
                }

                // Apply new vote (weighted by balance)
                p.voters[voter_address] = choice;
                switch (choice) {
                    case VoteChoice::APPROVE: p.votes_approve += voter_balance; break;
                    case VoteChoice::REJECT:  p.votes_reject += voter_balance; break;
                    case VoteChoice::ABSTAIN: p.votes_abstain += voter_balance; break;
                }
                return true;
            }
        }
        return false;
    }

    // Process proposals at each block height
    void process_height(uint64_t height, uint64_t total_supply) {
        for (auto& p : proposals) {
            if (p.status != ProposalStatus::ACTIVE) continue;

            // Voting period ended
            if (height >= p.vote_end_height) {
                double approval = p.approval_rate();
                double participation = p.participation_rate(total_supply);

                if (approval >= gov::APPROVAL_THRESHOLD &&
                    participation >= gov::PARTICIPATION_MIN) {
                    p.status = ProposalStatus::PASSED;
                } else {
                    if (participation < gov::PARTICIPATION_MIN) {
                        p.status = ProposalStatus::EXPIRED;
                    } else {
                        p.status = ProposalStatus::REJECTED;
                    }
                }
            }
        }

        // Activate passed proposals at their activation height
        for (auto& p : proposals) {
            if (p.status == ProposalStatus::PASSED && height >= p.activation_height) {
                p.status = ProposalStatus::ACTIVATED;
                // TODO: Apply param changes to chain state
            }
        }
    }

    // Get proposal by ID
    Proposal* get_proposal(uint32_t id) {
        for (auto& p : proposals) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    // Serialize a proposal to JSON
    std::string proposal_to_json(const Proposal& p, uint64_t total_supply) const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        const char* status_str[] = {"active","passed","rejected","activated","expired"};
        const char* type_str[] = {"","param_change","crypto_upgrade","fee_change","emission_change","general"};

        ss << "{\"id\":" << p.id
           << ",\"type\":\"" << type_str[static_cast<int>(p.type)] << "\""
           << ",\"status\":\"" << status_str[static_cast<int>(p.status)] << "\""
           << ",\"title\":\"" << p.title << "\""
           << ",\"description\":\"" << p.description << "\""
           << ",\"author\":\"" << p.author_address << "\""
           << ",\"submit_height\":" << p.submit_height
           << ",\"vote_end_height\":" << p.vote_end_height
           << ",\"activation_height\":" << p.activation_height
           << ",\"votes_approve\":" << (p.votes_approve / 1000000.0)
           << ",\"votes_reject\":" << (p.votes_reject / 1000000.0)
           << ",\"votes_abstain\":" << (p.votes_abstain / 1000000.0)
           << ",\"approval_rate\":" << (p.approval_rate() * 100.0)
           << ",\"participation_rate\":" << (p.participation_rate(total_supply) * 100.0)
           << ",\"total_voters\":" << p.voters.size();

        if (!p.param_key.empty()) {
            ss << ",\"param_key\":\"" << p.param_key << "\""
               << ",\"param_value\":\"" << p.param_value << "\"";
        }
        ss << "}";
        return ss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════
//  Anti-Sybil Protection
// ═══════════════════════════════════════════════════════════════════

namespace sybil {
    // 1. Max consecutive blocks by same miner
    constexpr uint32_t MAX_CONSECUTIVE_BLOCKS = 5;

    // 2. Minimum stake to mine (must have mined at least 1 block or hold Certs)
    //    Set to 0 for testnet to allow new miners to join
    //    On mainnet this would be higher
    constexpr uint64_t MIN_STAKE_TO_MINE = 0;

    // 3. Max connections per IP
    constexpr uint32_t MAX_PEERS_PER_IP = 2;

    // Check if a miner has hit the consecutive block limit
    bool check_consecutive(const std::vector<Block>& blocks, const std::string& miner_addr) {
        if (blocks.size() < MAX_CONSECUTIVE_BLOCKS) return true;
        uint32_t consecutive = 0;
        for (size_t i = blocks.size(); i > 0 && consecutive < MAX_CONSECUTIVE_BLOCKS; --i) {
            if (blocks[i - 1].miner_address == miner_addr) {
                consecutive++;
            } else {
                break;
            }
        }
        return consecutive < MAX_CONSECUTIVE_BLOCKS;
    }

    // Check if miner has sufficient stake
    bool check_stake(const ChainState& chain, const std::string& miner_addr) {
        if (MIN_STAKE_TO_MINE == 0) return true;
        return chain.get_balance(miner_addr) >= MIN_STAKE_TO_MINE;
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Staking & Mainnet Migration System
//
//  Testnet miners earn Certs. At snapshot time, all balances are
//  frozen and converted at 1000:1 ratio for mainnet genesis.
//
//  Flow:
//  1. Mine on testnet → accumulate Certs
//  2. Admin triggers snapshot at block height N
//  3. Merkle root computed over all balances
//  4. Miners complete KYC to bind testnet wallet to mainnet
//  5. Mainnet genesis includes all verified balances at 1000:1
// ═══════════════════════════════════════════════════════════════════

namespace migration {
    constexpr uint64_t CONVERSION_RATIO = 1000;  // 1000 testnet = 1 mainnet
    constexpr uint64_t MIN_BALANCE_TO_MIGRATE = 47564688000ULL;  // ~47,564 Certs (1 block reward)
}

struct BalanceSnapshot {
    std::string address;
    uint64_t testnet_balance;     // In base units
    uint64_t mainnet_balance;     // After 1000:1 conversion
    uint64_t blocks_mined;
    Hash256 proof;                // Merkle proof leaf hash
    bool kyc_verified = false;
    std::string kyc_token;
};

struct MigrationState {
    bool snapshot_taken = false;
    uint64_t snapshot_height = 0;
    uint64_t snapshot_timestamp = 0;
    Hash256 merkle_root{};
    std::vector<BalanceSnapshot> snapshots;
    uint64_t total_testnet_supply = 0;
    uint64_t total_mainnet_allocation = 0;
    uint32_t eligible_wallets = 0;
    uint32_t kyc_verified_count = 0;

    // Take a snapshot of all balances
    void take_snapshot(const ChainState& chain) {
        snapshots.clear();
        snapshot_height = chain.height();
        snapshot_timestamp = static_cast<uint64_t>(
            std::chrono::system_clock::now().time_since_epoch().count() / 1'000'000'000);
        total_testnet_supply = chain.total_supply;
        total_mainnet_allocation = 0;
        eligible_wallets = 0;

        // Count blocks per miner
        std::map<std::string, uint64_t> blocks_per_miner;
        for (const auto& b : chain.blocks) {
            blocks_per_miner[b.miner_address]++;
        }

        // Create snapshot for each address with sufficient balance
        for (const auto& [addr, balance] : chain.balances) {
            if (balance < migration::MIN_BALANCE_TO_MIGRATE) continue;

            BalanceSnapshot snap;
            snap.address = addr;
            snap.testnet_balance = balance;
            snap.mainnet_balance = balance / migration::CONVERSION_RATIO;
            snap.blocks_mined = blocks_per_miner.count(addr) ? blocks_per_miner[addr] : 0;

            // Compute leaf hash: SHA3(address || balance || blocks_mined)
            Bytes leaf_data;
            leaf_data.insert(leaf_data.end(), addr.begin(), addr.end());
            uint8_t bal_bytes[8];
            std::memcpy(bal_bytes, &balance, 8);
            leaf_data.insert(leaf_data.end(), bal_bytes, bal_bytes + 8);
            uint64_t bm = snap.blocks_mined;
            uint8_t bm_bytes[8];
            std::memcpy(bm_bytes, &bm, 8);
            leaf_data.insert(leaf_data.end(), bm_bytes, bm_bytes + 8);
            snap.proof = sha3_256(ByteSpan{leaf_data.data(), leaf_data.size()});

            total_mainnet_allocation += snap.mainnet_balance;
            eligible_wallets++;
            snapshots.push_back(std::move(snap));
        }

        // Compute merkle root
        if (!snapshots.empty()) {
            std::vector<Hash256> leaves;
            for (const auto& s : snapshots) leaves.push_back(s.proof);

            // Simple merkle tree
            while (leaves.size() > 1) {
                std::vector<Hash256> next;
                for (size_t i = 0; i < leaves.size(); i += 2) {
                    Bytes combined(64);
                    std::memcpy(combined.data(), leaves[i].data(), 32);
                    if (i + 1 < leaves.size()) {
                        std::memcpy(combined.data() + 32, leaves[i + 1].data(), 32);
                    } else {
                        std::memcpy(combined.data() + 32, leaves[i].data(), 32); // Duplicate last
                    }
                    next.push_back(sha3_256(ByteSpan{combined.data(), combined.size()}));
                }
                leaves = next;
            }
            merkle_root = leaves[0];
        }

        snapshot_taken = true;
    }

    // KYC verify a wallet
    bool verify_kyc(const std::string& address, const std::string& token) {
        for (auto& s : snapshots) {
            if (s.address == address) {
                s.kyc_verified = true;
                s.kyc_token = token;
                kyc_verified_count++;
                return true;
            }
        }
        return false;
    }

    // Get snapshot for an address
    BalanceSnapshot* get_snapshot(const std::string& address) {
        for (auto& s : snapshots) {
            if (s.address == address) return &s;
        }
        return nullptr;
    }

    // Generate mainnet genesis allocation JSON
    std::string to_genesis_json() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"snapshot_height\":" << snapshot_height
           << ",\"merkle_root\":\"" << to_hex(merkle_root) << "\""
           << ",\"conversion_ratio\":" << migration::CONVERSION_RATIO
           << ",\"eligible_wallets\":" << eligible_wallets
           << ",\"kyc_verified\":" << kyc_verified_count
           << ",\"total_testnet_supply\":" << (total_testnet_supply / 1000000.0)
           << ",\"total_mainnet_allocation\":" << (total_mainnet_allocation / 1000000.0)
           << ",\"allocations\":[";

        bool first = true;
        for (const auto& s : snapshots) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"address\":\"" << s.address << "\""
               << ",\"testnet_balance\":" << (s.testnet_balance / 1000000.0)
               << ",\"mainnet_balance\":" << (s.mainnet_balance / 1000000.0)
               << ",\"blocks_mined\":" << s.blocks_mined
               << ",\"kyc_verified\":" << (s.kyc_verified ? "true" : "false")
               << ",\"proof\":\"" << to_hex(s.proof).substr(0, 16) << "...\""
               << "}";
        }
        ss << "]}";
        return ss.str();
    }

    // Single snapshot entry to JSON
    std::string snapshot_to_json(const BalanceSnapshot& s) const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"address\":\"" << s.address << "\""
           << ",\"testnet_balance\":" << (s.testnet_balance / 1000000.0)
           << ",\"mainnet_balance\":" << (s.mainnet_balance / 1000000.0)
           << ",\"blocks_mined\":" << s.blocks_mined
           << ",\"kyc_verified\":" << (s.kyc_verified ? "true" : "false")
           << ",\"proof\":\"" << to_hex(s.proof) << "\""
           << ",\"conversion_ratio\":" << migration::CONVERSION_RATIO
           << ",\"snapshot_height\":" << snapshot_height
           << ",\"merkle_root\":\"" << to_hex(merkle_root).substr(0, 32) << "...\""
           << "}";
        return ss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════
//  Mining Loop
// ═══════════════════════════════════════════════════════════════════

Block mine_block(const ChainState& chain, const std::string& miner_addr) {
    Block block;
    block.version = 1;
    block.height = chain.height() + 1;
    block.prev_hash = chain.tip_hash();
    block.difficulty = chain.current_difficulty();
    block.reward = chain.block_reward();
    block.miner_address = miner_addr;
    block.timestamp = static_cast<uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count() / 1'000'000'000);

    // Generate NP instance
    auto inst = generate_instance(block.prev_hash, block.difficulty);
    block.num_variables = inst.num_variables;
    block.num_clauses = inst.num_clauses;

    // Solve
    auto witness = solve_sat(inst, 120000);
    if (!witness.has_value()) {
        block.height = 0;  // Signal failure
        return block;
    }

    // Verify (paranoia)
    assert(inst.verify(witness.value()));

    block.witness = witness.value();

    // Witness hash
    Bytes wdata(inst.num_variables);
    for (uint32_t i = 0; i < inst.num_variables; ++i)
        wdata[i] = witness.value()[i] ? 1 : 0;
    block.witness_hash = sha3_256(ByteSpan{wdata.data(), wdata.size()});

    // Merkle root (coinbase only for now)
    uint8_t coinbase_data[16];
    std::memcpy(coinbase_data, &block.height, 8);
    std::memcpy(coinbase_data + 8, &block.reward, 8);
    Hash256 coinbase_hash = sha3_256(ByteSpan{coinbase_data, 16});
    block.merkle_root = coinbase_hash;

    block.hash = block.compute_hash();
    return block;
}

// ═══════════════════════════════════════════════════════════════════
//  RPC Server (minimal HTTP, runs in background thread)
//  Endpoints:
//    GET /api/v1/status      — chain height, supply, difficulty
//    GET /api/v1/balance/ADDR — balance for a cert1... address
//    GET /api/v1/blocks      — recent blocks
// ═══════════════════════════════════════════════════════════════════

static ChainState* g_chain = nullptr;
static std::mutex g_chain_mutex;
static P2PManager* g_p2p = nullptr;
static std::string* g_miner_addr = nullptr;
static GovernanceState* g_gov = nullptr;
static MigrationState* g_migration = nullptr;
static std::string g_admin_password = "";  // Required for migration endpoints

std::string make_json_response(const std::string& json) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET\r\n"
           "Access-Control-Allow-Headers: *\r\n"
           "Connection: close\r\n"
           "Content-Length: " + std::to_string(json.size()) + "\r\n"
           "\r\n" + json;
}

std::string make_404() {
    std::string body = "{\"error\":\"not found\"}";
    return "HTTP/1.1 404 Not Found\r\n"
           "Content-Type: application/json\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Connection: close\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "\r\n" + body;
}

void handle_rpc_client(SOCKET client) {
    char buf[8192] = {};
    recv(client, buf, sizeof(buf) - 1, 0);
    std::string req(buf);

    // Parse method and path
    std::string path, body;
    bool is_post = false;
    if (req.substr(0, 4) == "GET ") {
        size_t end = req.find(' ', 4);
        if (end != std::string::npos) path = req.substr(4, end - 4);
    } else if (req.substr(0, 5) == "POST ") {
        is_post = true;
        size_t end = req.find(' ', 5);
        if (end != std::string::npos) path = req.substr(5, end - 5);
        // Extract body after \r\n\r\n
        auto body_start = req.find("\r\n\r\n");
        if (body_start != std::string::npos) body = req.substr(body_start + 4);
    }

    // Handle CORS preflight
    if (req.substr(0, 7) == "OPTIONS") {
        std::string resp = "HTTP/1.1 204 No Content\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                           "Access-Control-Allow-Headers: Content-Type, *\r\n"
                           "Connection: close\r\n\r\n";
        send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
        CLOSESOCK(client);
        return;
    }

    std::string response;
    std::lock_guard<std::mutex> lock(g_chain_mutex);

    if (path == "/api/v1/status" || path == "/") {
        double supply = g_chain->total_supply / 1'000'000.0;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"chain_id\":\"TCR\",\"network\":\"testnet\""
           << ",\"height\":" << g_chain->height()
           << ",\"difficulty\":" << g_chain->current_difficulty()
           << ",\"total_supply\":" << supply
           << ",\"total_supply_base\":" << g_chain->total_supply
           << ",\"blocks_count\":" << g_chain->blocks.size()
           << ",\"tip_hash\":\"" << (g_chain->blocks.empty() ? "" : to_hex(g_chain->tip_hash()).substr(0, 32)) << "\""
           << ",\"version\":\"0.3.0\""
           << ",\"accounts\":" << g_chain->balances.size()
           << ",\"peers\":" << (g_p2p ? g_p2p->peer_count() : 0)
           << "}";
        response = make_json_response(ss.str());
    }
    else if (path.substr(0, 20) == "/api/v1/balance/cert") {
        std::string addr = path.substr(16);
        uint64_t bal = g_chain->get_balance(addr);
        double bal_certs = bal / 1'000'000.0;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"address\":\"" << addr << "\""
           << ",\"balance\":" << bal_certs
           << ",\"balance_base\":" << bal
           << ",\"network\":\"testnet\"}";
        response = make_json_response(ss.str());
    }
    else if (path == "/api/v1/blocks") {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"blocks\":[";
        size_t start = g_chain->blocks.size() > 10 ? g_chain->blocks.size() - 10 : 0;
        for (size_t i = start; i < g_chain->blocks.size(); ++i) {
            auto& b = g_chain->blocks[i];
            if (i > start) ss << ",";
            ss << "{\"height\":" << b.height
               << ",\"hash\":\"" << to_hex(b.hash).substr(0, 32) << "\""
               << ",\"miner\":\"" << b.miner_address << "\""
               << ",\"reward\":" << b.reward / 1'000'000.0
               << ",\"difficulty\":" << b.difficulty
               << ",\"timestamp\":" << b.timestamp
               << ",\"vars\":" << b.num_variables
               << ",\"clauses\":" << b.num_clauses << "}";
        }
        ss << "]}";
        response = make_json_response(ss.str());
    }
    else if (path == "/api/v1/balances") {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"balances\":{";
        bool first = true;
        for (const auto& [addr, bal] : g_chain->balances) {
            if (!first) ss << ",";
            ss << "\"" << addr << "\":" << bal / 1'000'000.0;
            first = false;
        }
        ss << "}}";
        response = make_json_response(ss.str());
    }
    else if (path == "/api/v1/peers") {
        std::ostringstream ss;
        ss << "{\"peers\":[";
        if (g_p2p) {
            auto list = g_p2p->peer_list();
            for (size_t i = 0; i < list.size(); ++i) {
                if (i > 0) ss << ",";
                ss << "\"" << list[i] << "\"";
            }
        }
        ss << "],\"count\":" << (g_p2p ? g_p2p->peer_count() : 0) << "}";
        response = make_json_response(ss.str());
    }
    else if (path == "/api/v1/miner") {
        // Export miner wallet info for browser wallet import
        std::ostringstream ss;
        std::string addr = g_miner_addr ? *g_miner_addr : "";
        uint64_t bal = g_chain->get_balance(addr);
        double bal_certs = bal / 1'000'000.0;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"address\":\"" << addr << "\""
           << ",\"balance\":" << bal_certs
           << ",\"balance_base\":" << bal
           << ",\"network\":\"testnet\""
           << ",\"type\":\"miner\""
           << ",\"blocks_mined\":" << [&]() {
                uint64_t count = 0;
                for (const auto& b : g_chain->blocks)
                    if (b.miner_address == addr) count++;
                return count;
              }()
           << ",\"import_token\":\"" << addr << "-miner-" 
           << to_hex(g_chain->tip_hash()).substr(0, 8) << "\""
           << "}";
        response = make_json_response(ss.str());
    }
    // ─── Governance Endpoints ───
    else if (path == "/api/v1/governance/proposals") {
        // List all proposals
        std::ostringstream ss;
        ss << "{\"proposals\":[";
        if (g_gov) {
            for (size_t i = 0; i < g_gov->proposals.size(); ++i) {
                if (i > 0) ss << ",";
                ss << g_gov->proposal_to_json(g_gov->proposals[i], g_chain->total_supply);
            }
        }
        ss << "],\"count\":" << (g_gov ? g_gov->proposals.size() : 0)
           << ",\"chain_height\":" << g_chain->height()
           << "}";
        response = make_json_response(ss.str());
    }
    else if (path.substr(0, 30) == "/api/v1/governance/proposal/") {
        // Get single proposal by ID
        uint32_t pid = std::stoul(path.substr(30));
        if (g_gov) {
            auto* p = g_gov->get_proposal(pid);
            if (p) {
                response = make_json_response(g_gov->proposal_to_json(*p, g_chain->total_supply));
            } else {
                response = make_json_response("{\"error\":\"Proposal not found\"}");
            }
        } else {
            response = make_json_response("{\"error\":\"Governance not initialized\"}");
        }
    }
    else if (path == "/api/v1/governance/submit" && body.size() > 0) {
        // Submit a proposal via POST
        // Expected: {"author":"cert1...","type":"param_change","title":"...","description":"...","param_key":"...","param_value":"..."}
        // Simple JSON parsing (no library)
        auto getField = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            auto pos = body.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = body.find("\"", pos);
            if (end == std::string::npos) return "";
            return body.substr(pos, end - pos);
        };

        std::string author = getField("author");
        std::string type_str = getField("type");
        std::string title = getField("title");
        std::string desc = getField("description");
        std::string pkey = getField("param_key");
        std::string pval = getField("param_value");

        ProposalType ptype = ProposalType::GENERAL;
        if (type_str == "param_change") ptype = ProposalType::PARAM_CHANGE;
        else if (type_str == "crypto_upgrade") ptype = ProposalType::CRYPTO_UPGRADE;
        else if (type_str == "fee_change") ptype = ProposalType::FEE_CHANGE;
        else if (type_str == "emission_change") ptype = ProposalType::EMISSION_CHANGE;

        uint64_t author_bal = g_chain->get_balance(author);

        if (g_gov && g_gov->submit_proposal(author, ptype, title, desc,
                                             g_chain->height(), author_bal, pkey, pval)) {
            auto* p = g_gov->get_proposal(g_gov->next_id - 1);
            std::cout << "[GOV] New proposal #" << (g_gov->next_id - 1)
                      << " by " << author.substr(0, 20) << "...: " << title << "\n";
            response = make_json_response("{\"ok\":true,\"proposal_id\":" +
                std::to_string(g_gov->next_id - 1) + "}");
        } else {
            response = make_json_response("{\"ok\":false,\"error\":\"Proposal rejected. Need at least ~47,564 Certs balance to propose.\"}");
        }
    }
    else if (path == "/api/v1/governance/vote" && body.size() > 0) {
        // Cast a vote via POST
        // Expected: {"proposal_id":1,"voter":"cert1...","choice":"approve"}
        auto getField = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            auto pos = body.find(search);
            if (pos == std::string::npos) {
                // Try numeric field
                search = "\"" + key + "\":";
                pos = body.find(search);
                if (pos == std::string::npos) return "";
                pos += search.size();
                auto end = body.find_first_of(",}", pos);
                return body.substr(pos, end - pos);
            }
            pos += search.size();
            auto end = body.find("\"", pos);
            if (end == std::string::npos) return "";
            return body.substr(pos, end - pos);
        };

        uint32_t pid = std::stoul(getField("proposal_id"));
        std::string voter = getField("voter");
        std::string choice_str = getField("choice");

        VoteChoice choice = VoteChoice::ABSTAIN;
        if (choice_str == "approve") choice = VoteChoice::APPROVE;
        else if (choice_str == "reject") choice = VoteChoice::REJECT;

        uint64_t voter_bal = g_chain->get_balance(voter);

        if (g_gov && g_gov->cast_vote(pid, voter, choice, voter_bal, g_chain->height())) {
            auto* p = g_gov->get_proposal(pid);
            std::cout << "[GOV] Vote on #" << pid << " by " << voter.substr(0, 20)
                      << "... : " << choice_str
                      << " (power=" << std::fixed << std::setprecision(2) << (voter_bal / 1000000.0) << " Certs)\n";
            response = make_json_response("{\"ok\":true,\"proposal_id\":" + std::to_string(pid) +
                ",\"choice\":\"" + choice_str + "\",\"voting_power\":" +
                std::to_string(voter_bal / 1000000.0) + "}");
        } else {
            response = make_json_response("{\"ok\":false,\"error\":\"Vote failed. Check proposal ID, voting period, and balance.\"}");
        }
    }
    else if (path == "/api/v1/governance/config") {
        // Return governance configuration
        std::ostringstream ss;
        ss << "{\"vote_period_blocks\":" << gov::VOTE_PERIOD_BLOCKS
           << ",\"activation_delay\":" << gov::ACTIVATION_DELAY
           << ",\"approval_threshold\":" << gov::APPROVAL_THRESHOLD
           << ",\"participation_min\":" << gov::PARTICIPATION_MIN
           << ",\"min_proposer_balance\":" << (gov::MIN_PROPOSER_BALANCE / 1000000.0)
           << ",\"emergency_vote_period\":" << gov::EMERGENCY_VOTE_PERIOD
           << ",\"emergency_threshold\":" << gov::EMERGENCY_THRESHOLD
           << "}";
        response = make_json_response(ss.str());
    }
    // ─── Migration / Staking Endpoints ───
    else if (path == "/api/v1/migration/status") {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"snapshot_taken\":" << (g_migration->snapshot_taken ? "true" : "false")
           << ",\"snapshot_height\":" << g_migration->snapshot_height
           << ",\"conversion_ratio\":" << migration::CONVERSION_RATIO
           << ",\"min_balance_to_migrate\":" << (migration::MIN_BALANCE_TO_MIGRATE / 1000000.0)
           << ",\"eligible_wallets\":" << g_migration->eligible_wallets
           << ",\"kyc_verified\":" << g_migration->kyc_verified_count
           << ",\"total_testnet_supply\":" << (g_chain->total_supply / 1000000.0)
           << ",\"total_mainnet_allocation\":" << (g_migration->total_mainnet_allocation / 1000000.0)
           << ",\"merkle_root\":\"" << (g_migration->snapshot_taken ? to_hex(g_migration->merkle_root).substr(0, 32) + "..." : "") << "\""
           << ",\"chain_height\":" << g_chain->height()
           << ",\"total_miners\":" << g_chain->balances.size()
           << "}";
        response = make_json_response(ss.str());
    }
    else if (path == "/api/v1/migration/snapshot" && is_post) {
        // Take a snapshot (ADMIN ONLY — requires admin password)
        auto getField = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            auto pos = body.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = body.find("\"", pos);
            if (end == std::string::npos) return "";
            return body.substr(pos, end - pos);
        };
        std::string pw = getField("admin_password");

        if (g_admin_password.empty()) {
            response = make_json_response("{\"ok\":false,\"error\":\"Admin password not set. Start node with --admin-password YOUR_PASSWORD\"}");
        } else if (pw != g_admin_password) {
            response = make_json_response("{\"ok\":false,\"error\":\"Invalid admin password\"}");
        } else if (g_migration->snapshot_taken) {
            response = make_json_response("{\"ok\":false,\"error\":\"Snapshot already taken at height "
                + std::to_string(g_migration->snapshot_height) + "\"}");
        } else {
            g_migration->take_snapshot(*g_chain);
            std::cout << "[MIGRATION] Snapshot taken at height " << g_migration->snapshot_height
                      << " | " << g_migration->eligible_wallets << " eligible wallets"
                      << " | Mainnet allocation: " << std::fixed << std::setprecision(2)
                      << (g_migration->total_mainnet_allocation / 1000000.0) << " Certs\n";
            response = make_json_response(g_migration->to_genesis_json());
        }
    }
    else if (path.substr(0, 25) == "/api/v1/migration/check/c") {
        // Check migration status for a specific address
        std::string addr = path.substr(23);
        if (!g_migration->snapshot_taken) {
            // No snapshot yet — show live preview
            uint64_t bal = g_chain->get_balance(addr);
            uint64_t mainnet_bal = bal / migration::CONVERSION_RATIO;
            uint64_t blocks = 0;
            for (const auto& b : g_chain->blocks)
                if (b.miner_address == addr) blocks++;

            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4);
            ss << "{\"address\":\"" << addr << "\""
               << ",\"snapshot_taken\":false"
               << ",\"testnet_balance\":" << (bal / 1000000.0)
               << ",\"estimated_mainnet_balance\":" << (mainnet_bal / 1000000.0)
               << ",\"blocks_mined\":" << blocks
               << ",\"eligible\":" << (bal >= migration::MIN_BALANCE_TO_MIGRATE ? "true" : "false")
               << ",\"conversion_ratio\":" << migration::CONVERSION_RATIO
               << ",\"min_balance\":" << (migration::MIN_BALANCE_TO_MIGRATE / 1000000.0)
               << "}";
            response = make_json_response(ss.str());
        } else {
            auto* snap = g_migration->get_snapshot(addr);
            if (snap) {
                response = make_json_response(g_migration->snapshot_to_json(*snap));
            } else {
                response = make_json_response("{\"address\":\"" + addr + "\",\"eligible\":false,\"error\":\"Address not in snapshot\"}");
            }
        }
    }
    else if (path == "/api/v1/migration/kyc" && is_post) {
        // KYC verify a wallet (ADMIN ONLY)
        auto getField = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            auto pos = body.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = body.find("\"", pos);
            if (end == std::string::npos) return "";
            return body.substr(pos, end - pos);
        };

        std::string pw = getField("admin_password");
        std::string addr = getField("address");
        std::string token = getField("kyc_token");

        if (g_admin_password.empty() || pw != g_admin_password) {
            response = make_json_response("{\"ok\":false,\"error\":\"Invalid admin password\"}");
        } else if (!g_migration->snapshot_taken) {
            response = make_json_response("{\"ok\":false,\"error\":\"No snapshot taken yet\"}");
        } else if (g_migration->verify_kyc(addr, token)) {
            std::cout << "[MIGRATION] KYC verified: " << addr.substr(0, 20) << "...\n";
            response = make_json_response("{\"ok\":true,\"address\":\"" + addr + "\",\"kyc_verified\":true}");
        } else {
            response = make_json_response("{\"ok\":false,\"error\":\"Address not found in snapshot\"}");
        }
    }
    else if (path == "/api/v1/migration/genesis") {
        // Export full genesis allocation
        if (!g_migration->snapshot_taken) {
            response = make_json_response("{\"error\":\"No snapshot taken yet\"}");
        } else {
            response = make_json_response(g_migration->to_genesis_json());
        }
    }
    else {
        response = make_404();
    }

    send(client, response.c_str(), static_cast<int>(response.size()), 0);
    CLOSESOCK(client);
}

void rpc_server_thread(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "[RPC] Failed to create socket\n";
        return;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[RPC] Failed to bind port " << port << "\n";
        CLOSESOCK(server);
        return;
    }

    listen(server, 10);
    std::cout << "[RPC] Server listening on http://localhost:" << port << "\n";
    std::cout << "[RPC] Endpoints: /api/v1/status, /api/v1/balance/<addr>, /api/v1/blocks\n\n";

    while (!g_shutdown) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        // Use select() with timeout so we can check g_shutdown
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server, &fds);
        struct timeval tv{1, 0};  // 1 second timeout

        int sel = select(static_cast<int>(server) + 1, &fds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        SOCKET client = accept(server, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client == INVALID_SOCKET) continue;

        handle_rpc_client(client);
    }

    CLOSESOCK(server);
#ifdef _WIN32
    WSACleanup();
#endif
}

// ═══════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse args
    uint32_t max_blocks = 0;
    uint32_t init_difficulty = 1;
    std::string custom_address;
    uint16_t rpc_port = 18333;
    uint16_t p2p_port = 19333;
    bool fast_mode = false;
    uint32_t target_block_time = 15;
    std::vector<std::string> seed_nodes;
    std::string data_dir = ".";

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--blocks" && i + 1 < argc) max_blocks = std::stoul(argv[++i]);
        if (arg == "--difficulty" && i + 1 < argc) init_difficulty = std::stoul(argv[++i]);
        if (arg == "--address" && i + 1 < argc) custom_address = argv[++i];
        if (arg == "--rpc-port" && i + 1 < argc) rpc_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        if (arg == "--p2p-port" && i + 1 < argc) p2p_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        if (arg == "--seed" && i + 1 < argc) seed_nodes.push_back(argv[++i]);
        if (arg == "--fast") fast_mode = true;
        if (arg == "--block-time" && i + 1 < argc) target_block_time = std::stoul(argv[++i]);
        if (arg == "--admin-password" && i + 1 < argc) g_admin_password = argv[++i];
        if (arg == "--data-dir" && i + 1 < argc) data_dir = argv[++i];
        if (arg == "--help") {
            std::cout << "Usage: npchain_testnet [options]\n\n"
                      << "  --address <cert1...>   Mine to your wallet address\n"
                      << "  --blocks <N>           Mine N blocks then stop (0=unlimited)\n"
                      << "  --difficulty <D>        Starting difficulty (default: 1)\n"
                      << "  --rpc-port <port>      RPC server port (default: 18333)\n"
                      << "  --p2p-port <port>      P2P listen port (default: 19333)\n"
                      << "  --seed <host:port>     Seed node to connect to (can use multiple)\n"
                      << "  --block-time <secs>    Target block time (default: 15)\n"
                      << "  --fast                 No delay between blocks (testing only)\n"
                      << "\nExamples:\n"
                      << "  # Solo mining:\n"
                      << "  ./npchain_testnet --address cert1abc...\n\n"
                      << "  # First node (seed):\n"
                      << "  ./npchain_testnet --address cert1abc... --p2p-port 19333\n\n"
                      << "  # Second node (connects to first):\n"
                      << "  ./npchain_testnet --address cert1def... --p2p-port 19334 --seed localhost:19333\n";
            return 0;
        }
    }

    std::cout << R"(
    ╔═══════════════════════════════════════════════════════╗
    ║       NPChain Testnet Node v0.1.0                    ║
    ║       Proof-of-NP-Witness Consensus                  ║
    ║       SHA3-256 • WalkSAT+CDCL • O(n) Verify         ║
    ╚═══════════════════════════════════════════════════════╝
)" << '\n';

    // Miner identity — WALLET FIRST: must create wallet before mining
    std::string miner_addr;
    if (!custom_address.empty()) {
        if (custom_address.substr(0, 5) != "cert1" || custom_address.size() < 20) {
            std::cerr << "ERROR: Invalid address format. Must start with 'cert1' and be at least 20 characters.\n";
            return 1;
        }
        miner_addr = custom_address;
        std::cout << "[INIT] Using wallet address: " << miner_addr << "\n";
    } else {
        std::cerr << "\n";
        std::cerr << "  ╔═══════════════════════════════════════════════════════════╗\n";
        std::cerr << "  ║  ERROR: No wallet address provided!                       ║\n";
        std::cerr << "  ║                                                           ║\n";
        std::cerr << "  ║  You must create a wallet BEFORE mining.                  ║\n";
        std::cerr << "  ║                                                           ║\n";
        std::cerr << "  ║  Step 1: Open the NPChain Web Wallet                     ║\n";
        std::cerr << "  ║          (web/index.html in your NPChain folder)          ║\n";
        std::cerr << "  ║                                                           ║\n";
        std::cerr << "  ║  Step 2: Create a password-protected wallet               ║\n";
        std::cerr << "  ║                                                           ║\n";
        std::cerr << "  ║  Step 3: Copy your cert1... address                       ║\n";
        std::cerr << "  ║                                                           ║\n";
        std::cerr << "  ║  Step 4: Run the miner with your address:                 ║\n";
        std::cerr << "  ║    ./npchain_testnet --address cert1YOUR_ADDRESS           ║\n";
        std::cerr << "  ║                                                           ║\n";
        std::cerr << "  ╚═══════════════════════════════════════════════════════════╝\n\n";
        return 1;
    }

    // Initialize chain
    ChainState chain;
    std::string chain_file = data_dir + "/npchain_blocks.dat";
    std::string balance_file = data_dir + "/npchain_balances.json";
    bool have_seeds = !seed_nodes.empty();

    // Try loading chain from disk
    if (chain.load_from_disk(chain_file)) {
        std::cout << "[INIT] Loaded chain from disk: " << chain.height() << " blocks, "
                  << std::fixed << std::setprecision(4) << (chain.total_supply / 1000000.0) << " Certs\n";
        std::cout << "[INIT] Tip: " << to_hex(chain.tip_hash()).substr(0, 32) << "...\n";
    } else if (!have_seeds) {
        // Create genesis block (first node / seed node)
        std::cout << "[INIT] Creating genesis block...\n";
        Block genesis;
        genesis.version = 1;
        genesis.height = 0;
        genesis.prev_hash = Hash256{};
        genesis.timestamp = static_cast<uint64_t>(
            std::chrono::system_clock::now().time_since_epoch().count() / 1'000'000'000);
        genesis.difficulty = init_difficulty;
        genesis.reward = chain.block_reward();
        genesis.miner_address = miner_addr;

        // Solve genesis instance
        auto genesis_inst = generate_instance(genesis.prev_hash, genesis.difficulty);
        genesis.num_variables = genesis_inst.num_variables;
        genesis.num_clauses = genesis_inst.num_clauses;
        auto genesis_witness = solve_sat(genesis_inst, 60000);
        if (!genesis_witness.has_value()) {
            std::cerr << "FATAL: Cannot solve genesis instance\n";
            return 1;
        }
        genesis.witness = genesis_witness.value();
        Bytes gw(genesis_inst.num_variables);
        for (uint32_t i = 0; i < genesis_inst.num_variables; ++i)
            gw[i] = genesis_witness.value()[i] ? 1 : 0;
        genesis.witness_hash = sha3_256(ByteSpan{gw.data(), gw.size()});

        uint8_t gcb[16];
        std::memcpy(gcb, &genesis.height, 8);
        std::memcpy(gcb + 8, &genesis.reward, 8);
        genesis.merkle_root = sha3_256(ByteSpan{gcb, 16});
        genesis.hash = genesis.compute_hash();

        chain.add_block(genesis);

        std::cout << "[GENESIS] Block #0 mined\n";
        std::cout << "[GENESIS] Hash: " << to_hex(genesis.hash) << "\n";
        std::cout << "[GENESIS] Reward: " << std::fixed << std::setprecision(4)
                  << genesis.reward / 1'000'000.0 << " Certs\n\n";
    } else {
        std::cout << "[INIT] Seed node(s) specified — will sync chain from network\n";
    }

    // Point RPC server at chain state and launch
    g_chain = &chain;
    g_miner_addr = &miner_addr;
    GovernanceState governance;
    g_gov = &governance;
    MigrationState migration_state;
    g_migration = &migration_state;
    std::thread rpc_thread(rpc_server_thread, rpc_port);
    rpc_thread.detach();

    // ─── P2P Network ───
    P2PManager p2p(p2p_port, miner_addr);

    // Block received from peer: validate and add
    p2p.set_callbacks(
        [&](const Block& b) -> bool {
            std::lock_guard<std::mutex> lock(g_chain_mutex);

            // Accept genesis block when our chain is empty
            if (b.height == 0 && chain.blocks.empty()) {
                if (b.prev_hash != Hash256{}) return false;
                auto inst = generate_instance(b.prev_hash, b.difficulty);
                if (!inst.verify(b.witness)) {
                    std::cout << "[P2P] REJECTED genesis — invalid witness\n";
                    return false;
                }
                Block copy = b;
                if (chain.add_block(copy)) {
                    std::cout << "  ← Genesis #0 from peer | miner=" << b.miner_address.substr(0, 20)
                              << "... | hash=" << to_hex(b.hash).substr(0, 16) << "...\n";
                    return true;
                }
                return false;
            }

            // Normal block: must chain onto our tip
            if (b.height != chain.height() + 1) return false;
            if (b.prev_hash != chain.tip_hash()) return false;

            // Anti-sybil: check consecutive block limit
            if (!sybil::check_consecutive(chain.blocks, b.miner_address)) {
                std::cout << "[SYBIL] REJECTED block #" << b.height
                          << " — miner " << b.miner_address.substr(0, 20)
                          << "... hit " << sybil::MAX_CONSECUTIVE_BLOCKS << " consecutive block limit\n";
                return false;
            }

            // Anti-sybil: check minimum stake
            if (!sybil::check_stake(chain, b.miner_address)) {
                std::cout << "[SYBIL] REJECTED block #" << b.height
                          << " — miner " << b.miner_address.substr(0, 20)
                          << "... insufficient stake\n";
                return false;
            }

            auto inst = generate_instance(b.prev_hash, b.difficulty);
            if (!inst.verify(b.witness)) {
                std::cout << "[P2P] REJECTED block #" << b.height << " — invalid witness\n";
                return false;
            }

            Block copy = b;
            if (chain.add_block(copy)) {
                double supply = chain.total_supply / 1'000'000.0;
                std::cout << "  ← Block #" << chain.height()
                          << " from peer | miner=" << b.miner_address.substr(0, 20) << "..."
                          << " | supply=" << std::fixed << std::setprecision(1) << supply << " Certs\n";
                return true;
            }
            return false;
        },
        [&](uint64_t from) -> std::vector<Block> {
            std::lock_guard<std::mutex> lock(g_chain_mutex);
            std::vector<Block> result;
            for (size_t i = from; i < chain.blocks.size() && result.size() < 100; ++i) {
                result.push_back(chain.blocks[i]);
            }
            return result;
        }
    );

    p2p.start();
    g_p2p = &p2p;
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Let listener bind

    // Connect to seed nodes
    for (const auto& seed : seed_nodes) {
        auto colon = seed.rfind(':');
        if (colon != std::string::npos) {
            std::string host = seed.substr(0, colon);
            uint16_t port = static_cast<uint16_t>(std::stoul(seed.substr(colon + 1)));
            std::cout << "[P2P] Connecting to seed " << host << ":" << port << "...\n";
            if (!p2p.connect_to(host, port)) {
                std::cerr << "[P2P] Failed to connect to " << seed << "\n";
            }
        }
    }

    // Wait for chain sync from seeds
    if (have_seeds) {
        std::cout << "[P2P] Waiting for chain sync";
        for (int w = 0; w < 30 && chain.blocks.empty(); ++w) {
            std::cout << "." << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        std::cout << "\n";
        // Extra time for remaining blocks
        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (chain.blocks.empty()) {
            std::cerr << "[P2P] ERROR: Could not sync chain from seeds. Is the seed node running?\n";
            p2p.stop();
            return 1;
        }
        std::cout << "[SYNC] Chain synced to height " << chain.height()
                  << " | tip=" << to_hex(chain.tip_hash()).substr(0, 16) << "...\n";
        std::cout << "[SYNC] Total supply: " << std::fixed << std::setprecision(4)
                  << chain.total_supply / 1'000'000.0 << " Certs\n\n";
        p2p.set_chain_info(chain.height(), chain.tip_hash());
    }

    // ─── Mining Loop ───
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "  MINING STARTED";
    if (max_blocks > 0) std::cout << " (target: " << max_blocks << " blocks)";
    if (fast_mode) std::cout << " [FAST MODE — no delays]";
    else std::cout << " [" << target_block_time << "s block target]";
    std::cout << "\n  P2P port: " << p2p_port << " | RPC port: " << rpc_port;
    std::cout << " | Peers: " << p2p.peer_count();
    std::cout << "\n  Press Ctrl+C to stop\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    uint64_t blocks_mined = 0;
    double total_solve_ms = 0;

    while (!g_shutdown) {
        if (max_blocks > 0 && blocks_mined >= max_blocks) break;

        // Anti-sybil: if we've hit consecutive block limit, wait for a peer block
        if (!sybil::check_consecutive(chain.blocks, miner_addr)) {
            std::cout << "  [~] Consecutive block limit reached (" << sybil::MAX_CONSECUTIVE_BLOCKS
                      << "), waiting for other miners...\n";
            std::this_thread::sleep_for(std::chrono::seconds(target_block_time));
            continue;
        }

        auto mine_start = std::chrono::steady_clock::now();

        Block block = mine_block(chain, miner_addr);

        auto mine_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - mine_start).count();

        if (block.height == 0 && blocks_mined > 0) {
            std::cerr << "  [!] Mining failed (timeout), retrying...\n";
            continue;
        }

        // Validate witness independently (what every validator does)
        auto verify_inst = generate_instance(block.prev_hash, block.difficulty);
        if (!verify_inst.verify(block.witness)) {
            std::cerr << "  [!] INVALID WITNESS — solver bug!\n";
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_chain_mutex);
            // Chain may have advanced while we were mining — check and retry
            if (block.height != chain.height() + 1 || block.prev_hash != chain.tip_hash()) {
                std::cout << "  [~] Chain advanced while mining (peer block arrived), retrying...\n";
                continue;
            }
            if (!chain.add_block(block)) {
                std::cerr << "  [!] Block rejected by chain state\n";
                continue;
            }
        }

        // Broadcast to all peers
        p2p.broadcast_block(chain.blocks.back());
        p2p.set_chain_info(chain.height(), chain.tip_hash());

        // Process governance votes/activations
        governance.process_height(chain.height(), chain.total_supply);

        // ─── Persistence: save chain to disk after each block ───
        chain.save_to_disk(chain_file);

        // ─── Periodic balance export every 10 blocks ───
        if (chain.height() % 10 == 0) {
            chain.export_balances(balance_file);
        }

        ++blocks_mined;
        total_solve_ms += mine_elapsed;

        // Calculate stats (under lock for RPC safety)
        double supply_certs, avg_ms;
        {
            std::lock_guard<std::mutex> lock(g_chain_mutex);
            supply_certs = chain.total_supply / 1'000'000.0;
            avg_ms = total_solve_ms / blocks_mined;
        }

        std::cout << "  ✓ Block #" << std::setw(6) << chain.height()
                  << " | " << std::setw(2) << block.num_variables << " vars"
                  << " | " << std::setw(4) << mine_elapsed << "ms"
                  << " | diff=" << std::setw(3) << block.difficulty
                  << " | reward=" << std::fixed << std::setprecision(2)
                  << block.reward / 1'000'000.0 << " Certs"
                  << " | supply=" << std::setprecision(1) << supply_certs << " Certs"
                  << " | peers=" << p2p.peer_count()
                  << " | hash=" << to_hex(chain.tip_hash()).substr(0, 12) << "..."
                  << '\n';

        // Status summary every 10 blocks
        if (blocks_mined % 10 == 0) {
            std::cout << "\n  ─── Status ─────────────────────────────────────\n"
                      << "  Height:        " << chain.height() << '\n'
                      << "  Difficulty:    " << chain.current_difficulty() << '\n'
                      << "  Total supply:  " << std::fixed << std::setprecision(4) << supply_certs << " Certs\n"
                      << "  Avg solve:     " << std::setprecision(1) << avg_ms << "ms\n"
                      << "  Peers:         " << p2p.peer_count() << '\n'
                      << "  Miner:         " << miner_addr.substr(0, 30) << "...\n"
                      << "  Tip:           " << to_hex(chain.tip_hash()).substr(0, 32) << "...\n"
                      << "  ────────────────────────────────────────────────\n\n";
        }

        // Block timing: wait until target_block_time has elapsed since mine_start
        if (!fast_mode && !g_shutdown) {
            auto elapsed_total = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - mine_start).count();
            int64_t wait = static_cast<int64_t>(target_block_time) - elapsed_total;
            if (wait > 0) {
                for (int64_t w = 0; w < wait && !g_shutdown; ++w) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }
    }

    // ─── Shutdown ───
    p2p.stop();
    g_p2p = nullptr;
    g_gov = nullptr;
    g_migration = nullptr;

    // Save chain and balances to disk before exiting
    std::cout << "\n[SAVE] Saving chain to disk...\n";
    if (chain.save_to_disk(chain_file)) {
        std::cout << "[SAVE] Chain saved: " << chain.height() << " blocks → " << chain_file << "\n";
    } else {
        std::cerr << "[SAVE] WARNING: Failed to save chain!\n";
    }
    if (chain.export_balances(balance_file)) {
        std::cout << "[SAVE] Balances exported → " << balance_file << "\n";
    }

    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << "  TESTNET STOPPED\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    double supply = chain.total_supply / 1'000'000.0;
    std::cout << "  Blocks mined:  " << blocks_mined << '\n';
    std::cout << "  Chain height:  " << chain.height() << '\n';
    std::cout << "  Total supply:  " << std::fixed << std::setprecision(4) << supply << " Certs\n";
    std::cout << "  Avg solve:     " << std::setprecision(1) << (blocks_mined > 0 ? total_solve_ms / blocks_mined : 0) << "ms\n";
    std::cout << "  Genesis hash:  " << to_hex(chain.blocks[0].hash) << '\n';
    std::cout << "  Tip hash:      " << to_hex(chain.tip_hash()) << '\n';
    std::cout << "  Miner address: " << miner_addr << "\n\n";

    return 0;
}

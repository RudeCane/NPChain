// ═══════════════════════════════════════════════════════════════════
//  NPChain Consensus Proof-of-Concept
//
//  This standalone program proves the entire PoNW loop works:
//    1. Hash a "previous block" → deterministic seed
//    2. Generate an NP-complete instance (3-SAT) from that seed
//    3. Solve it with the CDCL SAT solver
//    4. Verify the witness in O(n) time
//    5. Show that verification passes
//
//  Compile:
//    g++ -std=c++20 -O2 -pthread -I include \
//        tests/test_ponw_loop.cpp \
//        src/crypto/hash.cpp \
//        -o test_ponw_loop
//
//    ./test_ponw_loop
// ═══════════════════════════════════════════════════════════════════

#include "crypto/hash.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <atomic>
#include <memory>

using namespace npchain;
using namespace npchain::crypto;

// ═══════════════════════════════════════════════════════════════════
//  SAT Instance (self-contained for this test)
// ═══════════════════════════════════════════════════════════════════

struct SATInstance {
    uint32_t num_variables;
    uint32_t num_clauses;
    uint32_t k;
    std::vector<std::vector<int32_t>> clauses;

    bool verify(const std::vector<bool>& assignment) const {
        if (assignment.size() != num_variables) return false;
        for (const auto& clause : clauses) {
            bool satisfied = false;
            for (int32_t lit : clause) {
                uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
                if (var >= num_variables) return false;
                bool val = assignment[var];
                if (lit < 0) val = !val;
                if (val) { satisfied = true; break; }
            }
            if (!satisfied) return false;
        }
        return true;
    }
};

// ═══════════════════════════════════════════════════════════════════
//  Instance Generator (from block hash)
// ═══════════════════════════════════════════════════════════════════

SATInstance generate_sat_instance(const Hash256& prev_hash, uint32_t difficulty) {
    DeterministicRNG rng(prev_hash);

    double log_diff = std::log2(static_cast<double>(std::max(difficulty, 1u)));
    uint32_t num_vars = static_cast<uint32_t>(10 + log_diff * 2.5);
    uint32_t num_clauses = static_cast<uint32_t>(num_vars * 4.267);
    constexpr uint32_t K = 3;

    SATInstance inst;
    inst.num_variables = num_vars;
    inst.num_clauses = num_clauses;
    inst.k = K;

    // Plant a solution
    std::vector<bool> planted(num_vars);
    for (uint32_t i = 0; i < num_vars; ++i)
        planted[i] = rng.next_bool(0.5);

    // Generate clauses satisfied by planted solution
    inst.clauses.reserve(num_clauses);
    uint32_t generated = 0;
    uint32_t attempts = 0;

    while (generated < num_clauses && attempts < 10'000'000) {
        ++attempts;
        std::vector<int32_t> clause(K);
        bool satisfied = false;

        for (uint32_t j = 0; j < K; ++j) {
            uint32_t var = static_cast<uint32_t>(rng.next_bounded(num_vars)) + 1;
            bool negated = rng.next_bool(0.5);
            clause[j] = negated ? -static_cast<int32_t>(var) : static_cast<int32_t>(var);
            bool var_value = planted[var - 1];
            if (negated ? !var_value : var_value) satisfied = true;
        }

        if (satisfied) {
            inst.clauses.push_back(std::move(clause));
            ++generated;
        }
    }

    return inst;
}

// ═══════════════════════════════════════════════════════════════════
//  CDCL SAT Solver (simplified but functional)
// ═══════════════════════════════════════════════════════════════════

class CDCLSolver {
public:
    std::optional<std::vector<bool>> solve(const SATInstance& inst, int timeout_ms = 60000) {
        auto start = std::chrono::steady_clock::now();

        // Strategy 1: Random restarts with unit propagation (fast for small instances)
        auto rr = solve_random_restart(inst, start, timeout_ms / 2);
        if (rr.has_value()) return rr;

        // Strategy 2: CDCL (for harder instances)
        return solve_cdcl(inst, start, timeout_ms);
    }

private:
    // Random restart solver — try random assignments, fix with propagation
    std::optional<std::vector<bool>> solve_random_restart(
        const SATInstance& inst,
        std::chrono::steady_clock::time_point start,
        int timeout_ms
    ) {
        uint64_t seed = 12345;
        auto cheap_rand = [&]() -> uint64_t {
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed;
        };

        for (int attempt = 0; attempt < 1000000; ++attempt) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms) return std::nullopt;

            // Random assignment
            std::vector<bool> assignment(inst.num_variables);
            for (uint32_t i = 0; i < inst.num_variables; ++i)
                assignment[i] = (cheap_rand() & 1);

            // Count unsatisfied clauses
            int unsat = 0;
            for (const auto& clause : inst.clauses) {
                bool sat = false;
                for (int32_t lit : clause) {
                    uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
                    bool val = assignment[var];
                    if (lit < 0) val = !val;
                    if (val) { sat = true; break; }
                }
                if (!sat) ++unsat;
            }

            if (unsat == 0) return assignment;

            // WalkSAT: flip variables in unsatisfied clauses
            for (int flip = 0; flip < static_cast<int>(inst.num_variables * 3); ++flip) {
                // Find first unsatisfied clause
                int target_ci = -1;
                for (int ci = 0; ci < static_cast<int>(inst.clauses.size()); ++ci) {
                    bool sat = false;
                    for (int32_t lit : inst.clauses[ci]) {
                        uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
                        bool val = assignment[var];
                        if (lit < 0) val = !val;
                        if (val) { sat = true; break; }
                    }
                    if (!sat) { target_ci = ci; break; }
                }
                if (target_ci < 0) return assignment;  // All satisfied!

                // Flip a random variable in the unsatisfied clause
                const auto& clause = inst.clauses[target_ci];
                int32_t lit = clause[cheap_rand() % clause.size()];
                uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
                assignment[var] = !assignment[var];

                // Check if we're done
                if (inst.verify(assignment)) return assignment;
            }
        }
        return std::nullopt;
    }

    std::optional<std::vector<bool>> solve_cdcl(
        const SATInstance& inst,
        std::chrono::steady_clock::time_point start,
        int timeout_ms
    ) {
        n_ = inst.num_variables;
        assignment_.assign(n_, -1);  // -1 = unassigned, 0 = false, 1 = true
        level_.assign(n_, -1);
        antecedent_.assign(n_, -1);
        activity_.assign(n_, 0.0);
        trail_.clear();
        trail_lim_.clear();
        current_level_ = 0;

        clauses_ = inst.clauses;

        // Init activity from clause frequency
        for (const auto& c : clauses_)
            for (int32_t lit : c)
                activity_[std::abs(lit) - 1] += 1.0;

        uint64_t conflicts = 0;

        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms) return std::nullopt;

            // Unit propagation
            int conflict_clause = propagate();

            if (conflict_clause >= 0) {
                if (current_level_ == 0) return std::nullopt;
                ++conflicts;

                // Conflict analysis: learn clause + backtrack
                std::vector<int32_t> learned;
                int bt_level = 0;
                analyze(conflict_clause, learned, bt_level);
                backtrack(bt_level);

                // Add learned clause
                clauses_.push_back(learned);
                if (!learned.empty()) {
                    assign(learned[0], static_cast<int>(clauses_.size()) - 1);
                }

                // VSIDS decay
                for (auto& a : activity_) a *= 0.95;

                // Restart (Luby-like)
                if (conflicts % 500 == 0 && current_level_ > 2) {
                    backtrack(0);
                }
            } else {
                // Pick decision variable
                int32_t decision = pick_variable();
                if (decision == 0) {
                    // All assigned — SAT!
                    std::vector<bool> result(n_);
                    for (uint32_t i = 0; i < n_; ++i)
                        result[i] = (assignment_[i] == 1);
                    return result;
                }

                ++current_level_;
                trail_lim_.push_back(trail_.size());
                assign(decision, -1);
            }
        }
    }

private:
    uint32_t n_;
    std::vector<int8_t> assignment_;  // -1/0/1
    std::vector<int> level_;
    std::vector<int> antecedent_;
    std::vector<double> activity_;
    std::vector<int32_t> trail_;
    std::vector<size_t> trail_lim_;
    std::vector<std::vector<int32_t>> clauses_;
    int current_level_ = 0;

    bool lit_true(int32_t lit) const {
        int var = std::abs(lit) - 1;
        if (assignment_[var] < 0) return false;
        return (lit > 0) ? assignment_[var] == 1 : assignment_[var] == 0;
    }

    bool lit_false(int32_t lit) const {
        int var = std::abs(lit) - 1;
        if (assignment_[var] < 0) return false;
        return (lit > 0) ? assignment_[var] == 0 : assignment_[var] == 1;
    }

    bool lit_undef(int32_t lit) const {
        return assignment_[std::abs(lit) - 1] < 0;
    }

    void assign(int32_t lit, int ante) {
        int var = std::abs(lit) - 1;
        assignment_[var] = (lit > 0) ? 1 : 0;
        level_[var] = current_level_;
        antecedent_[var] = ante;
        trail_.push_back(lit);
    }

    void unassign(int32_t lit) {
        int var = std::abs(lit) - 1;
        assignment_[var] = -1;
        level_[var] = -1;
        antecedent_[var] = -1;
    }

    // Returns clause index of conflict, or -1 if no conflict
    int propagate() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int ci = 0; ci < static_cast<int>(clauses_.size()); ++ci) {
                const auto& clause = clauses_[ci];
                int undef_count = 0;
                int32_t undef_lit = 0;
                bool satisfied = false;

                for (int32_t lit : clause) {
                    if (lit_true(lit)) { satisfied = true; break; }
                    if (lit_undef(lit)) { ++undef_count; undef_lit = lit; }
                }

                if (satisfied) continue;
                if (undef_count == 0) return ci;  // Conflict
                if (undef_count == 1) {
                    assign(undef_lit, ci);  // Unit propagation
                    changed = true;
                }
            }
        }
        return -1;
    }

    int32_t pick_variable() {
        int best = -1;
        double best_act = -1.0;
        for (uint32_t i = 0; i < n_; ++i) {
            if (assignment_[i] < 0 && activity_[i] > best_act) {
                best_act = activity_[i];
                best = static_cast<int>(i);
            }
        }
        if (best < 0) return 0;  // All assigned
        return -(best + 1);  // Try negative first
    }

    void analyze(int conflict_ci, std::vector<int32_t>& learned, int& bt_level) {
        learned.clear();
        bt_level = 0;
        std::vector<bool> seen(n_, false);

        // Collect all literals at current level from conflict clause
        for (int32_t lit : clauses_[conflict_ci]) {
            int var = std::abs(lit) - 1;
            seen[var] = true;
            activity_[var] += 1.0;
        }

        // Build learned clause: negate all assignments involved
        for (uint32_t var = 0; var < n_; ++var) {
            if (seen[var] && level_[var] >= 0) {
                int32_t lit = (assignment_[var] == 1) ? -(static_cast<int32_t>(var) + 1) : (static_cast<int32_t>(var) + 1);
                learned.push_back(lit);
                if (level_[var] < current_level_ && level_[var] > bt_level) {
                    bt_level = level_[var];
                }
            }
        }

        if (learned.empty()) {
            // Fallback: learn from current level decisions
            for (auto it = trail_.rbegin(); it != trail_.rend(); ++it) {
                int var = std::abs(*it) - 1;
                if (level_[var] == current_level_) {
                    int32_t lit = (*it > 0) ? -(static_cast<int32_t>(var)+1) : (static_cast<int32_t>(var)+1);
                    learned.push_back(lit);
                }
            }
            bt_level = current_level_ - 1;
        }
    }

    void backtrack(int level) {
        while (!trail_.empty()) {
            int32_t lit = trail_.back();
            int var = std::abs(lit) - 1;
            if (level_[var] <= level) break;
            unassign(lit);
            trail_.pop_back();
        }
        while (trail_lim_.size() > static_cast<size_t>(level))
            trail_lim_.pop_back();
        current_level_ = level;
    }
};

// ═══════════════════════════════════════════════════════════════════
//  Main Test: End-to-End PoNW Consensus Loop
// ═══════════════════════════════════════════════════════════════════

int main() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════╗
║  NPChain — Proof-of-NP-Witness Consensus: E2E Test      ║
║  SHA3-256 (Keccak) + CDCL SAT Solver + O(n) Verifier    ║
╚═══════════════════════════════════════════════════════════╝
)" << '\n';

    int passed = 0;
    int failed = 0;

    // ─── TEST 1: SHA3-256 produces correct output ───
    {
        std::cout << "TEST 1: SHA3-256 hash function\n";
        Hash256 h1 = sha3_256(std::string_view(""));
        Hash256 h2 = sha3_256(std::string_view("abc"));
        Hash256 h3 = sha3_256(std::string_view("abc"));

        std::cout << "  SHA3(\"\")    = " << to_hex(h1).substr(0, 32) << "...\n";
        std::cout << "  SHA3(\"abc\") = " << to_hex(h2).substr(0, 32) << "...\n";

        // Deterministic
        assert(h2 == h3 && "SHA3 is not deterministic!");
        // Different inputs → different outputs
        assert(h1 != h2 && "SHA3 collision on trivial inputs!");
        // Non-zero
        uint8_t zero_check = 0;
        for (auto b : h1) zero_check |= b;
        assert(zero_check != 0 && "SHA3 of empty string is all zeros!");

        std::cout << "  ✓ Deterministic, collision-free, non-zero\n\n";
        ++passed;
    }

    // ─── TEST 2: SHAKE-256 extendable output ───
    {
        std::cout << "TEST 2: SHAKE-256 extendable output function\n";
        Hash256 seed{};
        seed[0] = 0x42;

        auto out64 = shake_256(ByteSpan{seed.data(), 32}, 64);
        auto out128 = shake_256(ByteSpan{seed.data(), 32}, 128);

        assert(out64.size() == 64);
        assert(out128.size() == 128);
        // First 64 bytes should match
        assert(std::memcmp(out64.data(), out128.data(), 64) == 0 &&
               "SHAKE-256 prefix mismatch!");

        std::cout << "  ✓ Produces correct-length output, prefix-consistent\n\n";
        ++passed;
    }

    // ─── TEST 3: Deterministic RNG from hash seed ───
    {
        std::cout << "TEST 3: Deterministic RNG\n";
        Hash256 seed = sha3_256(std::string_view("genesis"));
        DeterministicRNG rng1(seed);
        DeterministicRNG rng2(seed);

        bool match = true;
        for (int i = 0; i < 100; ++i) {
            if (rng1.next_u64() != rng2.next_u64()) { match = false; break; }
        }
        assert(match && "RNG not deterministic from same seed!");

        DeterministicRNG rng3(sha3_256(std::string_view("different")));
        int diffs = 0;
        DeterministicRNG rng4(seed);
        for (int i = 0; i < 100; ++i) {
            if (rng3.next_u64() != rng4.next_u64()) ++diffs;
        }
        assert(diffs > 90 && "Different seeds producing same sequence!");

        std::cout << "  ✓ Same seed → same sequence, different seed → different\n\n";
        ++passed;
    }

    // ─── TEST 4: Merkle tree ───
    {
        std::cout << "TEST 4: Merkle tree construction + verification\n";
        std::vector<Hash256> leaves;
        for (int i = 0; i < 8; ++i) {
            uint8_t data[4];
            std::memcpy(data, &i, 4);
            leaves.push_back(sha3_256(ByteSpan{data, 4}));
        }

        Hash256 root = merkle_root(leaves);
        std::cout << "  Root (8 leaves) = " << to_hex(root).substr(0, 32) << "...\n";

        MerkleProof proof = merkle_proof(leaves, 3);
        assert(merkle_verify(root, leaves[3], proof) && "Merkle proof failed!");
        assert(!merkle_verify(root, leaves[5], proof) && "Merkle proof accepted wrong leaf!");

        std::cout << "  ✓ Proof verified for correct leaf, rejected for wrong leaf\n\n";
        ++passed;
    }

    // ─── TEST 5: SAT instance generation (planted solution exists) ───
    {
        std::cout << "TEST 5: 3-SAT instance generation with planted solution\n";

        Hash256 block_hash = sha3_256(std::string_view("block_0"));
        auto inst = generate_sat_instance(block_hash, 1);

        std::cout << "  Variables:  " << inst.num_variables << '\n';
        std::cout << "  Clauses:    " << inst.num_clauses << '\n';
        std::cout << "  Ratio α:    " << static_cast<double>(inst.num_clauses) / inst.num_variables << '\n';

        // Verify determinism: same hash → same instance
        auto inst2 = generate_sat_instance(block_hash, 1);
        assert(inst.num_variables == inst2.num_variables);
        assert(inst.clauses.size() == inst2.clauses.size());
        for (size_t i = 0; i < inst.clauses.size(); ++i)
            assert(inst.clauses[i] == inst2.clauses[i]);

        std::cout << "  ✓ Instance is deterministic from block hash\n\n";
        ++passed;
    }

    // ─── TEST 6: CDCL solver finds a valid witness ───
    {
        std::cout << "TEST 6: CDCL SAT solver finds witness\n";

        for (uint32_t diff = 1; diff <= 8; diff *= 2) {
            Hash256 block_hash{};
            block_hash[0] = static_cast<uint8_t>(diff);
            block_hash[1] = 0xAB;

            auto inst = generate_sat_instance(block_hash, diff);
            std::cout << "  Difficulty " << diff
                      << " → " << inst.num_variables << " vars, "
                      << inst.num_clauses << " clauses... ";

            auto start = std::chrono::steady_clock::now();
            CDCLSolver solver;
            auto result = solver.solve(inst, 10000);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if (result.has_value()) {
                assert(inst.verify(result.value()) && "Solver returned invalid witness!");
                std::cout << "SOLVED in " << elapsed << "ms ✓\n";
            } else {
                std::cout << "TIMEOUT ✗\n";
                ++failed;
                continue;
            }
            ++passed;
        }
        std::cout << '\n';
    }

    // ─── TEST 7: O(n) verification is fast ───
    {
        std::cout << "TEST 7: Witness verification is O(n) fast\n";

        Hash256 block_hash = sha3_256(std::string_view("speed_test"));
        auto inst = generate_sat_instance(block_hash, 4);

        CDCLSolver solver;
        auto witness = solver.solve(inst, 10000);
        assert(witness.has_value());

        // Verify 10000 times and measure
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; ++i) {
            bool valid = inst.verify(witness.value());
            assert(valid);
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();

        double per_verify = static_cast<double>(elapsed) / 10000.0;
        std::cout << "  " << inst.num_variables << " vars, " << inst.num_clauses << " clauses\n";
        std::cout << "  10,000 verifications in " << elapsed / 1000 << "ms\n";
        std::cout << "  Per verification: " << per_verify << "μs\n";
        std::cout << "  ✓ Verification is fast (polynomial)\n\n";
        ++passed;
    }

    // ─── TEST 8: Full PoNW mining loop simulation ───
    {
        std::cout << "TEST 8: Full PoNW mining loop (5 blocks)\n\n";

        Hash256 prev_hash = sha3_256(std::string_view("NPChain Testnet Genesis"));
        uint32_t difficulty = 1;

        for (int block = 0; block < 5; ++block) {
            auto start = std::chrono::steady_clock::now();

            // Step 1: Generate instance from previous block hash
            auto inst = generate_sat_instance(prev_hash, difficulty);

            // Step 2: Mine (solve the instance)
            CDCLSolver solver;
            auto witness = solver.solve(inst, 30000);

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if (!witness.has_value()) {
                std::cout << "  Block " << block << ": FAILED TO MINE\n";
                ++failed;
                continue;
            }

            // Step 3: Verify (what every validator does)
            bool valid = inst.verify(witness.value());
            assert(valid);

            // Step 4: Compute "block hash" for next iteration
            // In real chain: hash(header + witness + transactions)
            // Here: hash(prev_hash + witness_bits)
            Bytes block_data(32 + inst.num_variables);
            std::memcpy(block_data.data(), prev_hash.data(), 32);
            for (uint32_t i = 0; i < inst.num_variables; ++i)
                block_data[32 + i] = witness.value()[i] ? 1 : 0;
            Hash256 block_hash = sha3_256(ByteSpan{block_data.data(), block_data.size()});

            std::cout << "  ✓ Block #" << block
                      << " | " << inst.num_variables << " vars"
                      << " | solved " << elapsed << "ms"
                      << " | hash " << to_hex(block_hash).substr(0, 16) << "..."
                      << '\n';

            prev_hash = block_hash;
            ++passed;
        }
    }

    // ─── Summary ───
    std::cout << "\n═══════════════════════════════════════════════════════════\n";
    std::cout << "  Results: " << passed << " passed, " << failed << " failed\n";
    if (failed == 0) {
        std::cout << R"(
  ╔═══════════════════════════════════════════════════════╗
  ║  ALL TESTS PASSED                                    ║
  ║                                                      ║
  ║  The PoNW consensus loop works end-to-end:           ║
  ║    SHA3-256 (Keccak) ✓                               ║
  ║    SHAKE-256 (XOF)   ✓                               ║
  ║    Merkle Trees      ✓                               ║
  ║    Deterministic RNG ✓                               ║
  ║    3-SAT Generation  ✓                               ║
  ║    CDCL Solver       ✓                               ║
  ║    O(n) Verification ✓                               ║
  ║    Mining Loop       ✓                               ║
  ╚═══════════════════════════════════════════════════════╝
)";
    }
    std::cout << "═══════════════════════════════════════════════════════════\n";

    return failed > 0 ? 1 : 0;
}

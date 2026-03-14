#pragma once

#include "utils/types.hpp"
#include "core/block.hpp"
#include "consensus/ponw_engine.hpp"

#include <atomic>
#include <functional>
#include <thread>
#include <future>
#include <chrono>

namespace npchain::mining {

using namespace npchain::consensus;
using namespace npchain::core;

// ═══════════════════════════════════════════════════════════════════
//  Solver Interface
//  Each NP problem type has a dedicated solver implementing this
// ═══════════════════════════════════════════════════════════════════

class ISolver {
public:
    virtual ~ISolver() = default;

    /// Attempt to find a witness. Returns nullopt if cancelled or timed out.
    [[nodiscard]] virtual std::optional<Witness> solve(
        const NPInstance& instance,
        std::atomic<bool>& cancel_flag,
        std::chrono::seconds timeout
    ) = 0;

    /// Get solver name for logging
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Get the problem type this solver handles
    [[nodiscard]] virtual ProblemType problem_type() const noexcept = 0;
};

// ═══════════════════════════════════════════════════════════════════
//  k-SAT Solver (CDCL — Conflict-Driven Clause Learning)
//
//  State of the art SAT solving:
//  1. Unit propagation (BCP)
//  2. VSIDS decision heuristic
//  3. Conflict analysis → first-UIP clause learning
//  4. Non-chronological backtracking
//  5. Geometric restart strategy
// ═══════════════════════════════════════════════════════════════════

class CDCLSolver : public ISolver {
public:
    struct Config {
        double   vsids_decay     = 0.95;      // Variable activity decay
        uint32_t restart_base    = 100;        // Luby restart base
        double   restart_mult    = 1.5;        // Restart multiplier
        uint32_t clause_db_limit = 100'000;    // Max learned clauses before GC
        bool     phase_saving    = true;        // Remember last assignment
    };

    explicit CDCLSolver(Config cfg = {});
    ~CDCLSolver() override;

    [[nodiscard]] std::optional<Witness> solve(
        const NPInstance& instance,
        std::atomic<bool>& cancel_flag,
        std::chrono::seconds timeout
    ) override;

    [[nodiscard]] std::string_view name() const noexcept override { return "CDCL-SAT"; }
    [[nodiscard]] ProblemType problem_type() const noexcept override { return ProblemType::K_SAT; }

private:
    Config config_;

    struct InternalState;
    std::unique_ptr<InternalState> state_;

    // Core CDCL operations
    enum class PropResult { OK, CONFLICT, UNSAT };

    PropResult unit_propagate();
    int32_t    pick_decision_variable();
    void       analyze_conflict(std::vector<int32_t>& learned_clause, int& backtrack_level);
    void       backtrack(int level);
    void       add_learned_clause(const std::vector<int32_t>& clause);
    bool       should_restart(uint64_t conflicts) const;
    void       reduce_clause_db();
};

// ═══════════════════════════════════════════════════════════════════
//  Subset-Sum Solver (Meet-in-the-Middle + Dynamic Programming)
//
//  O(2^{n/2}) meet-in-the-middle for small instances
//  Horowitz-Sahni algorithm with hash table lookup
// ═══════════════════════════════════════════════════════════════════

class SubsetSumSolver : public ISolver {
public:
    struct Config {
        uint32_t mitm_threshold = 40;  // Use MitM for n <= threshold
        size_t   hash_table_size = 1ULL << 24;  // 16M entries
    };

    explicit SubsetSumSolver(Config cfg = {});

    [[nodiscard]] std::optional<Witness> solve(
        const NPInstance& instance,
        std::atomic<bool>& cancel_flag,
        std::chrono::seconds timeout
    ) override;

    [[nodiscard]] std::string_view name() const noexcept override { return "MitM-SubsetSum"; }
    [[nodiscard]] ProblemType problem_type() const noexcept override { return ProblemType::SUBSET_SUM; }

private:
    Config config_;

    std::optional<std::vector<uint32_t>> meet_in_the_middle(
        const SubsetSumInstance& inst,
        std::atomic<bool>& cancel
    );

    std::optional<std::vector<uint32_t>> dynamic_programming(
        const SubsetSumInstance& inst,
        std::atomic<bool>& cancel
    );
};

// ═══════════════════════════════════════════════════════════════════
//  Graph Coloring Solver (Backtracking + Constraint Propagation)
//
//  DSatur heuristic + MAC (Maintaining Arc Consistency)
//  Color ordering: try fewest available colors first
// ═══════════════════════════════════════════════════════════════════

class GraphColorSolver : public ISolver {
public:
    struct Config {
        bool     use_dsatur    = true;   // DSatur vertex ordering heuristic
        bool     use_mac       = true;   // Arc consistency propagation
        uint32_t backjump      = true;   // Conflict-directed backjumping
    };

    explicit GraphColorSolver(Config cfg = {});

    [[nodiscard]] std::optional<Witness> solve(
        const NPInstance& instance,
        std::atomic<bool>& cancel_flag,
        std::chrono::seconds timeout
    ) override;

    [[nodiscard]] std::string_view name() const noexcept override { return "DSatur-GC"; }
    [[nodiscard]] ProblemType problem_type() const noexcept override { return ProblemType::GRAPH_COLORING; }

private:
    Config config_;

    bool backtrack_search(
        std::vector<uint8_t>& colors,
        const GraphColorInstance& inst,
        std::vector<std::vector<bool>>& available,
        std::atomic<bool>& cancel
    );

    uint32_t select_vertex_dsatur(
        const std::vector<uint8_t>& colors,
        const GraphColorInstance& inst,
        const std::vector<std::vector<bool>>& available
    );
};

// ═══════════════════════════════════════════════════════════════════
//  Hamiltonian Path Solver (DFS + Warnsdorff + Pruning)
//
//  Multiple random starts with intelligent backtracking
//  Warnsdorff's heuristic for neighbor selection
// ═══════════════════════════════════════════════════════════════════

class HamiltonianSolver : public ISolver {
public:
    struct Config {
        uint32_t random_restarts = 1000;  // Different starting vertices
        bool     warnsdorff      = true;  // Prefer vertices with fewer options
    };

    explicit HamiltonianSolver(Config cfg = {});

    [[nodiscard]] std::optional<Witness> solve(
        const NPInstance& instance,
        std::atomic<bool>& cancel_flag,
        std::chrono::seconds timeout
    ) override;

    [[nodiscard]] std::string_view name() const noexcept override { return "DFS-Hamilton"; }
    [[nodiscard]] ProblemType problem_type() const noexcept override { return ProblemType::HAMILTONIAN_PATH; }

private:
    Config config_;

    bool dfs_search(
        std::vector<uint32_t>& path,
        std::vector<bool>& visited,
        const HamiltonianInstance& inst,
        const std::vector<std::vector<uint32_t>>& adj_list,
        std::atomic<bool>& cancel
    );
};

// ═══════════════════════════════════════════════════════════════════
//  Solver Pool — Parallel Multi-Strategy Mining Engine
//
//  Runs multiple solver instances in parallel across threads.
//  First solver to find a valid witness wins.
//  Portfolio strategy: different random seeds per thread.
// ═══════════════════════════════════════════════════════════════════

class SolverPool {
public:
    struct Config {
        uint32_t num_threads       = std::thread::hardware_concurrency();
        uint32_t portfolio_copies  = 4;  // Copies of each solver with different seeds
        std::chrono::seconds timeout{300};  // 5 minute timeout per attempt
    };

    explicit SolverPool(Config cfg = {});
    ~SolverPool();

    // Non-copyable, moveable
    SolverPool(const SolverPool&) = delete;
    SolverPool& operator=(const SolverPool&) = delete;

    /// Register a solver for a specific problem type
    void register_solver(std::unique_ptr<ISolver> solver);

    /// Mine: attempt to find a witness for the given instance
    /// Returns the first valid witness found across all parallel attempts
    [[nodiscard]] std::optional<Witness> mine(
        const NPInstance& instance
    );

    /// Stop all mining operations
    void stop();

    /// Mining statistics
    struct Stats {
        uint64_t attempts       = 0;
        uint64_t solutions      = 0;
        uint64_t timeouts       = 0;
        double   avg_solve_ms   = 0;
        std::string last_solver;  // Name of solver that found last solution
    };

    [[nodiscard]] Stats stats() const noexcept;

private:
    Config config_;
    std::atomic<bool> running_{true};
    std::vector<std::unique_ptr<ISolver>> solvers_;
    Stats stats_;
    mutable std::mutex stats_mutex_;
};

// ═══════════════════════════════════════════════════════════════════
//  Mining Coordinator
//  Orchestrates the full mining loop: get template → solve → submit
// ═══════════════════════════════════════════════════════════════════

class MiningCoordinator {
public:
    using BlockTemplateCallback = std::function<Block()>;
    using BlockSubmitCallback   = std::function<bool(Block&&)>;
    using NewBlockSignal        = std::function<bool()>;  // returns true if new block arrived

    struct Config {
        SolverPool::Config pool_config;
        std::array<uint8_t, DILITHIUM_PK_SIZE> miner_pubkey;
        SecureArray<DILITHIUM_SK_SIZE>*         miner_secret_key;
    };

    explicit MiningCoordinator(Config cfg);
    ~MiningCoordinator();

    /// Start mining loop (blocking — run in dedicated thread)
    void run(
        BlockTemplateCallback get_template,
        BlockSubmitCallback submit_block,
        NewBlockSignal check_new_block
    );

    /// Stop mining
    void stop();

    /// Check if actively mining
    [[nodiscard]] bool is_mining() const noexcept;

    /// Get mining statistics
    [[nodiscard]] SolverPool::Stats stats() const noexcept;

private:
    Config config_;
    SolverPool pool_;
    std::atomic<bool> mining_{false};
};

} // namespace npchain::mining

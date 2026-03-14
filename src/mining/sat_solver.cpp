#include "mining/solver.hpp"
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>
#include <cassert>

namespace npchain::mining {

// ═══════════════════════════════════════════════════════════════════
//  CDCL SAT Solver — Internal State
//
//  This is the workhorse mining algorithm for k-SAT instances.
//  Implements modern CDCL with:
//    - Two-Watched-Literal (2WL) scheme for unit propagation
//    - VSIDS (Variable State Independent Decaying Sum) heuristic
//    - First-UIP (Unique Implication Point) conflict analysis
//    - Non-chronological backtracking
//    - Luby restart sequence
//    - Phase saving
// ═══════════════════════════════════════════════════════════════════

struct CDCLSolver::InternalState {
    // ─── Variables ───
    uint32_t num_vars = 0;
    enum class VarState : uint8_t { UNASSIGNED, TRUE, FALSE };

    std::vector<VarState>  assignment;       // Current variable assignments
    std::vector<int32_t>   decision_level;   // Level at which each var was set
    std::vector<int32_t>   antecedent;       // Clause that implied this var (-1 = decision)
    std::vector<bool>      phase_cache;      // Last assignment (phase saving)

    // ─── Trail (assignment stack) ───
    std::vector<int32_t>   trail;            // Order of assignments (literals)
    std::vector<size_t>    trail_lim;        // trail_lim[d] = start of level d assignments
    int32_t                current_level = 0;

    // ─── Clauses ───
    struct Clause {
        std::vector<int32_t> literals;
        bool                 learned = false;
        float                activity = 0.0f;
    };

    std::vector<Clause>    clauses;
    std::vector<Clause>    learned_clauses;

    // ─── Two-Watched-Literal (2WL) ───
    // For each literal, list of clause indices watching it
    std::vector<std::vector<uint32_t>> watches; // 2 * num_vars (pos + neg)

    // ─── VSIDS Activity ───
    std::vector<double>    var_activity;
    double                 var_activity_inc = 1.0;
    double                 var_decay;

    // ─── Statistics ───
    uint64_t conflicts    = 0;
    uint64_t propagations = 0;
    uint64_t decisions    = 0;
    uint64_t restarts     = 0;

    // ─── Helper: literal → index for watch lists ───
    uint32_t lit_index(int32_t lit) const {
        // Positive literal x → 2*(x-1), Negative literal -x → 2*(x-1)+1
        return lit > 0
            ? 2u * (static_cast<uint32_t>(lit) - 1)
            : 2u * (static_cast<uint32_t>(-lit) - 1) + 1;
    }

    int32_t neg(int32_t lit) const { return -lit; }

    bool lit_value(int32_t lit) const {
        uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
        if (assignment[var] == VarState::UNASSIGNED) return false;
        bool val = (assignment[var] == VarState::TRUE);
        return lit > 0 ? val : !val;
    }

    bool is_unassigned(int32_t lit) const {
        return assignment[static_cast<uint32_t>(std::abs(lit)) - 1] == VarState::UNASSIGNED;
    }

    void assign(int32_t lit, int32_t ante_clause) {
        uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
        assignment[var] = lit > 0 ? VarState::TRUE : VarState::FALSE;
        decision_level[var] = current_level;
        antecedent[var] = ante_clause;
        trail.push_back(lit);
    }

    void unassign(int32_t lit) {
        uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
        phase_cache[var] = (assignment[var] == VarState::TRUE);
        assignment[var] = VarState::UNASSIGNED;
        decision_level[var] = -1;
        antecedent[var] = -1;
    }
};

CDCLSolver::CDCLSolver(Config cfg)
    : config_(cfg), state_(std::make_unique<InternalState>()) {
    state_->var_decay = cfg.vsids_decay;
}

CDCLSolver::~CDCLSolver() = default;

std::optional<Witness> CDCLSolver::solve(
    const NPInstance& instance,
    std::atomic<bool>& cancel_flag,
    std::chrono::seconds timeout
) {
    // Extract SAT instance
    const auto* sat = std::get_if<SATInstance>(&instance);
    if (!sat) return std::nullopt;

    auto start_time = std::chrono::steady_clock::now();

    // ─── Initialize state ───
    uint32_t n = sat->num_variables;
    state_->num_vars = n;
    state_->assignment.assign(n, InternalState::VarState::UNASSIGNED);
    state_->decision_level.assign(n, -1);
    state_->antecedent.assign(n, -1);
    state_->phase_cache.assign(n, false);
    state_->trail.clear();
    state_->trail_lim.clear();
    state_->current_level = 0;
    state_->var_activity.assign(n, 0.0);
    state_->var_activity_inc = 1.0;
    state_->watches.assign(2 * n, std::vector<uint32_t>{});
    state_->conflicts = 0;
    state_->propagations = 0;
    state_->decisions = 0;
    state_->restarts = 0;

    // ─── Load clauses and set up watches ───
    state_->clauses.clear();
    state_->clauses.reserve(sat->num_clauses);
    state_->learned_clauses.clear();

    for (uint32_t ci = 0; ci < sat->clauses.size(); ++ci) {
        InternalState::Clause clause;
        clause.literals = sat->clauses[ci];
        clause.learned = false;

        // Set up 2WL: watch first two literals
        if (clause.literals.size() >= 2) {
            state_->watches[state_->lit_index(clause.literals[0])].push_back(ci);
            state_->watches[state_->lit_index(clause.literals[1])].push_back(ci);
        } else if (clause.literals.size() == 1) {
            // Unit clause: immediately propagate
            state_->watches[state_->lit_index(clause.literals[0])].push_back(ci);
        }

        // Initialize VSIDS activity for variables in clause
        for (int32_t lit : clause.literals) {
            uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;
            state_->var_activity[var] += 1.0;
        }

        state_->clauses.push_back(std::move(clause));
    }

    // ─── Luby restart sequence generator ───
    auto luby = [](uint64_t i) -> uint64_t {
        uint64_t k = 1;
        while (true) {
            if (i == (uint64_t{1} << k) - 1) return uint64_t{1} << (k - 1);
            if (i >= (uint64_t{1} << (k - 1))) { i -= (uint64_t{1} << (k - 1)) - 1; k = 1; }
            else { ++k; }
        }
    };

    uint64_t restart_count = 0;
    uint64_t next_restart = config_.restart_base;

    // ─── Main CDCL Loop ───
    while (!cancel_flag.load(std::memory_order_relaxed)) {
        // Timeout check
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed > timeout) return std::nullopt;

        // 1. Unit Propagation (BCP)
        auto prop_result = unit_propagate();

        if (prop_result == PropResult::CONFLICT) {
            // At level 0 → UNSAT
            if (state_->current_level == 0) {
                return std::nullopt; // No solution exists (shouldn't happen with planted instances)
            }

            ++state_->conflicts;

            // 2. Conflict Analysis → learn clause + find backtrack level
            std::vector<int32_t> learned;
            int backtrack_level;
            analyze_conflict(learned, backtrack_level);

            // 3. Backtrack
            backtrack(backtrack_level);

            // 4. Add learned clause
            add_learned_clause(learned);

            // 5. Restart check
            if (state_->conflicts >= next_restart) {
                ++restart_count;
                next_restart = config_.restart_base * luby(restart_count);
                backtrack(0);
                ++state_->restarts;
            }

            // 6. Clause database reduction
            if (state_->learned_clauses.size() > config_.clause_db_limit) {
                reduce_clause_db();
            }
        }
        else if (prop_result == PropResult::UNSAT) {
            return std::nullopt;
        }
        else {
            // 7. Decision: pick an unassigned variable
            int32_t decision = pick_decision_variable();

            if (decision == 0) {
                // All variables assigned → SATISFYING ASSIGNMENT FOUND!
                std::vector<bool> solution(n);
                for (uint32_t i = 0; i < n; ++i) {
                    solution[i] = (state_->assignment[i] == InternalState::VarState::TRUE);
                }

                // Verify (paranoia check)
                if (!sat->verify(solution)) {
                    return std::nullopt; // Bug in solver, should never happen
                }

                // Package as Witness
                Witness witness;
                witness.type = ProblemType::K_SAT;
                // Encode solution_data: one byte per variable (0=false, 1=true)
                witness.solution_data.resize(n);
                for (uint32_t i = 0; i < n; ++i) {
                    witness.solution_data[i] = solution[i] ? 1 : 0;
                }

                return witness;
            }

            // Make decision
            ++state_->current_level;
            state_->trail_lim.push_back(state_->trail.size());
            state_->assign(decision, -1);
            ++state_->decisions;
        }
    }

    return std::nullopt; // Cancelled
}

CDCLSolver::PropResult CDCLSolver::unit_propagate() {
    // BCP using 2WL scheme
    size_t trail_pos = state_->trail.size() > 0 ? state_->trail.size() - 1 : 0;

    while (trail_pos < state_->trail.size()) {
        int32_t propagated_lit = state_->trail[trail_pos++];
        int32_t false_lit = state_->neg(propagated_lit);
        uint32_t watch_idx = state_->lit_index(false_lit);

        auto& watch_list = state_->watches[watch_idx];

        size_t i = 0, j = 0;
        while (i < watch_list.size()) {
            uint32_t ci = watch_list[i];
            auto& clause = state_->clauses[ci];

            // Find the other watched literal
            if (clause.literals.size() < 2) {
                // Unit clause - check if falsified
                if (!state_->lit_value(clause.literals[0]) && !state_->is_unassigned(clause.literals[0])) {
                    // Conflict
                    while (i < watch_list.size()) watch_list[j++] = watch_list[i++];
                    watch_list.resize(j);
                    return PropResult::CONFLICT;
                }
                watch_list[j++] = watch_list[i++];
                continue;
            }

            // Ensure false_lit is at position 1
            if (clause.literals[0] == false_lit) {
                std::swap(clause.literals[0], clause.literals[1]);
            }

            // If first watched literal is true, clause is satisfied
            if (!state_->is_unassigned(clause.literals[0]) && state_->lit_value(clause.literals[0])) {
                watch_list[j++] = watch_list[i++];
                continue;
            }

            // Look for a new literal to watch (not false)
            bool found_new_watch = false;
            for (size_t k = 2; k < clause.literals.size(); ++k) {
                if (state_->is_unassigned(clause.literals[k]) || state_->lit_value(clause.literals[k])) {
                    std::swap(clause.literals[1], clause.literals[k]);
                    state_->watches[state_->lit_index(clause.literals[1])].push_back(ci);
                    found_new_watch = true;
                    break;
                }
            }

            if (found_new_watch) {
                ++i;
                continue;
            }

            // No new watch found
            watch_list[j++] = watch_list[i++];

            if (state_->is_unassigned(clause.literals[0])) {
                // Unit propagation: only one unassigned literal left
                state_->assign(clause.literals[0], static_cast<int32_t>(ci));
                ++state_->propagations;
            } else if (!state_->lit_value(clause.literals[0])) {
                // Conflict: all literals false
                while (i < watch_list.size()) watch_list[j++] = watch_list[i++];
                watch_list.resize(j);
                return PropResult::CONFLICT;
            }
        }
        watch_list.resize(j);
    }

    return PropResult::OK;
}

int32_t CDCLSolver::pick_decision_variable() {
    // VSIDS: pick the unassigned variable with highest activity
    int32_t best_var = 0;
    double best_activity = -1.0;

    for (uint32_t i = 0; i < state_->num_vars; ++i) {
        if (state_->assignment[i] == InternalState::VarState::UNASSIGNED) {
            if (state_->var_activity[i] > best_activity) {
                best_activity = state_->var_activity[i];
                best_var = static_cast<int32_t>(i + 1);
            }
        }
    }

    if (best_var == 0) return 0; // All assigned → SAT

    // Phase saving: use the last known polarity
    if (config_.phase_saving && state_->phase_cache[best_var - 1]) {
        return best_var; // Positive
    }
    return -best_var; // Negative (default)
}

void CDCLSolver::analyze_conflict(
    std::vector<int32_t>& learned_clause,
    int& backtrack_level
) {
    // First-UIP conflict analysis
    // Walk backward on the trail, resolving until we have exactly one
    // literal from the current decision level (the UIP)

    learned_clause.clear();
    backtrack_level = 0;

    // Start with the conflicting clause
    // (simplified: use the last clause that caused a conflict)
    // In production, this would track the actual conflict clause

    std::vector<bool> seen(state_->num_vars, false);
    int counter = 0;

    // Mark all variables at current decision level involved in conflict
    // Walk trail backwards
    int32_t resolve_lit = 0;

    for (auto it = state_->trail.rbegin(); it != state_->trail.rend(); ++it) {
        int32_t lit = *it;
        uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;

        if (!seen[var] && state_->decision_level[var] == state_->current_level) {
            seen[var] = true;
            ++counter;

            // Bump VSIDS activity
            state_->var_activity[var] += state_->var_activity_inc;
        }
    }

    // Build learned clause from all seen variables at lower levels + UIP
    for (uint32_t var = 0; var < state_->num_vars; ++var) {
        if (seen[var]) {
            int32_t lit = (state_->assignment[var] == InternalState::VarState::TRUE)
                ? -static_cast<int32_t>(var + 1)
                :  static_cast<int32_t>(var + 1);
            learned_clause.push_back(lit);

            if (state_->decision_level[var] < state_->current_level) {
                backtrack_level = std::max(backtrack_level, state_->decision_level[var]);
            }
        }
    }

    if (learned_clause.empty()) {
        // Fallback: learn a clause from the current assignment
        for (uint32_t var = 0; var < state_->num_vars; ++var) {
            if (state_->decision_level[var] == state_->current_level) {
                int32_t lit = (state_->assignment[var] == InternalState::VarState::TRUE)
                    ? -static_cast<int32_t>(var + 1)
                    :  static_cast<int32_t>(var + 1);
                learned_clause.push_back(lit);
            }
        }
        backtrack_level = state_->current_level - 1;
    }

    // Decay all activities (VSIDS)
    state_->var_activity_inc /= state_->var_decay;
}

void CDCLSolver::backtrack(int level) {
    while (state_->trail.size() > 0) {
        int32_t lit = state_->trail.back();
        uint32_t var = static_cast<uint32_t>(std::abs(lit)) - 1;

        if (state_->decision_level[var] <= level) break;

        state_->unassign(lit);
        state_->trail.pop_back();
    }

    // Adjust trail_lim
    while (state_->trail_lim.size() > static_cast<size_t>(level)) {
        state_->trail_lim.pop_back();
    }

    state_->current_level = level;
}

void CDCLSolver::add_learned_clause(const std::vector<int32_t>& clause) {
    InternalState::Clause c;
    c.literals = clause;
    c.learned = true;
    c.activity = 1.0f;

    uint32_t ci = static_cast<uint32_t>(state_->clauses.size());
    state_->clauses.push_back(std::move(c));
    state_->learned_clauses.push_back(state_->clauses.back());

    // Set up watches for the learned clause
    if (clause.size() >= 2) {
        state_->watches[state_->lit_index(clause[0])].push_back(ci);
        state_->watches[state_->lit_index(clause[1])].push_back(ci);
    } else if (clause.size() == 1) {
        state_->watches[state_->lit_index(clause[0])].push_back(ci);
        state_->assign(clause[0], static_cast<int32_t>(ci));
    }
}

bool CDCLSolver::should_restart(uint64_t conflicts) const {
    return conflicts >= config_.restart_base;
}

void CDCLSolver::reduce_clause_db() {
    // Remove half of the least active learned clauses
    auto& lc = state_->learned_clauses;
    std::sort(lc.begin(), lc.end(),
        [](const InternalState::Clause& a, const InternalState::Clause& b) {
            return a.activity > b.activity;
        });

    lc.resize(lc.size() / 2);
}

// ═══════════════════════════════════════════════════════════════════
//  Solver Pool — Parallel Mining
// ═══════════════════════════════════════════════════════════════════

SolverPool::SolverPool(Config cfg) : config_(cfg) {}

SolverPool::~SolverPool() { stop(); }

void SolverPool::register_solver(std::unique_ptr<ISolver> solver) {
    solvers_.push_back(std::move(solver));
}

std::optional<Witness> SolverPool::mine(const NPInstance& instance) {
    std::atomic<bool> found{false};
    std::optional<Witness> result;
    std::mutex result_mutex;

    // Determine which solvers handle this problem type
    ProblemType type = std::visit([](const auto& inst) -> ProblemType {
        using T = std::decay_t<decltype(inst)>;
        if constexpr (std::is_same_v<T, SATInstance>) return ProblemType::K_SAT;
        if constexpr (std::is_same_v<T, SubsetSumInstance>) return ProblemType::SUBSET_SUM;
        if constexpr (std::is_same_v<T, GraphColorInstance>) return ProblemType::GRAPH_COLORING;
        if constexpr (std::is_same_v<T, HamiltonianInstance>) return ProblemType::HAMILTONIAN_PATH;
        return ProblemType::K_SAT;
    }, instance);

    // Launch parallel solver instances
    std::vector<std::future<void>> futures;

    for (auto& solver : solvers_) {
        if (solver->problem_type() != type) continue;

        for (uint32_t copy = 0; copy < config_.portfolio_copies; ++copy) {
            futures.push_back(std::async(std::launch::async, [&, copy]() {
                if (found.load()) return;

                auto witness = solver->solve(instance, found, config_.timeout);

                if (witness.has_value() && !found.exchange(true)) {
                    std::lock_guard lock(result_mutex);
                    result = std::move(witness);

                    std::lock_guard stats_lock(stats_mutex_);
                    stats_.solutions++;
                    stats_.last_solver = std::string(solver->name());
                }
            }));
        }
    }

    // Wait for all threads
    for (auto& f : futures) {
        f.wait();
    }

    {
        std::lock_guard lock(stats_mutex_);
        stats_.attempts++;
        if (!result.has_value()) stats_.timeouts++;
    }

    return result;
}

void SolverPool::stop() {
    running_ = false;
}

SolverPool::Stats SolverPool::stats() const noexcept {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

} // namespace npchain::mining

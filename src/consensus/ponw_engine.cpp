#include "consensus/ponw_engine.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>
#include <cassert>

namespace npchain::consensus {

// ═══════════════════════════════════════════════════════════════════
//  Instance Generation
//
//  The core of PoNW: deterministically generate NP-complete instances
//  from the previous block hash. Every node generates the SAME
//  instance, so any claimed witness can be verified.
// ═══════════════════════════════════════════════════════════════════

ProblemType InstanceGenerator::select_problem_type(
    const Hash256& prev_block_hash
) noexcept {
    // Use last byte of hash mod 4 to select problem type
    // This ensures uniform distribution across problem types
    uint8_t selector = prev_block_hash[HASH_SIZE - 1] % static_cast<uint8_t>(ProblemType::COUNT);
    return static_cast<ProblemType>(selector);
}

auto InstanceGenerator::difficulty_to_size(
    ProblemType type, Difficulty difficulty
) noexcept -> SizeParams {
    // Map difficulty (uint64) to problem size parameters.
    // Higher difficulty = larger instances = harder to solve.
    //
    // The mapping is calibrated so that:
    //   - At genesis difficulty, instances are trivially small
    //   - At target difficulty, instances take ~60s to solve
    //   - The relationship is roughly logarithmic
    //     (doubling difficulty adds ~1 variable/vertex)

    double log_diff = std::log2(static_cast<double>(std::max(difficulty, uint64_t{1})));

    switch (type) {
        case ProblemType::K_SAT: {
            // 3-SAT at the phase transition (α ≈ 4.267 clauses/variable)
            // This is where instances are hardest
            uint32_t num_vars = static_cast<uint32_t>(10 + log_diff * 2.5);
            uint32_t num_clauses = static_cast<uint32_t>(num_vars * 4.267);
            return {num_vars, num_clauses};
        }

        case ProblemType::SUBSET_SUM: {
            // Density ≈ 1.0 (hardest regime for lattice attacks too)
            uint32_t set_size = static_cast<uint32_t>(8 + log_diff * 2.0);
            uint32_t target_bits = static_cast<uint32_t>(set_size * 1.0);
            return {set_size, target_bits};
        }

        case ProblemType::GRAPH_COLORING: {
            // Chromatic number = 3, at the connectivity threshold
            uint32_t num_vertices = static_cast<uint32_t>(10 + log_diff * 2.0);
            uint32_t edge_density_pct = 45; // ~45% of max edges (hard regime for 3-coloring)
            return {num_vertices, edge_density_pct};
        }

        case ProblemType::HAMILTONIAN_PATH: {
            // Sparse graphs near the Hamiltonian threshold (c ≈ ln(n)/n)
            uint32_t num_vertices = static_cast<uint32_t>(8 + log_diff * 1.8);
            uint32_t avg_degree = std::max(uint32_t{2}, static_cast<uint32_t>(std::log(num_vertices) * 1.2));
            return {num_vertices, avg_degree};
        }

        default:
            return {20, 85}; // Fallback
    }
}

NPInstance InstanceGenerator::generate(
    const Hash256& prev_block_hash,
    Difficulty difficulty
) {
    // 1. Select problem type from hash
    ProblemType type = select_problem_type(prev_block_hash);

    // 2. Create deterministic RNG from hash (SHAKE-256 based)
    crypto::DeterministicRNG rng(prev_block_hash);

    // 3. Generate the appropriate instance
    switch (type) {
        case ProblemType::K_SAT:
            return generate_sat(rng, difficulty);
        case ProblemType::SUBSET_SUM:
            return generate_subset_sum(rng, difficulty);
        case ProblemType::GRAPH_COLORING:
            return generate_graph_color(rng, difficulty);
        case ProblemType::HAMILTONIAN_PATH:
            return generate_hamiltonian(rng, difficulty);
        default:
            return generate_sat(rng, difficulty); // Fallback
    }
}

SATInstance InstanceGenerator::generate_sat(
    crypto::DeterministicRNG& rng, Difficulty difficulty
) {
    auto [num_vars, num_clauses] = difficulty_to_size(ProblemType::K_SAT, difficulty);
    constexpr uint32_t K = 3; // 3-SAT

    SATInstance inst;
    inst.num_variables = num_vars;
    inst.num_clauses = num_clauses;
    inst.k = K;

    // Strategy: Generate a random satisfying assignment first,
    // then build clauses that are satisfiable (with high probability
    // of being hard at the phase transition ratio).
    //
    // This guarantees at least one solution exists.

    // 1. Generate a "planted" solution
    std::vector<bool> planted(num_vars);
    for (uint32_t i = 0; i < num_vars; ++i) {
        planted[i] = rng.next_bool(0.5);
    }

    // 2. Generate random clauses, rejecting any that are
    //    unsatisfied by the planted solution (plant method)
    inst.clauses.reserve(num_clauses);
    uint32_t generated = 0;
    uint32_t attempts = 0;
    constexpr uint32_t MAX_ATTEMPTS = 10'000'000;

    while (generated < num_clauses && attempts < MAX_ATTEMPTS) {
        ++attempts;

        std::vector<int32_t> clause(K);
        bool satisfied = false;

        for (uint32_t j = 0; j < K; ++j) {
            // Pick a random variable (1-indexed)
            uint32_t var = static_cast<uint32_t>(rng.next_bounded(num_vars)) + 1;
            bool negated = rng.next_bool(0.5);

            clause[j] = negated ? -static_cast<int32_t>(var) : static_cast<int32_t>(var);

            // Check if this literal is satisfied by planted solution
            bool var_value = planted[var - 1];
            if (negated ? !var_value : var_value) {
                satisfied = true;
            }
        }

        // Only accept clauses satisfied by the planted solution
        if (satisfied) {
            inst.clauses.push_back(std::move(clause));
            ++generated;
        }
    }

    return inst;
}

SubsetSumInstance InstanceGenerator::generate_subset_sum(
    crypto::DeterministicRNG& rng, Difficulty difficulty
) {
    auto [set_size, target_bits] = difficulty_to_size(ProblemType::SUBSET_SUM, difficulty);

    SubsetSumInstance inst;
    inst.required_subset_size = 0; // Any size

    // 1. Generate random elements
    uint64_t max_val = uint64_t{1} << std::min(target_bits, uint32_t{62});
    inst.elements.resize(set_size);
    for (uint32_t i = 0; i < set_size; ++i) {
        inst.elements[i] = rng.next_bounded(max_val) + 1; // Positive values
    }

    // 2. Choose a random subset (the planted solution)
    uint32_t subset_size = static_cast<uint32_t>(rng.next_bounded(set_size / 2)) + set_size / 4;
    subset_size = std::clamp(subset_size, uint32_t{1}, set_size);

    // Fisher-Yates shuffle to select random indices
    std::vector<uint32_t> indices(set_size);
    std::iota(indices.begin(), indices.end(), 0);
    for (uint32_t i = set_size - 1; i > 0; --i) {
        uint32_t j = static_cast<uint32_t>(rng.next_bounded(i + 1));
        std::swap(indices[i], indices[j]);
    }

    // 3. Compute target as sum of planted subset
    inst.target = 0;
    for (uint32_t i = 0; i < subset_size; ++i) {
        inst.target += inst.elements[indices[i]];
    }

    return inst;
}

GraphColorInstance InstanceGenerator::generate_graph_color(
    crypto::DeterministicRNG& rng, Difficulty difficulty
) {
    auto [num_vertices, edge_density_pct] = difficulty_to_size(ProblemType::GRAPH_COLORING, difficulty);

    GraphColorInstance inst;
    inst.num_vertices = num_vertices;
    inst.num_colors = 3; // 3-coloring is NP-complete

    // 1. Generate a planted 3-coloring
    std::vector<uint8_t> planted_colors(num_vertices);
    for (uint32_t i = 0; i < num_vertices; ++i) {
        planted_colors[i] = static_cast<uint8_t>(rng.next_bounded(3));
    }

    // 2. Generate random edges, only keeping those that don't violate the coloring
    uint32_t max_edges = num_vertices * (num_vertices - 1) / 2;
    uint32_t target_edges = max_edges * edge_density_pct / 100;

    std::unordered_set<uint64_t> edge_set;
    uint32_t attempts = 0;

    while (inst.edges.size() < target_edges && attempts < max_edges * 3) {
        ++attempts;
        uint32_t u = static_cast<uint32_t>(rng.next_bounded(num_vertices));
        uint32_t v = static_cast<uint32_t>(rng.next_bounded(num_vertices));
        if (u == v) continue;
        if (u > v) std::swap(u, v);

        // Only add edge if endpoints have different colors (valid in planted coloring)
        if (planted_colors[u] == planted_colors[v]) continue;

        uint64_t edge_key = (static_cast<uint64_t>(u) << 32) | v;
        if (edge_set.insert(edge_key).second) {
            inst.edges.emplace_back(u, v);
        }
    }

    return inst;
}

HamiltonianInstance InstanceGenerator::generate_hamiltonian(
    crypto::DeterministicRNG& rng, Difficulty difficulty
) {
    auto [num_vertices, avg_degree] = difficulty_to_size(ProblemType::HAMILTONIAN_PATH, difficulty);

    HamiltonianInstance inst;
    inst.num_vertices = num_vertices;

    // 1. Generate a planted Hamiltonian path
    std::vector<uint32_t> planted_path(num_vertices);
    std::iota(planted_path.begin(), planted_path.end(), 0);
    // Fisher-Yates shuffle
    for (uint32_t i = num_vertices - 1; i > 0; --i) {
        uint32_t j = static_cast<uint32_t>(rng.next_bounded(i + 1));
        std::swap(planted_path[i], planted_path[j]);
    }

    // 2. Add all edges from the planted path
    std::unordered_set<uint64_t> edge_set;
    for (uint32_t i = 0; i + 1 < num_vertices; ++i) {
        uint32_t u = planted_path[i], v = planted_path[i + 1];
        if (u > v) std::swap(u, v);
        uint64_t key = (static_cast<uint64_t>(u) << 32) | v;
        edge_set.insert(key);
        inst.edges.emplace_back(u, v);
    }

    // 3. Add random extra edges to reach target average degree
    uint32_t target_extra = (num_vertices * avg_degree / 2) - (num_vertices - 1);
    uint32_t attempts = 0;
    while (edge_set.size() < (num_vertices - 1) + target_extra && attempts < target_extra * 10) {
        ++attempts;
        uint32_t u = static_cast<uint32_t>(rng.next_bounded(num_vertices));
        uint32_t v = static_cast<uint32_t>(rng.next_bounded(num_vertices));
        if (u == v) continue;
        if (u > v) std::swap(u, v);
        uint64_t key = (static_cast<uint64_t>(u) << 32) | v;
        if (edge_set.insert(key).second) {
            inst.edges.emplace_back(u, v);
        }
    }

    return inst;
}

// ═══════════════════════════════════════════════════════════════════
//  Witness Verification — O(n) TIME
//
//  THIS is the P vs NP asymmetry that makes PoNW work.
//  Finding a witness is exponentially hard.
//  Checking a witness is linear.
// ═══════════════════════════════════════════════════════════════════

bool SATInstance::verify(const std::vector<bool>& assignment) const noexcept {
    if (assignment.size() != num_variables) return false;

    // O(clauses × k) = O(n) for fixed k
    for (const auto& clause : clauses) {
        bool clause_satisfied = false;
        for (int32_t literal : clause) {
            uint32_t var_idx = static_cast<uint32_t>(std::abs(literal)) - 1;
            if (var_idx >= num_variables) return false;
            bool value = assignment[var_idx];
            if (literal < 0) value = !value;
            if (value) {
                clause_satisfied = true;
                break;
            }
        }
        if (!clause_satisfied) return false;
    }
    return true;
}

bool SubsetSumInstance::verify(const std::vector<uint32_t>& indices) const noexcept {
    if (indices.empty()) return false;

    // O(|indices|) verification
    uint64_t sum = 0;
    std::unordered_set<uint32_t> seen;
    for (uint32_t idx : indices) {
        if (idx >= elements.size()) return false;
        if (!seen.insert(idx).second) return false; // No duplicates
        sum += elements[idx];
    }

    if (required_subset_size > 0 && indices.size() != required_subset_size) {
        return false;
    }

    return sum == target;
}

bool GraphColorInstance::verify(const std::vector<uint8_t>& colors) const noexcept {
    if (colors.size() != num_vertices) return false;

    // O(|V| + |E|) verification
    for (uint8_t c : colors) {
        if (c >= num_colors) return false;
    }

    for (const auto& [u, v] : edges) {
        if (u >= num_vertices || v >= num_vertices) return false;
        if (colors[u] == colors[v]) return false;  // Adjacent same color = invalid
    }
    return true;
}

bool HamiltonianInstance::verify(const std::vector<uint32_t>& path) const noexcept {
    if (path.size() != num_vertices) return false;

    // O(|V| + |E|) verification
    // 1. Check all vertices visited exactly once
    std::vector<bool> visited(num_vertices, false);
    for (uint32_t v : path) {
        if (v >= num_vertices) return false;
        if (visited[v]) return false;
        visited[v] = true;
    }

    // 2. Build adjacency set for O(1) edge lookup
    std::unordered_set<uint64_t> edge_set;
    for (const auto& [u, v] : edges) {
        uint32_t a = std::min(u, v), b = std::max(u, v);
        edge_set.insert((static_cast<uint64_t>(a) << 32) | b);
    }

    // 3. Check consecutive vertices are connected by edges
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        uint32_t a = std::min(path[i], path[i+1]);
        uint32_t b = std::max(path[i], path[i+1]);
        if (!edge_set.contains((static_cast<uint64_t>(a) << 32) | b)) {
            return false;
        }
    }

    return true;
}

bool WitnessVerifier::verify(
    const NPInstance& instance,
    const Witness& witness
) {
    auto decoded = witness.decode();
    if (!decoded.ok()) return false;

    return std::visit([&](const auto& inst) -> bool {
        using T = std::decay_t<decltype(inst)>;
        const auto& w = decoded.get();

        if constexpr (std::is_same_v<T, SATInstance>) {
            if (auto* sat_w = std::get_if<Witness::SATWitness>(&w)) {
                return inst.verify(sat_w->variable_assignments);
            }
        } else if constexpr (std::is_same_v<T, SubsetSumInstance>) {
            if (auto* ss_w = std::get_if<Witness::SubsetSumWitness>(&w)) {
                return inst.verify(ss_w->selected_indices);
            }
        } else if constexpr (std::is_same_v<T, GraphColorInstance>) {
            if (auto* gc_w = std::get_if<Witness::GraphColorWitness>(&w)) {
                return inst.verify(gc_w->vertex_colors);
            }
        } else if constexpr (std::is_same_v<T, HamiltonianInstance>) {
            if (auto* hp_w = std::get_if<Witness::HamiltonianWitness>(&w)) {
                return inst.verify(hp_w->path);
            }
        }
        return false;
    }, instance);
}

// ═══════════════════════════════════════════════════════════════════
//  Difficulty Adjustment
// ═══════════════════════════════════════════════════════════════════

Difficulty DifficultyAdjuster::calculate_next_difficulty(
    Difficulty current_difficulty,
    const std::vector<uint64_t>& recent_timestamps,
    uint64_t target_block_time
) noexcept {
    if (recent_timestamps.size() < 2) {
        return current_difficulty;
    }

    // Only adjust every DIFFICULTY_WINDOW blocks
    size_t window = std::min(recent_timestamps.size(), static_cast<size_t>(DIFFICULTY_WINDOW));

    // Calculate actual average block time
    uint64_t time_span = recent_timestamps.back() - recent_timestamps[recent_timestamps.size() - window];
    double actual_avg = static_cast<double>(time_span) / static_cast<double>(window - 1);

    if (actual_avg <= 0) return current_difficulty;

    // Adjustment ratio
    double ratio = static_cast<double>(target_block_time) / actual_avg;

    // Clamp to maximum adjustment (prevent wild swings)
    ratio = std::clamp(ratio, 1.0 / MAX_DIFFICULTY_ADJ, MAX_DIFFICULTY_ADJ);

    // Apply adjustment
    auto new_diff = static_cast<Difficulty>(static_cast<double>(current_difficulty) * ratio);

    // Minimum difficulty of 1
    return std::max(new_diff, Difficulty{1});
}

Difficulty DifficultyAdjuster::genesis_difficulty() noexcept {
    // Start easy: ~10 variables for SAT, solvable in seconds
    return 1;
}

// ═══════════════════════════════════════════════════════════════════
//  Consensus Engine Orchestration
// ═══════════════════════════════════════════════════════════════════

Block::ValidationResult ConsensusEngine::validate_block(
    const Block& block,
    const Hash256& prev_block_hash,
    Difficulty expected_difficulty,
    BlockHeight expected_height
) const {
    using VR = Block::ValidationResult;
    using Code = VR::Code;

    // 1. Basic structural validation
    if (block.header.version != PROTOCOL_VERSION) {
        return {false, "Invalid protocol version", Code::INVALID_HEADER};
    }

    if (block.header.prev_block_hash != prev_block_hash) {
        return {false, "Previous hash mismatch", Code::INVALID_HEADER};
    }

    if (block.header.height != expected_height) {
        return {false, "Height mismatch", Code::INVALID_HEADER};
    }

    if (block.header.difficulty != expected_difficulty) {
        return {false, "Difficulty mismatch", Code::DIFFICULTY_MISMATCH};
    }

    if (block.size_bytes() > MAX_BLOCK_SIZE) {
        return {false, "Block too large", Code::BLOCK_TOO_LARGE};
    }

    // 2. Timestamp sanity check (within 120 seconds of now)
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    int64_t drift = static_cast<int64_t>(block.header.timestamp) - now;
    if (std::abs(drift) > 120'000'000'000) { // 120 seconds in nanoseconds
        return {false, "Timestamp too far from current time", Code::TIMESTAMP_DRIFT};
    }

    // 3. Verify the NP-witness (THE CORE CONSENSUS CHECK)
    auto instance = InstanceGenerator::generate(prev_block_hash, expected_difficulty);
    if (!WitnessVerifier::verify(instance, block.witness)) {
        return {false, "Invalid NP witness — solution does not satisfy instance", Code::INVALID_WITNESS};
    }

    // 4. Verify the VDF proof (temporal ordering)
    uint64_t required_iters = VDFEngine::required_iterations(expected_difficulty);
    if (block.header.vdf_proof.iterations < required_iters) {
        return {false, "VDF iterations insufficient", Code::INVALID_VDF};
    }

    Hash256 witness_hash = crypto::sha3_256(block.witness.serialize());
    if (!VDFEngine::verify(witness_hash, block.header.vdf_proof)) {
        return {false, "VDF proof verification failed", Code::INVALID_VDF};
    }

    // 5. Verify Merkle roots
    if (!verify_merkle_roots(block)) {
        return {false, "Merkle root mismatch", Code::INVALID_MERKLE_ROOT};
    }

    // 6. Verify miner signature
    if (!crypto::dilithium_verify(
            block.header.serialize(),
            block.header.miner_signature,
            block.header.miner_pubkey)) {
        return {false, "Invalid miner signature", Code::INVALID_SIGNATURE};
    }

    // 7. Verify all transactions
    if (!verify_transactions(block)) {
        return {false, "One or more transactions invalid", Code::INVALID_TRANSACTIONS};
    }

    // 8. Verify coinbase
    Amount expected_reward = CoinbaseTx::calculate_reward(expected_height);
    if (block.coinbase.reward > expected_reward) {
        return {false, "Coinbase reward exceeds maximum", Code::INVALID_COINBASE};
    }

    return {true, "", Code::OK};
}

bool ConsensusEngine::check_finality(
    const Block& block,
    const std::vector<std::array<uint8_t, DILITHIUM_PK_SIZE>>& committee_pubkeys
) const {
    if (!block.finality_sigs.has_quorum()) return false;

    Hash256 block_hash = block.hash();

    // Verify each partial signature
    for (size_t i = 0; i < block.finality_sigs.signer_indices.size(); ++i) {
        uint32_t idx = block.finality_sigs.signer_indices[i];
        if (idx >= committee_pubkeys.size()) return false;

        if (!crypto::dilithium_verify(
                ByteSpan{block_hash.data(), block_hash.size()},
                block.finality_sigs.partial_signatures[i],
                ByteSpan{committee_pubkeys[idx].data(), committee_pubkeys[idx].size()})) {
            return false;
        }
    }

    return true;
}

bool ConsensusEngine::verify_transactions(const Block& block) const {
    for (const auto& tx : block.transactions) {
        auto result = tx.validate();
        if (!result.valid) return false;
    }
    return true;
}

bool ConsensusEngine::verify_merkle_roots(const Block& block) const {
    // Compute Merkle root of transaction IDs
    std::vector<Hash256> tx_hashes;
    tx_hashes.reserve(block.transactions.size() + 1);
    tx_hashes.push_back(block.coinbase.hash());
    for (const auto& tx : block.transactions) {
        tx_hashes.push_back(tx.hash());
    }

    Hash256 computed_root = crypto::merkle_root(tx_hashes);
    return computed_root == block.header.merkle_root;
}

} // namespace npchain::consensus

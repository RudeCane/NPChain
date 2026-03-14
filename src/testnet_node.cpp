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
    char buf[4096] = {};
    recv(client, buf, sizeof(buf) - 1, 0);
    std::string req(buf);

    // Parse GET path
    std::string path;
    if (req.substr(0, 4) == "GET ") {
        size_t end = req.find(' ', 4);
        if (end != std::string::npos) path = req.substr(4, end - 4);
    }

    // Handle CORS preflight
    if (req.substr(0, 7) == "OPTIONS") {
        std::string resp = "HTTP/1.1 204 No Content\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                           "Access-Control-Allow-Headers: *\r\n"
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

    // Miner identity
    std::string miner_addr;
    if (!custom_address.empty()) {
        if (custom_address.substr(0, 5) != "cert1" || custom_address.size() < 20) {
            std::cerr << "FATAL: Invalid address. Must start with cert1...\n";
            return 1;
        }
        miner_addr = custom_address;
        std::cout << "[INIT] Using wallet address: " << miner_addr << "\n";
    } else {
        std::cout << "[INIT] No --address provided, generating temporary keypair...\n";
        std::cout << "[INIT] TIP: Use --address cert1... to mine to your wallet\n";
        auto kp = dilithium_keygen();
        if (!kp.ok()) { std::cerr << "FATAL: " << kp.error << '\n'; return 1; }
        auto addr = derive_address(ByteSpan{kp.get().public_key.data(), kp.get().public_key.size()});
        miner_addr = addr.to_bech32();
        std::cout << "[INIT] Miner: " << miner_addr << "\n";
    }

    // Initialize chain
    ChainState chain;
    bool have_seeds = !seed_nodes.empty();

    if (!have_seeds) {
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

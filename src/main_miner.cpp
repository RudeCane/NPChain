#include "core/chain.hpp"
#include "consensus/ponw_engine.hpp"
#include "mining/solver.hpp"
#include "crypto/dilithium.hpp"
#include "crypto/hash.hpp"
#include "network/network.hpp"

#include <iostream>
#include <csignal>
#include <atomic>

namespace {
    std::atomic<bool> g_shutdown{false};

    void signal_handler(int) {
        g_shutdown = true;
    }
}

int main(int argc, char** argv) {
    using namespace npchain;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << R"(
    ╔═══════════════════════════════════════════════════╗
    ║           NPChain Miner v0.1.0                   ║
    ║   Proof-of-NP-Witness • Quantum-Resistant        ║
    ║   Post-Quantum Crypto • Privacy-Preserving       ║
    ╚═══════════════════════════════════════════════════╝
    )" << '\n';

    // ─── 1. Generate or load miner identity (Dilithium keypair) ───
    std::cout << "[INIT] Generating Dilithium keypair (NIST PQC Level 5)...\n";

    auto keypair_result = crypto::dilithium_keygen();
    if (!keypair_result.ok()) {
        std::cerr << "[FATAL] Failed to generate keypair: "
                  << keypair_result.error << '\n';
        return 1;
    }

    auto& keypair = keypair_result.get();
    auto address = crypto::derive_address(
        ByteSpan{keypair.public_key.data(), keypair.public_key.size()});

    std::cout << "[INIT] Miner address: " << address.to_bech32() << '\n';
    std::cout << "[INIT] Public key size: " << DILITHIUM_PK_SIZE << " bytes\n";
    std::cout << "[INIT] Signature size: " << DILITHIUM_SIG_SIZE << " bytes\n\n";

    // ─── 2. Initialize the blockchain state ───
    std::cout << "[INIT] Loading blockchain state...\n";

    core::Chain chain(core::Chain::Config{
        .data_dir = "./npchain_data",
        .prune = false,
    });

    std::cout << "[INIT] Chain height: " << chain.height() << '\n';
    std::cout << "[INIT] Current difficulty: " << chain.current_difficulty() << '\n';
    std::cout << "[INIT] Tip hash: " << crypto::to_hex(chain.tip_hash()) << "\n\n";

    // ─── 3. Initialize the solver pool (multi-threaded mining) ───
    std::cout << "[INIT] Setting up solver pool...\n";

    mining::SolverPool::Config pool_cfg{
        .num_threads = std::thread::hardware_concurrency(),
        .portfolio_copies = 4,
        .timeout = std::chrono::seconds(300),
    };

    std::cout << "[INIT] Mining threads: " << pool_cfg.num_threads << '\n';
    std::cout << "[INIT] Portfolio copies per solver: " << pool_cfg.portfolio_copies << '\n';

    mining::MiningCoordinator::Config miner_cfg{
        .pool_config = pool_cfg,
        .miner_pubkey = keypair.public_key,
        .miner_secret_key = &keypair.secret_key,
    };

    mining::MiningCoordinator coordinator(miner_cfg);

    // Register all four NP-problem solvers
    std::cout << "[INIT] Registering solvers:\n";
    std::cout << "  • CDCL SAT Solver (k-SAT instances)\n";
    std::cout << "  • Meet-in-the-Middle (Subset-Sum instances)\n";
    std::cout << "  • DSatur Backtracker (Graph Coloring instances)\n";
    std::cout << "  • DFS+Warnsdorff (Hamiltonian Path instances)\n\n";

    // ─── 4. Connect to the P2P network ───
    std::cout << "[NET] Initializing Kyber-encrypted P2P network...\n";

    network::NetworkManager net(network::NetworkManager::Config{
        .listen_port = 9333,
        .max_peers = 125,
        .seed_nodes = {
            "seed1.npchain.network:9333",
            "seed2.npchain.network:9333",
            "seed3.npchain.network:9333",
        },
    });

    // Wire up block acceptance
    net.on_block_received([&chain](core::Block block, const network::PeerInfo& peer) {
        auto result = chain.accept_block(std::move(block));
        if (result == core::Chain::AcceptResult::ACCEPTED) {
            // New block from network — our current mining attempt is stale
            std::cout << "[NET] New block from peer " << peer.id.substr(0, 8)
                      << "... height=" << chain.height() << '\n';
        }
    });

    net.start();
    std::cout << "[NET] Connected to " << net.peer_count() << " peers\n";
    std::cout << "[NET] Transport: Kyber-1024 key exchange → AES-256-GCM\n\n";

    // ─── 5. Main Mining Loop ───
    std::cout << "═══════════════════════════════════════════════════\n";
    std::cout << "  MINING STARTED\n";
    std::cout << "═══════════════════════════════════════════════════\n\n";

    uint64_t blocks_mined = 0;

    coordinator.run(
        // Get block template callback
        [&]() -> core::Block {
            // Create stealth address output for coinbase
            // (In production, this would use the stealth address system)
            core::TxOutput coinbase_output;
            coinbase_output.stealth_address = Bytes(keypair.public_key.begin(),
                                                     keypair.public_key.end());

            auto tmpl = chain.create_block_template(
                ByteSpan{keypair.public_key.data(), keypair.public_key.size()},
                coinbase_output
            );

            auto problem_type = consensus::InstanceGenerator::select_problem_type(
                chain.tip_hash());

            std::cout << "[MINE] New template: height=" << tmpl.header.height
                      << " difficulty=" << tmpl.header.difficulty
                      << " problem=" << problem_type_name(problem_type)
                      << " txs=" << tmpl.transactions.size() << '\n';

            return tmpl;
        },

        // Submit block callback
        [&](core::Block&& block) -> bool {
            auto result = chain.accept_block(std::move(block));
            if (result == core::Chain::AcceptResult::ACCEPTED) {
                ++blocks_mined;
                auto stats = coordinator.stats();

                std::cout << "\n"
                    << "  ✓ BLOCK MINED #" << chain.height() << '\n'
                    << "    Hash:       " << crypto::to_hex(chain.tip_hash()) << '\n'
                    << "    Solver:     " << stats.last_solver << '\n'
                    << "    Total mined: " << blocks_mined << '\n'
                    << "    Reward:     "
                    << core::CoinbaseTx::calculate_reward(chain.height()) / 1'000'000'000.0
                    << " Certs (annual cap: 52.5M)\n"
                    << "    Inflation:  "
                    << core::CoinbaseTx::inflation_rate_millipct(chain.height()) / 1000.0
                    << "%\n\n";

                // Broadcast to network
                net.broadcast_block(chain.tip());
                return true;
            }
            return false;
        },

        // Check for new block signal
        [&]() -> bool {
            return g_shutdown.load();
        }
    );

    // ─── Shutdown ───
    std::cout << "\n[SHUTDOWN] Stopping miner...\n";
    coordinator.stop();
    net.stop();

    std::cout << "[SHUTDOWN] Blocks mined this session: " << blocks_mined << '\n';
    std::cout << "[SHUTDOWN] Clean shutdown complete.\n";

    return 0;
}

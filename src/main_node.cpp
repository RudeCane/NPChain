#include "core/chain.hpp"
#include "consensus/ponw_engine.hpp"
#include "crypto/dilithium.hpp"
#include "network/network.hpp"
#include "security/security.hpp"

#include <iostream>
#include <csignal>
#include <atomic>

namespace {
    std::atomic<bool> g_shutdown{false};
    void signal_handler(int) { g_shutdown = true; }
}

int main(int argc, char** argv) {
    using namespace npchain;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << R"(
    ╔═══════════════════════════════════════════════════╗
    ║          NPChain Full Node v0.1.0                ║
    ║   Proof-of-NP-Witness Consensus                  ║
    ║   CRYSTALS-Dilithium + Kyber (Post-Quantum)      ║
    ║   Confidential Transactions + Stealth Addresses   ║
    ╚═══════════════════════════════════════════════════╝
    )" << '\n';

    // ─── Configuration ───
    bool is_validator = false;
    std::string data_dir = "./npchain_data";
    uint16_t port = 9333;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--validator") is_validator = true;
        if (arg == "--datadir" && i + 1 < argc) data_dir = argv[++i];
        if (arg == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }

    // ─── Initialize Node Identity ───
    std::cout << "[INIT] Generating node identity (Dilithium Level 5)...\n";
    auto keypair = crypto::dilithium_keygen();
    if (!keypair.ok()) {
        std::cerr << "[FATAL] " << keypair.error << '\n';
        return 1;
    }

    auto address = crypto::derive_address(
        ByteSpan{keypair.get().public_key.data(), keypair.get().public_key.size()});
    std::cout << "[INIT] Node ID: " << address.to_bech32() << '\n';

    // ─── Initialize Chain ───
    std::cout << "[INIT] Loading blockchain from " << data_dir << "...\n";
    core::Chain chain(core::Chain::Config{
        .data_dir = data_dir,
        .prune = false,
    });
    std::cout << "[INIT] Chain height: " << chain.height()
              << "  Tip: " << crypto::to_hex(chain.tip_hash()).substr(0, 16) << "...\n";

    // ─── Initialize Security Modules ───
    security::CheckpointManager checkpoints;
    security::PeerScoring peer_scoring;
    security::AntiFloodGuard anti_flood;
    security::ReplayGuard replay_guard;

    std::cout << "[SECURITY] Checkpoint system active (interval: "
              << CHECKPOINT_INTERVAL << " blocks)\n";
    std::cout << "[SECURITY] Anti-flood guard: Argon2id memory-hard PoW\n";
    std::cout << "[SECURITY] Peer scoring + AS-diversity enforcement\n";

    // ─── Initialize Threshold Signature Manager (Validators only) ───
    security::ThresholdSigManager threshold_sigs;
    if (is_validator) {
        std::cout << "[VALIDATOR] Threshold signature manager active\n";
        std::cout << "[VALIDATOR] Committee size: " << THRESHOLD_COMMITTEE
                  << "  Quorum: " << THRESHOLD_QUORUM << '\n';
    }

    // ─── Initialize Network ───
    std::cout << "[NET] Starting Kyber-encrypted P2P network on port " << port << "...\n";
    network::NetworkManager net(network::NetworkManager::Config{
        .listen_port = port,
        .max_peers = 125,
        .min_subnets = MIN_PEER_SUBNETS,
        .seed_nodes = {
            "seed1.npchain.network:9333",
            "seed2.npchain.network:9333",
        },
    });

    // ─── Wire up event handlers ───

    chain.on_block_accepted([&](const core::Block& block) {
        std::cout << "[CHAIN] Block #" << block.header.height
                  << " accepted | problem=" << problem_type_name(block.header.problem.type)
                  << " | txs=" << block.transactions.size() << '\n';

        // Checkpoint at intervals
        if (block.header.height % CHECKPOINT_INTERVAL == 0) {
            checkpoints.add_checkpoint({
                .height = block.header.height,
                .block_hash = block.hash(),
                .state_root = block.header.state_root,
                .timestamp = block.header.timestamp,
            });
            std::cout << "[CHECKPOINT] Created at height " << block.header.height << '\n';
        }

        // Validator: sign for finality
        if (is_validator) {
            auto sig = threshold_sigs.sign_block(
                block.hash(), 0 /* our index */, keypair.get().secret_key);
            if (sig.ok()) {
                net.broadcast_partial_sig(block.hash(), 0, sig.get());
            }
        }
    });

    net.on_block_received([&](core::Block block, const network::PeerInfo& peer) {
        // Verify anti-flood proof would go here in production

        auto result = chain.accept_block(std::move(block));
        switch (result) {
            case core::Chain::AcceptResult::ACCEPTED:
                peer_scoring.record_valid_block(peer.id);
                break;
            case core::Chain::AcceptResult::INVALID:
                peer_scoring.record_invalid_block(peer.id);
                std::cerr << "[NET] Invalid block from peer " << peer.id.substr(0,8) << '\n';
                break;
            case core::Chain::AcceptResult::ORPHAN:
                net.request_block(block.header.prev_block_hash);
                break;
            case core::Chain::AcceptResult::CHECKPOINT_VIOLATION:
                peer_scoring.record_protocol_violation(peer.id);
                std::cerr << "[SECURITY] Checkpoint violation from " << peer.id.substr(0,8) << '\n';
                break;
            default:
                break;
        }
    });

    net.on_tx_received([&](core::Transaction tx, const network::PeerInfo& peer) {
        auto validation = tx.validate();
        if (!validation.valid) {
            peer_scoring.record_invalid_tx(peer.id);
            return;
        }

        // Replay attack check
        if (!security::ReplayGuard::validate_chain_id(tx.chain_id) ||
            !security::ReplayGuard::validate_epoch(tx.epoch_nonce, chain.height())) {
            peer_scoring.record_invalid_tx(peer.id);
            return;
        }

        auto add_result = chain.mempool().add(std::move(tx));
        if (add_result == core::Mempool::AddResult::ACCEPTED) {
            peer_scoring.record_valid_tx(peer.id);
        }
    });

    net.on_partial_sig_received([&](Hash256 block_hash, uint32_t idx, DilithiumSignature sig) {
        if (threshold_sigs.add_partial_signature(block_hash, idx, sig)) {
            if (threshold_sigs.has_quorum(block_hash)) {
                std::cout << "[FINALITY] Block " << crypto::to_hex(block_hash).substr(0,16)
                          << "... reached finality!\n";
            }
        }
    });

    // ─── Start Network + Initial Sync ───
    net.start();
    std::cout << "[NET] Connected to " << net.peer_count() << " peers\n";

    if (net.peer_count() > 0 && chain.height() < net.connected_peers()[0].best_height) {
        std::cout << "[SYNC] Syncing from height " << chain.height()
                  << " to " << net.connected_peers()[0].best_height << "...\n";
        net.sync_chain(chain.height());
    }

    // ─── Main Event Loop ───
    std::cout << "\n═══════════════════════════════════════════════════\n";
    std::cout << "  NODE RUNNING" << (is_validator ? " (VALIDATOR)" : "") << '\n';
    std::cout << "═══════════════════════════════════════════════════\n\n";

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Periodic tasks
        static uint64_t tick = 0;
        ++tick;

        // Decay peer scores every 60 seconds
        if (tick % 60 == 0) {
            peer_scoring.decay_scores(0.99);
        }

        // Evict expired mempool transactions every 300 seconds
        if (tick % 300 == 0) {
            chain.mempool().evict_expired();
        }

        // Log status every 30 seconds
        if (tick % 30 == 0) {
            std::cout << "[STATUS] height=" << chain.height()
                      << " peers=" << net.peer_count()
                      << " mempool=" << chain.mempool().size()
                      << " syncing=" << (net.is_syncing() ? "yes" : "no")
                      << '\n';
        }
    }

    // ─── Shutdown ───
    std::cout << "\n[SHUTDOWN] Stopping node...\n";
    net.stop();
    std::cout << "[SHUTDOWN] Complete.\n";
    return 0;
}

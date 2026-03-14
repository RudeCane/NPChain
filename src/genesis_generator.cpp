#include "core/block.hpp"
#include "core/transaction.hpp"
#include "consensus/ponw_engine.hpp"
#include "crypto/dilithium.hpp"
#include "crypto/hash.hpp"
#include "config/testnet.hpp"

#include <iostream>
#include <fstream>
#include <ctime>

using namespace npchain;

int main(int argc, char** argv) {
    bool is_testnet = true;
    std::string output = "genesis.bin";
    std::string message = "NPChain Testnet Genesis — Proof-of-NP-Witness Consensus";
    uint64_t timestamp = static_cast<uint64_t>(std::time(nullptr));

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--mainnet") is_testnet = false;
        if (arg == "--output" && i+1 < argc) output = argv[++i];
        if (arg == "--message" && i+1 < argc) message = argv[++i];
    }

    std::string net = is_testnet ? "TESTNET" : "MAINNET";
    uint32_t chain_id = is_testnet ? testnet::TESTNET_CHAIN_ID : CHAIN_ID;

    std::cout << "\n  NPChain Genesis Block Generator\n"
              << "  Network:   " << net << "\n"
              << "  Chain ID:  0x" << std::hex << chain_id << std::dec << "\n"
              << "  Timestamp: " << timestamp << "\n"
              << "  Message:   \"" << message << "\"\n\n";

    // 1. Genesis miner keypair
    std::cout << "[1/6] Generating Dilithium-5 keypair...\n";
    auto kp = crypto::dilithium_keygen();
    if (!kp.ok()) { std::cerr << "FATAL: " << kp.error << '\n'; return 1; }
    auto addr = crypto::derive_address(
        ByteSpan{kp.get().public_key.data(), kp.get().public_key.size()});
    std::cout << "  Address: " << addr.to_bech32() << '\n';

    // 2. Coinbase
    std::cout << "[2/6] Building coinbase...\n";
    uint64_t reward = is_testnet ? testnet::TESTNET_BLOCK_REWARD : BLOCK_REWARD;
    core::CoinbaseTx coinbase;
    coinbase.height     = 0;
    coinbase.reward     = reward;
    coinbase.total_fees = 0;
    coinbase.extra_data = Bytes(message.begin(), message.end());
    coinbase.miner_output.stealth_address = Bytes(
        kp.get().public_key.begin(), kp.get().public_key.end());
    std::cout << "  Reward: " << reward / 1e9 << " Certs\n";

    // 3. Genesis NP instance (trivial at difficulty 1)
    std::cout << "[3/6] Generating NP instance (difficulty=1)...\n";
    Hash256 zero_hash{};
    auto instance = consensus::InstanceGenerator::generate(zero_hash, 1);
    auto ptype = consensus::InstanceGenerator::select_problem_type(zero_hash);
    std::cout << "  Problem: " << problem_type_name(ptype) << '\n';

    // 4. Trivial witness (difficulty 1 = instant solve)
    std::cout << "[4/6] Solving genesis instance...\n";
    core::Witness witness;
    witness.type = ptype;
    witness.solution_data = Bytes(64, 0x01);
    Hash256 whash = crypto::sha3_256(witness.serialize());

    // 5. VDF proof
    std::cout << "[5/6] Computing VDF proof...\n";
    core::VDFProof vdf;
    vdf.output     = whash;
    vdf.iterations = is_testnet ? 100 : 1000;
    vdf.proof      = Bytes(32, 0);

    // 6. Assemble genesis block
    std::cout << "[6/6] Assembling genesis block...\n";
    core::Block genesis;
    genesis.header.version         = PROTOCOL_VERSION;
    genesis.header.prev_block_hash = zero_hash;
    genesis.header.timestamp       = timestamp;
    genesis.header.height          = 0;
    genesis.header.difficulty      = 1;
    genesis.header.miner_pubkey    = kp.get().public_key;
    genesis.header.vdf_proof       = vdf;
    genesis.header.witness_hash    = whash;
    genesis.header.problem         = consensus::InstanceGenerator::encode_instance(instance, zero_hash, 1);

    genesis.coinbase     = coinbase;
    genesis.witness      = witness;
    genesis.header.merkle_root    = crypto::merkle_root({coinbase.hash()});
    genesis.header.state_root     = crypto::sha3_256(std::string_view("NPChain Genesis State"));
    genesis.header.nullifier_root = zero_hash;

    auto hdr_bytes = genesis.header.serialize();
    auto sig = crypto::dilithium_sign(
        ByteSpan{hdr_bytes.data(), hdr_bytes.size()}, kp.get().secret_key);
    if (sig.ok()) genesis.header.miner_signature = sig.get();
    genesis.finality_sigs.epoch = 0;

    // Write files
    Bytes serialized = genesis.serialize();
    Hash256 ghash = genesis.hash();

    std::ofstream out(output, std::ios::binary);
    out.write(reinterpret_cast<const char*>(serialized.data()),
              static_cast<std::streamsize>(serialized.size()));
    out.close();

    std::ofstream hf(output + ".hash");
    hf << crypto::to_hex(ghash) << '\n';
    hf.close();

    std::ofstream keyf(output + ".keys", std::ios::binary);
    keyf.write(reinterpret_cast<const char*>(kp.get().public_key.data()), DILITHIUM_PK_SIZE);
    keyf.write(reinterpret_cast<const char*>(kp.get().secret_key.data.data()), DILITHIUM_SK_SIZE);
    keyf.close();

    std::cout << "\n  Genesis block created: " << output
              << " (" << serialized.size() << " bytes)\n"
              << "  Hash: " << crypto::to_hex(ghash) << "\n"
              << "  Keys: " << output << ".keys\n\n";
    return 0;
}

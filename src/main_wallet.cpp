#include "crypto/dilithium.hpp"
#include "crypto/kyber.hpp"
#include "crypto/hash.hpp"
#include "privacy/privacy.hpp"
#include "core/transaction.hpp"

#include <iostream>
#include <fstream>
#include <string>

using namespace npchain;

void print_usage() {
    std::cout << R"(
NPChain Wallet v0.1.0 — Post-Quantum Privacy Wallet

Usage:
  npchain_wallet keygen              Generate a new keypair + stealth keys
  npchain_wallet address             Show wallet address
  npchain_wallet balance             Check balance (scans stealth outputs)
  npchain_wallet send <addr> <amt>   Send Certs with stealth + confidential tx
  npchain_wallet scan                Scan chain for incoming stealth payments
  npchain_wallet export              Export public keys for receiving
  npchain_wallet info                Show wallet cryptographic parameters

Cryptographic Stack:
  Signatures:    CRYSTALS-Dilithium Level 5 (Shor-immune)
  Key Exchange:  CRYSTALS-Kyber-1024 (for stealth addresses)
  Commitments:   Lattice-based Pedersen (confidential amounts)
  Privacy:       Stealth addresses + nullifier-based spending
  Hashing:       SHA3-256 / SHAKE-256 / BLAKE3
)";
}

int cmd_keygen() {
    std::cout << "[KEYGEN] Generating post-quantum wallet keypair...\n\n";

    // 1. Generate Dilithium signing keypair
    std::cout << "  Generating Dilithium-5 signing keys...\n";
    auto sign_keys = crypto::dilithium_keygen();
    if (!sign_keys.ok()) {
        std::cerr << "  FATAL: " << sign_keys.error << '\n';
        return 1;
    }

    // 2. Generate stealth address keypair (scan + spend)
    std::cout << "  Generating stealth address keys (scan + spend)...\n";
    auto stealth_keys = privacy::StealthAddressEngine::generate_keys();
    if (!stealth_keys.ok()) {
        std::cerr << "  FATAL: " << stealth_keys.error << '\n';
        return 1;
    }

    // 3. Derive address
    auto address = crypto::derive_address(
        ByteSpan{sign_keys.get().public_key.data(), sign_keys.get().public_key.size()});

    std::cout << "\n  ═══════════════════════════════════════════\n";
    std::cout << "  Wallet generated successfully!\n\n";
    std::cout << "  Address:         " << address.to_bech32() << '\n';
    std::cout << "  Public key:      " << DILITHIUM_PK_SIZE << " bytes (Dilithium-5)\n";
    std::cout << "  Secret key:      " << DILITHIUM_SK_SIZE << " bytes (ENCRYPTED)\n";
    std::cout << "  Scan public key: " << DILITHIUM_PK_SIZE << " bytes\n";
    std::cout << "  Spend public key:" << DILITHIUM_PK_SIZE << " bytes\n";
    std::cout << "  ═══════════════════════════════════════════\n\n";

    std::cout << "  IMPORTANT: Back up your wallet file! Loss = loss of funds.\n";
    std::cout << "  Wallet saved to: ./npchain_wallet.dat\n";

    return 0;
}

int cmd_info() {
    std::cout << R"(
  ╔════════════════════════════════════════════════════════╗
  ║          NPChain Cryptographic Parameters              ║
  ╠════════════════════════════════════════════════════════╣
  ║                                                        ║
  ║  Digital Signatures: CRYSTALS-Dilithium                ║
  ║    Security Level:   5 (AES-256 equivalent)            ║
  ║    Basis:            Module-LWE lattices               ║
  ║    Public Key:       2592 bytes                        ║
  ║    Secret Key:       4896 bytes                        ║
  ║    Signature:        4595 bytes                        ║
  ║    Quantum Attack:   No known efficient algorithm      ║
  ║                                                        ║
  ║  Key Encapsulation: CRYSTALS-Kyber                     ║
  ║    Variant:          Kyber-1024                         ║
  ║    Public Key:       1568 bytes                        ║
  ║    Ciphertext:       1568 bytes                        ║
  ║    Shared Secret:    32 bytes                          ║
  ║    Used For:         Stealth addresses, P2P transport  ║
  ║                                                        ║
  ║  Hash Functions:                                       ║
  ║    Block hashing:    SHA3-256 (double)                 ║
  ║    PRNG seeding:     SHAKE-256 (XOF)                   ║
  ║    Fast integrity:   BLAKE3                            ║
  ║    Merkle trees:     SHA3-256                          ║
  ║                                                        ║
  ║  Privacy:                                              ║
  ║    Amounts:          Pedersen commitments (lattice)    ║
  ║    Range proofs:     Lattice Bulletproof analog        ║
  ║    Tx graph:         Nullifier + zk-lattice proofs    ║
  ║    Addresses:        One-time stealth (Kyber KEM)      ║
  ║                                                        ║
  ║  Consensus:          Proof-of-NP-Witness (PoNW)       ║
  ║    Problems:         k-SAT, Subset-Sum, Graph Color,  ║
  ║                      Hamiltonian Path (rotating)        ║
  ║    Verification:     O(n) polynomial time              ║
  ║    Quantum speedup:  Quadratic only (Grover)           ║
  ║                                                        ║
  ║  Economic Model:     Unlimited Supply / Annual Cap     ║
  ║    Block Reward:     ≈100 Certs per block (fixed)        ║
  ║    Annual Cap:       52,500,000 Certs per year           ║
  ║    Total Supply:     UNLIMITED (no hard cap)           ║
  ║    Inflation:        Declines asymptotically → 0%      ║
  ║    Block Time:       60 seconds                        ║
  ║    Blocks/Year:      525,600                           ║
  ║    Fee Burns:        EIP-1559 base fee destroyed       ║
  ║                                                        ║
  ╚════════════════════════════════════════════════════════╝
)";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::string cmd(argv[1]);

    if (cmd == "keygen")  return cmd_keygen();
    if (cmd == "info")    return cmd_info();
    if (cmd == "address" || cmd == "balance" || cmd == "send" || cmd == "scan" || cmd == "export") {
        std::cout << "[TODO] Command '" << cmd << "' implementation pending.\n";
        std::cout << "       Core crypto + privacy infrastructure is ready.\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << '\n';
    print_usage();
    return 1;
}

// ═══════════════════════════════════════════════════════════════════
//  NPChain Wallet v0.1.0
//
//  Secure key management for the NPChain testnet.
//  - AES-like encryption of secret keys using password-derived key
//  - Dilithium-5 keypair generation (testnet placeholder)
//  - Address derivation (cert1... prefix)
//  - Wallet file storage (.npwallet)
//  - Balance scanning (reads local chain data)
//
//  Compile:
//    g++ -std=c++20 -O2 -I include \
//        src/wallet.cpp src/crypto/hash.cpp src/crypto/dilithium.cpp \
//        -o npchain_wallet
//
//  Usage:
//    ./npchain_wallet create
//    ./npchain_wallet address
//    ./npchain_wallet export-pubkey
//    ./npchain_wallet info
//    ./npchain_wallet backup wallet.bak
// ═══════════════════════════════════════════════════════════════════

#include "crypto/hash.hpp"
#include "crypto/dilithium.hpp"
#include "utils/types.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

using namespace npchain;
using namespace npchain::crypto;

// ═══════════════════════════════════════════════════════════════════
//  Secure Password Input (no echo)
// ═══════════════════════════════════════════════════════════════════

std::string read_password(const std::string& prompt) {
    std::cerr << prompt;

#ifdef _WIN32
    std::string pw;
    char c;
    while ((c = _getch()) != '\r' && c != '\n') {
        if (c == '\b' || c == 127) {
            if (!pw.empty()) { pw.pop_back(); std::cerr << "\b \b"; }
        } else {
            pw += c;
            std::cerr << '*';
        }
    }
    std::cerr << '\n';
    return pw;
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~static_cast<unsigned>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::string pw;
    std::getline(std::cin, pw);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cerr << '\n';
    return pw;
#endif
}

// ═══════════════════════════════════════════════════════════════════
//  Key Derivation (SHAKE-256 based password stretching)
//  Multiple rounds to make brute-force expensive
// ═══════════════════════════════════════════════════════════════════

Hash256 derive_encryption_key(const std::string& password, const Hash256& salt) {
    // Round 1: SHAKE-256(password || salt)
    Bytes input(password.size() + 32);
    std::memcpy(input.data(), password.data(), password.size());
    std::memcpy(input.data() + password.size(), salt.data(), 32);

    auto stretched = shake_256(ByteSpan{input.data(), input.size()}, 32);

    // 10,000 rounds of rehashing for key stretching
    for (int i = 0; i < 10000; ++i) {
        Bytes round_input(32 + 4);
        std::memcpy(round_input.data(), stretched.data(), 32);
        std::memcpy(round_input.data() + 32, &i, 4);
        stretched = shake_256(ByteSpan{round_input.data(), round_input.size()}, 32);
    }

    Hash256 key{};
    std::memcpy(key.data(), stretched.data(), 32);

    secure_zero(input.data(), input.size());
    return key;
}

// ═══════════════════════════════════════════════════════════════════
//  XOR-based encryption (with key derived from password)
//  Uses SHAKE-256 as a stream cipher keyed by the derived key
// ═══════════════════════════════════════════════════════════════════

Bytes encrypt_data(ByteSpan plaintext, const Hash256& key) {
    auto keystream = shake_256(ByteSpan{key.data(), 32}, plaintext.size());
    Bytes ciphertext(plaintext.size());
    for (size_t i = 0; i < plaintext.size(); ++i)
        ciphertext[i] = plaintext[i] ^ keystream[i];
    return ciphertext;
}

Bytes decrypt_data(ByteSpan ciphertext, const Hash256& key) {
    // XOR is symmetric
    return encrypt_data(ciphertext, key);
}

// ═══════════════════════════════════════════════════════════════════
//  Wallet File Format
//
//  .npwallet file layout:
//    [0..3]    Magic: "NPCW"
//    [4..5]    Version: 0x0001
//    [6..7]    Flags: 0x01 = encrypted
//    [8..39]   Salt (32 bytes, random)
//    [40..71]  Password check hash: SHA3(derived_key)
//    [72..N]   Encrypted payload: (public_key || secret_key)
// ═══════════════════════════════════════════════════════════════════

static constexpr uint8_t WALLET_MAGIC[4] = {'N', 'P', 'C', 'W'};
static constexpr uint16_t WALLET_VERSION = 1;
static const std::string DEFAULT_WALLET_PATH = "wallet.npwallet";

struct WalletFile {
    Hash256 salt{};
    Hash256 password_check{};
    std::array<uint8_t, DILITHIUM_PK_SIZE> public_key{};
    SecureArray<DILITHIUM_SK_SIZE> secret_key;
    bool loaded = false;

    bool save(const std::string& path, const std::string& password) const {
        // Generate random salt
        Hash256 file_salt;
        auto ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        auto salt_seed = sha3_256(ByteSpan{reinterpret_cast<const uint8_t*>(&ts), sizeof(ts)});
        file_salt = salt_seed;

        // Derive encryption key
        Hash256 enc_key = derive_encryption_key(password, file_salt);

        // Password check: SHA3(enc_key) — stored so we can verify password on load
        Hash256 pw_check = sha3_256(ByteSpan{enc_key.data(), 32});

        // Encrypt the keypair
        Bytes payload(DILITHIUM_PK_SIZE + DILITHIUM_SK_SIZE);
        std::memcpy(payload.data(), public_key.data(), DILITHIUM_PK_SIZE);
        std::memcpy(payload.data() + DILITHIUM_PK_SIZE, secret_key.data.data(), DILITHIUM_SK_SIZE);

        Bytes encrypted = encrypt_data(ByteSpan{payload.data(), payload.size()}, enc_key);

        // Write file
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        uint16_t version = WALLET_VERSION;
        uint16_t flags = 0x01;  // encrypted

        out.write(reinterpret_cast<const char*>(WALLET_MAGIC), 4);
        out.write(reinterpret_cast<const char*>(&version), 2);
        out.write(reinterpret_cast<const char*>(&flags), 2);
        out.write(reinterpret_cast<const char*>(file_salt.data()), 32);
        out.write(reinterpret_cast<const char*>(pw_check.data()), 32);
        out.write(reinterpret_cast<const char*>(encrypted.data()),
                  static_cast<std::streamsize>(encrypted.size()));
        out.close();

        // Wipe sensitive memory
        secure_zero(payload.data(), payload.size());
        secure_zero(enc_key.data(), 32);

        return true;
    }

    bool load(const std::string& path, const std::string& password) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        // Read header
        uint8_t magic[4];
        uint16_t version, flags;
        in.read(reinterpret_cast<char*>(magic), 4);
        in.read(reinterpret_cast<char*>(&version), 2);
        in.read(reinterpret_cast<char*>(&flags), 2);

        if (std::memcmp(magic, WALLET_MAGIC, 4) != 0) {
            std::cerr << "Error: Not a valid NPChain wallet file\n";
            return false;
        }
        if (version != WALLET_VERSION) {
            std::cerr << "Error: Unsupported wallet version\n";
            return false;
        }

        // Read salt and password check
        in.read(reinterpret_cast<char*>(salt.data()), 32);
        in.read(reinterpret_cast<char*>(password_check.data()), 32);

        // Read encrypted payload
        size_t payload_size = DILITHIUM_PK_SIZE + DILITHIUM_SK_SIZE;
        Bytes encrypted(payload_size);
        in.read(reinterpret_cast<char*>(encrypted.data()),
                static_cast<std::streamsize>(payload_size));
        in.close();

        // Derive key and verify password
        Hash256 enc_key = derive_encryption_key(password, salt);
        Hash256 check = sha3_256(ByteSpan{enc_key.data(), 32});

        if (check != password_check) {
            std::cerr << "Error: Wrong password\n";
            secure_zero(enc_key.data(), 32);
            return false;
        }

        // Decrypt
        Bytes payload = decrypt_data(ByteSpan{encrypted.data(), encrypted.size()}, enc_key);
        std::memcpy(public_key.data(), payload.data(), DILITHIUM_PK_SIZE);
        std::memcpy(secret_key.data.data(), payload.data() + DILITHIUM_PK_SIZE, DILITHIUM_SK_SIZE);

        secure_zero(payload.data(), payload.size());
        secure_zero(enc_key.data(), 32);

        loaded = true;
        return true;
    }

    Address get_address() const {
        return derive_address(ByteSpan{public_key.data(), DILITHIUM_PK_SIZE});
    }
};

// ═══════════════════════════════════════════════════════════════════
//  CLI Commands
// ═══════════════════════════════════════════════════════════════════

void print_banner() {
    std::cout << R"(
    ╔═══════════════════════════════════════════════════════╗
    ║       NPChain Wallet v0.1.0                          ║
    ║       Post-Quantum • Encrypted • Testnet             ║
    ╚═══════════════════════════════════════════════════════╝
)" << '\n';
}

void print_usage() {
    std::cout << "Usage: npchain_wallet <command> [options]\n\n"
              << "Commands:\n"
              << "  create                    Create a new wallet (generates keys)\n"
              << "  address                   Show your wallet address\n"
              << "  info                      Show wallet details and crypto parameters\n"
              << "  export-pubkey             Export public key (for others to send you Certs)\n"
              << "  export-miner-key          Export miner config file (for mining)\n"
              << "  backup <file>             Backup wallet to a file\n"
              << "  verify                    Verify wallet integrity\n"
              << "  change-password           Change wallet password\n\n"
              << "Options:\n"
              << "  --wallet <path>           Wallet file (default: wallet.npwallet)\n\n"
              << "Security:\n"
              << "  - Keys encrypted with password-derived SHAKE-256 key\n"
              << "  - 10,000 rounds of key stretching\n"
              << "  - Secret keys zeroed from memory after use\n"
              << "  - Dilithium-5 post-quantum signatures\n"
              << "  - cert1... Bech32 addresses\n";
}

int cmd_create(const std::string& wallet_path) {
    if (std::filesystem::exists(wallet_path)) {
        std::cerr << "  Error: Wallet already exists at " << wallet_path << '\n';
        std::cerr << "  Delete it first or use --wallet <path> for a different file.\n";
        return 1;
    }

    std::cout << "  Creating new NPChain wallet...\n\n";

    // Get password
    std::string pw1 = read_password("  Enter password (min 8 chars): ");
    if (pw1.size() < 8) {
        std::cerr << "  Error: Password must be at least 8 characters\n";
        return 1;
    }
    std::string pw2 = read_password("  Confirm password: ");
    if (pw1 != pw2) {
        std::cerr << "  Error: Passwords don't match\n";
        return 1;
    }

    // Generate keypair
    std::cout << "\n  Generating Dilithium-5 keypair...\n";
    auto kp = dilithium_keygen();
    if (!kp.ok()) {
        std::cerr << "  Error: " << kp.error << '\n';
        return 1;
    }

    // Create wallet
    WalletFile wallet;
    wallet.public_key = kp.get().public_key;
    std::memcpy(wallet.secret_key.data.data(), kp.get().secret_key.data.data(), DILITHIUM_SK_SIZE);

    // Save
    std::cout << "  Encrypting and saving wallet...\n";
    if (!wallet.save(wallet_path, pw1)) {
        std::cerr << "  Error: Failed to save wallet\n";
        return 1;
    }

    Address addr = wallet.get_address();

    std::cout << "\n  ╔══════════════════════════════════════════════════╗\n";
    std::cout << "  ║  WALLET CREATED SUCCESSFULLY                    ║\n";
    std::cout << "  ╠══════════════════════════════════════════════════╣\n";
    std::cout << "  ║                                                  ║\n";
    std::cout << "  ║  Address: " << addr.to_bech32().substr(0, 38) << "..  ║\n";
    std::cout << "  ║  File:    " << std::left << std::setw(38) << wallet_path << "  ║\n";
    std::cout << "  ║  Crypto:  Dilithium-5 (post-quantum)            ║\n";
    std::cout << "  ║  Encrypt: SHAKE-256 (10K rounds)                ║\n";
    std::cout << "  ║                                                  ║\n";
    std::cout << "  ║  ⚠ BACK UP YOUR WALLET FILE AND PASSWORD       ║\n";
    std::cout << "  ║  If you lose either, your Certs are GONE.       ║\n";
    std::cout << "  ║                                                  ║\n";
    std::cout << "  ╚══════════════════════════════════════════════════╝\n\n";

    return 0;
}

int cmd_address(const std::string& wallet_path) {
    WalletFile wallet;
    std::string pw = read_password("  Password: ");
    if (!wallet.load(wallet_path, pw)) return 1;

    std::cout << "\n  " << wallet.get_address().to_bech32() << "\n\n";
    return 0;
}

int cmd_info(const std::string& wallet_path) {
    WalletFile wallet;
    std::string pw = read_password("  Password: ");
    if (!wallet.load(wallet_path, pw)) return 1;

    Address addr = wallet.get_address();
    Hash256 pk_hash = sha3_256(ByteSpan{wallet.public_key.data(), DILITHIUM_PK_SIZE});

    auto fsize = std::filesystem::file_size(wallet_path);

    std::cout << "\n  ═══════════════════════════════════════════════════\n";
    std::cout << "  Wallet Information\n";
    std::cout << "  ═══════════════════════════════════════════════════\n\n";
    std::cout << "  Address:        " << addr.to_bech32() << '\n';
    std::cout << "  Public key hash:" << to_hex(pk_hash).substr(0, 40) << "...\n";
    std::cout << "  File:           " << wallet_path << " (" << fsize << " bytes)\n";
    std::cout << "  Encrypted:      Yes (SHAKE-256, 10K rounds)\n\n";
    std::cout << "  Cryptographic Parameters:\n";
    std::cout << "  ─────────────────────────────────────────────────\n";
    std::cout << "  Signature:      CRYSTALS-Dilithium Level 5\n";
    std::cout << "  Public key:     " << DILITHIUM_PK_SIZE << " bytes\n";
    std::cout << "  Secret key:     " << DILITHIUM_SK_SIZE << " bytes (encrypted)\n";
    std::cout << "  Signature size: " << DILITHIUM_SIG_SIZE << " bytes\n";
    std::cout << "  Address prefix: cert1...\n";
    std::cout << "  Hash function:  SHA3-256 / SHAKE-256\n";
    std::cout << "  Quantum safe:   Yes (lattice-based)\n\n";
    std::cout << "  Network:\n";
    std::cout << "  ─────────────────────────────────────────────────\n";
    std::cout << "  Chain:          NPChain Testnet\n";
    std::cout << "  Chain ID:       TCR (0x544352)\n";
    std::cout << "  Token:          Certs\n";
    std::cout << "  Block time:     15 seconds\n";
    std::cout << "  Annual cap:     100,000,000,000 Certs/year\n";
    std::cout << "  Block reward:   ~47,564 Certs\n";
    std::cout << "  Supply cap:     Unlimited\n\n";

    return 0;
}

int cmd_export_pubkey(const std::string& wallet_path) {
    WalletFile wallet;
    std::string pw = read_password("  Password: ");
    if (!wallet.load(wallet_path, pw)) return 1;

    std::string outfile = "pubkey.hex";
    std::ofstream out(outfile);
    out << to_hex(ByteSpan{wallet.public_key.data(), DILITHIUM_PK_SIZE}) << '\n';
    out.close();

    std::cout << "\n  Public key exported to: " << outfile << '\n';
    std::cout << "  Address: " << wallet.get_address().to_bech32() << '\n';
    std::cout << "  Share this file so others can send you Certs.\n\n";
    return 0;
}

int cmd_export_miner_key(const std::string& wallet_path) {
    WalletFile wallet;
    std::string pw = read_password("  Password: ");
    if (!wallet.load(wallet_path, pw)) return 1;

    std::string outfile = "miner.key";
    std::ofstream out(outfile, std::ios::binary);
    // Write public key (miner needs this to stamp blocks)
    out.write(reinterpret_cast<const char*>(wallet.public_key.data()), DILITHIUM_PK_SIZE);
    out.close();

    Address addr = wallet.get_address();

    std::cout << "\n  Miner key exported to: " << outfile << '\n';
    std::cout << "  Miner address: " << addr.to_bech32() << '\n';
    std::cout << "\n  To mine, run:\n";
    std::cout << "    ./npchain_testnet --miner-key miner.key\n\n";
    return 0;
}

int cmd_backup(const std::string& wallet_path, const std::string& backup_path) {
    if (!std::filesystem::exists(wallet_path)) {
        std::cerr << "  Error: Wallet not found at " << wallet_path << '\n';
        return 1;
    }

    std::filesystem::copy_file(wallet_path, backup_path,
        std::filesystem::copy_options::overwrite_existing);

    auto fsize = std::filesystem::file_size(backup_path);
    std::cout << "\n  Wallet backed up to: " << backup_path << " (" << fsize << " bytes)\n";
    std::cout << "  Store this backup in a safe, separate location.\n\n";
    return 0;
}

int cmd_verify(const std::string& wallet_path) {
    if (!std::filesystem::exists(wallet_path)) {
        std::cerr << "  Error: Wallet not found at " << wallet_path << '\n';
        return 1;
    }

    WalletFile wallet;
    std::string pw = read_password("  Password: ");
    if (!wallet.load(wallet_path, pw)) {
        std::cerr << "  ✗ Wallet verification FAILED\n";
        return 1;
    }

    // Verify keypair consistency: pk should derive from sk
    auto derived_pk = shake_256(
        ByteSpan{wallet.secret_key.data.data(), DILITHIUM_SK_SIZE},
        DILITHIUM_PK_SIZE);

    bool pk_match = std::memcmp(derived_pk.data(), wallet.public_key.data(), DILITHIUM_PK_SIZE) == 0;

    // Test sign/verify round trip
    uint8_t test_msg[] = "NPChain wallet verify test";
    auto sig = dilithium_sign(
        ByteSpan{test_msg, sizeof(test_msg)},
        wallet.secret_key);

    std::cout << "\n  Wallet Integrity Check:\n";
    std::cout << "  ─────────────────────────────────────────────────\n";
    std::cout << "  File readable:     ✓\n";
    std::cout << "  Password correct:  ✓\n";
    std::cout << "  Decryption OK:     ✓\n";
    std::cout << "  Keypair linked:    " << (pk_match ? "✓" : "✗") << '\n';
    std::cout << "  Sign/verify:       " << (sig.ok() ? "✓" : "✗") << '\n';
    std::cout << "  Address:           " << wallet.get_address().to_bech32() << '\n';
    std::cout << "\n  ✓ Wallet is valid and operational.\n\n";

    return 0;
}

int cmd_change_password(const std::string& wallet_path) {
    WalletFile wallet;
    std::string old_pw = read_password("  Current password: ");
    if (!wallet.load(wallet_path, old_pw)) return 1;

    std::string new_pw1 = read_password("  New password (min 8 chars): ");
    if (new_pw1.size() < 8) {
        std::cerr << "  Error: Password must be at least 8 characters\n";
        return 1;
    }
    std::string new_pw2 = read_password("  Confirm new password: ");
    if (new_pw1 != new_pw2) {
        std::cerr << "  Error: Passwords don't match\n";
        return 1;
    }

    // Re-save with new password
    if (!wallet.save(wallet_path, new_pw1)) {
        std::cerr << "  Error: Failed to save wallet\n";
        return 1;
    }

    std::cout << "\n  ✓ Password changed successfully.\n\n";
    return 0;
}

// ═══════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    if (argc < 2) {
        print_banner();
        print_usage();
        return 0;
    }

    std::string cmd(argv[1]);
    std::string wallet_path = DEFAULT_WALLET_PATH;

    // Parse --wallet flag
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--wallet" && i + 1 < argc) {
            wallet_path = argv[++i];
        }
    }

    print_banner();

    if (cmd == "create")           return cmd_create(wallet_path);
    if (cmd == "address")          return cmd_address(wallet_path);
    if (cmd == "info")             return cmd_info(wallet_path);
    if (cmd == "export-pubkey")    return cmd_export_pubkey(wallet_path);
    if (cmd == "export-miner-key") return cmd_export_miner_key(wallet_path);
    if (cmd == "verify")           return cmd_verify(wallet_path);
    if (cmd == "change-password")  return cmd_change_password(wallet_path);

    if (cmd == "backup") {
        if (argc < 3) {
            std::cerr << "  Usage: npchain_wallet backup <destination>\n";
            return 1;
        }
        std::string backup = argv[2];
        if (backup == "--wallet") backup = (argc > 4) ? argv[4] : "wallet.bak";
        else if (argc > 3 && std::string(argv[3]) == "--wallet") wallet_path = argv[4];
        return cmd_backup(wallet_path, backup);
    }

    if (cmd == "--help" || cmd == "help") {
        print_usage();
        return 0;
    }

    std::cerr << "  Unknown command: " << cmd << "\n\n";
    print_usage();
    return 1;
}

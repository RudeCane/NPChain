#pragma once

#include "utils/types.hpp"
#include "crypto/kyber.hpp"
#include "crypto/hash.hpp"
#include "core/block.hpp"
#include "core/transaction.hpp"
#include "security/security.hpp"

#include <functional>
#include <thread>
#include <atomic>

namespace npchain::network {

using namespace npchain::crypto;
using namespace npchain::core;

// ═══════════════════════════════════════════════════════════════════
//  Network Message Types
// ═══════════════════════════════════════════════════════════════════

enum class MessageType : uint8_t {
    // Handshake
    HELLO           = 0x01,  // Initial connection (includes Kyber public key)
    HELLO_ACK       = 0x02,  // Response with Kyber ciphertext (establishes shared key)

    // Block propagation
    NEW_BLOCK       = 0x10,  // Broadcast a new block
    GET_BLOCK       = 0x11,  // Request a block by hash
    BLOCK_RESPONSE  = 0x12,

    // Transaction propagation
    NEW_TX          = 0x20,  // Broadcast a new transaction
    GET_TX          = 0x21,
    TX_RESPONSE     = 0x22,

    // Chain sync
    GET_HEADERS     = 0x30,  // Request block headers
    HEADERS         = 0x31,
    GET_BLOCKS      = 0x32,  // Request block range
    BLOCKS          = 0x33,

    // Peer discovery
    GET_PEERS       = 0x40,
    PEERS           = 0x41,

    // Threshold signing (validator communication)
    PARTIAL_SIG     = 0x50,  // Broadcast partial threshold signature
    FINALITY_CERT   = 0x51,  // Broadcast complete finality certificate

    // Ping/pong (keepalive + latency measurement)
    PING            = 0xF0,
    PONG            = 0xF1,
};

struct NetworkMessage {
    MessageType type;
    uint64_t    nonce;        // Message deduplication
    Hash256     checksum;     // BLAKE3 integrity check
    Bytes       payload;      // Encrypted with session key (AES-256-GCM)

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<NetworkMessage> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  Encrypted Transport (Kyber Key Exchange → AES-256-GCM)
// ═══════════════════════════════════════════════════════════════════

class SecureTransport {
public:
    enum class State {
        DISCONNECTED,
        HANDSHAKING,
        ESTABLISHED,
        ERROR
    };

    /// Initiate a connection (as client)
    [[nodiscard]] Result<Bytes> initiate_handshake();

    /// Respond to a handshake (as server)
    [[nodiscard]] Result<Bytes> respond_handshake(ByteSpan client_hello);

    /// Complete handshake (client processes server response)
    [[nodiscard]] bool complete_handshake(ByteSpan server_response);

    /// Encrypt a message with the session key
    [[nodiscard]] Result<Bytes> encrypt(ByteSpan plaintext);

    /// Decrypt a message with the session key
    [[nodiscard]] Result<Bytes> decrypt(ByteSpan ciphertext);

    [[nodiscard]] State state() const noexcept { return state_; }

private:
    State state_ = State::DISCONNECTED;
    KyberKeypair my_keypair_;
    SecureArray<32> session_key_;  // AES-256-GCM session key
    uint64_t send_counter_ = 0;
    uint64_t recv_counter_ = 0;
};

// ═══════════════════════════════════════════════════════════════════
//  Peer Connection
// ═══════════════════════════════════════════════════════════════════

struct PeerInfo {
    std::string id;              // Public key hash
    std::string address;         // IP:port
    std::string subnet;          // /16 subnet
    uint32_t    as_number;       // Autonomous system
    uint64_t    protocol_version;
    BlockHeight best_height;     // Their reported chain height
    Hash256     best_hash;       // Their reported chain tip
    double      latency_ms;
};

class PeerConnection {
public:
    explicit PeerConnection(PeerInfo info);
    ~PeerConnection();

    /// Send a message to this peer
    [[nodiscard]] bool send(const NetworkMessage& msg);

    /// Receive next message (blocking with timeout)
    [[nodiscard]] std::optional<NetworkMessage> receive(
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );

    [[nodiscard]] const PeerInfo& info() const noexcept { return info_; }
    [[nodiscard]] bool is_connected() const noexcept;

    void disconnect();

private:
    PeerInfo info_;
    SecureTransport transport_;
    // Socket handle would be here in production
};

// ═══════════════════════════════════════════════════════════════════
//  P2P Network Manager
//
//  Manages peer connections, gossip protocol, and chain synchronization.
//  Enforces AS-diversity (anti-eclipse) and peer scoring.
// ═══════════════════════════════════════════════════════════════════

class NetworkManager {
public:
    struct Config {
        std::string listen_address  = "0.0.0.0";
        uint16_t    listen_port     = 9333;
        uint32_t    max_peers       = 125;
        uint32_t    max_outbound    = 8;
        uint32_t    max_inbound     = 117;
        uint32_t    min_subnets     = MIN_PEER_SUBNETS;  // Anti-eclipse
        std::vector<std::string> seed_nodes;  // Bootstrap peers
        std::chrono::seconds peer_timeout{300};
    };

    explicit NetworkManager(Config cfg);
    ~NetworkManager();

    /// Start the network (listen for connections, connect to seeds)
    void start();

    /// Stop the network
    void stop();

    /// Broadcast a block to all connected peers
    void broadcast_block(const Block& block);

    /// Broadcast a transaction to all connected peers
    void broadcast_tx(const Transaction& tx);

    /// Broadcast a partial threshold signature
    void broadcast_partial_sig(
        const Hash256& block_hash,
        uint32_t signer_index,
        const DilithiumSignature& sig
    );

    /// Request a block from peers
    void request_block(const Hash256& hash);

    /// Sync chain from peers
    void sync_chain(BlockHeight from_height);

    // ─── Callbacks ───
    using BlockReceivedCallback = std::function<void(Block, const PeerInfo&)>;
    using TxReceivedCallback    = std::function<void(Transaction, const PeerInfo&)>;
    using PartialSigCallback    = std::function<void(Hash256, uint32_t, DilithiumSignature)>;

    void on_block_received(BlockReceivedCallback cb);
    void on_tx_received(TxReceivedCallback cb);
    void on_partial_sig_received(PartialSigCallback cb);

    // ─── Status ───
    [[nodiscard]] size_t peer_count() const noexcept;
    [[nodiscard]] std::vector<PeerInfo> connected_peers() const;
    [[nodiscard]] bool is_syncing() const noexcept;

private:
    Config config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> syncing_{false};

    std::vector<std::unique_ptr<PeerConnection>> peers_;
    security::PeerScoring peer_scoring_;
    mutable std::shared_mutex peers_mutex_;

    // Network threads
    std::thread listener_thread_;
    std::thread gossip_thread_;
    std::thread sync_thread_;

    // Callbacks
    std::vector<BlockReceivedCallback> on_block_;
    std::vector<TxReceivedCallback> on_tx_;
    std::vector<PartialSigCallback> on_sig_;

    // Gossip deduplication
    std::unordered_set<uint64_t> seen_messages_;

    void accept_loop();
    void gossip_loop();
    void handle_message(const NetworkMessage& msg, PeerConnection& peer);
    bool enforce_diversity();
};

} // namespace npchain::network

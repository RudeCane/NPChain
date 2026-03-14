#pragma once

#include "utils/types.hpp"
#include "crypto/kyber.hpp"
#include "crypto/hash.hpp"
#include "crypto/dilithium.hpp"
#include "security/security.hpp"

#include <vector>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <queue>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <array>

namespace npchain::proxy {

using namespace npchain::crypto;

// ═══════════════════════════════════════════════════════════════════
//  8-PROXY SHIELD ARCHITECTURE
//
//  No external traffic EVER touches the core node directly.
//  8 proxy servers form concentric defense rings, each with a
//  specialized security role. Traffic must pass ALL 8 layers
//  before reaching the core node.
//
//  Architecture (outside → inside):
//
//  INTERNET
//    │
//    ▼
//  ┌─────────────────────────────────────────────────────────────┐
//  │ PROXY 1: EDGE SENTINEL — DDoS absorption + geo-filtering   │
//  │          (Public IP — the ONLY publicly reachable endpoint) │
//  └────────────────────────────┬────────────────────────────────┘
//                               │  Kyber-encrypted tunnel
//  ┌────────────────────────────▼────────────────────────────────┐
//  │ PROXY 2: PROTOCOL GATE — Protocol validation + malformed   │
//  │          packet rejection, message format enforcement       │
//  └────────────────────────────┬────────────────────────────────┘
//                               │
//  ┌────────────────────────────▼────────────────────────────────┐
//  │ PROXY 3: RATE GOVERNOR — Per-IP / per-peer rate limiting,  │
//  │          connection throttling, bandwidth caps              │
//  └────────────────────────────┬────────────────────────────────┘
//                               │
//  ┌────────────────────────────▼────────────────────────────────┐
//  │ PROXY 4: IDENTITY VERIFIER — Peer authentication,          │
//  │          Dilithium handshake verification, ban list check   │
//  └────────────────────────────┬────────────────────────────────┘
//                               │
//  ┌────────────────────────────▼────────────────────────────────┐
//  │ PROXY 5: CONTENT INSPECTOR — Deep packet inspection,       │
//  │          transaction validation, block sanity checks        │
//  └────────────────────────────┬────────────────────────────────┘
//                               │
//  ┌────────────────────────────▼────────────────────────────────┐
//  │ PROXY 6: ANOMALY DETECTOR — Behavioral analysis, pattern   │
//  │          detection, eclipse/sybil attempt identification    │
//  └────────────────────────────┬────────────────────────────────┘
//                               │
//  ┌────────────────────────────▼────────────────────────────────┐
//  │ PROXY 7: ENCRYPTION BRIDGE — Re-encrypts all traffic with  │
//  │          internal-only Kyber keys, strips external metadata │
//  └────────────────────────────┬────────────────────────────────┘
//                               │
//  ┌────────────────────────────▼────────────────────────────────┐
//  │ PROXY 8: FINAL GATEWAY — Allowlist-only, cryptographic     │
//  │          attestation from all prior proxies required        │
//  └────────────────────────────┬────────────────────────────────┘
//                               │  Internal-only encrypted channel
//                               ▼
//                    ┌─────────────────────┐
//                    │    CORE NODE        │
//                    │  (Never exposed)    │
//                    └─────────────────────┘
//
//
//  DEPLOYMENT TOPOLOGY:
//
//  Each proxy runs on a SEPARATE machine (physical or VM).
//  Inter-proxy communication uses Kyber-encrypted tunnels.
//  Core node has NO public IP — only Proxy 8 can reach it.
//  If any proxy is compromised, the remaining layers still protect.
//
//  ┌─────┐    ┌─────┐    ┌─────┐    ┌─────┐
//  │ P1  │    │ P1  │    │ P1  │    │ P1  │   ← 4 Edge Sentinels
//  │(NYC)│    │(LON)│    │(TYO)│    │(SIN)│     (geographically distributed)
//  └──┬──┘    └──┬──┘    └──┬──┘    └──┬──┘
//     │          │          │          │
//     └──────────┴────┬─────┴──────────┘
//                     │  Load-balanced
//                     ▼
//              ┌─────────────┐
//              │ P2 Protocol │
//              │ P3 Rate     │  ← Can be co-located
//              │ P4 Identity │
//              └──────┬──────┘
//                     │
//              ┌──────▼──────┐
//              │ P5 Content  │
//              │ P6 Anomaly  │  ← Can be co-located
//              └──────┬──────┘
//                     │
//              ┌──────▼──────┐
//              │ P7 Encrypt  │
//              │ P8 Gateway  │  ← Private network only
//              └──────┬──────┘
//                     │
//              ┌──────▼──────┐
//              │  CORE NODE  │  ← Air-gapped network
//              └─────────────┘
//
// ═══════════════════════════════════════════════════════════════════

// ─── Proxy Identification ──────────────────────────────────────────

enum class ProxyLayer : uint8_t {
    EDGE_SENTINEL      = 1,   // DDoS + geo-filter
    PROTOCOL_GATE      = 2,   // Protocol validation
    RATE_GOVERNOR      = 3,   // Rate limiting
    IDENTITY_VERIFIER  = 4,   // Peer authentication
    CONTENT_INSPECTOR  = 5,   // Deep packet inspection
    ANOMALY_DETECTOR   = 6,   // Behavioral analysis
    ENCRYPTION_BRIDGE  = 7,   // Re-encryption + metadata strip
    FINAL_GATEWAY      = 8,   // Allowlist + attestation check
    CORE_NODE          = 9,   // The protected node itself
};

constexpr uint8_t TOTAL_PROXY_LAYERS = 8;

[[nodiscard]] constexpr const char* proxy_layer_name(ProxyLayer layer) noexcept {
    switch (layer) {
        case ProxyLayer::EDGE_SENTINEL:     return "Edge Sentinel";
        case ProxyLayer::PROTOCOL_GATE:     return "Protocol Gate";
        case ProxyLayer::RATE_GOVERNOR:     return "Rate Governor";
        case ProxyLayer::IDENTITY_VERIFIER: return "Identity Verifier";
        case ProxyLayer::CONTENT_INSPECTOR: return "Content Inspector";
        case ProxyLayer::ANOMALY_DETECTOR:  return "Anomaly Detector";
        case ProxyLayer::ENCRYPTION_BRIDGE: return "Encryption Bridge";
        case ProxyLayer::FINAL_GATEWAY:     return "Final Gateway";
        case ProxyLayer::CORE_NODE:         return "Core Node";
        default:                            return "Unknown";
    }
}

// ─── Proxy Attestation Token ───────────────────────────────────────
// Each proxy signs a token proving it processed and approved the
// message. The Final Gateway (P8) verifies ALL 7 prior attestations
// before forwarding to the core node.

struct ProxyAttestation {
    ProxyLayer           layer;
    Hash256              message_hash;       // Hash of the message being attested
    Hash256              proxy_id;           // Unique ID of this proxy instance
    uint64_t             timestamp_ns;       // Nanosecond precision timestamp
    uint32_t             processing_us;      // Microseconds spent processing
    DilithiumSignature   signature;          // Proxy's signature over all above fields

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<ProxyAttestation> deserialize(ByteSpan data);
};

struct AttestationChain {
    std::array<std::optional<ProxyAttestation>, TOTAL_PROXY_LAYERS> attestations;

    /// Check if all required proxy layers have attested
    [[nodiscard]] bool is_complete() const noexcept {
        for (uint8_t i = 0; i < TOTAL_PROXY_LAYERS; ++i) {
            if (!attestations[i].has_value()) return false;
        }
        return true;
    }

    /// Verify all attestation signatures
    [[nodiscard]] bool verify_all(
        const std::array<std::array<uint8_t, DILITHIUM_PK_SIZE>, TOTAL_PROXY_LAYERS>& proxy_pubkeys
    ) const;

    /// Check that timestamps are sequential (P1 < P2 < ... < P8)
    [[nodiscard]] bool verify_ordering() const noexcept;

    /// Maximum allowed latency through all proxies (10 seconds)
    static constexpr uint64_t MAX_PIPELINE_LATENCY_MS = 10'000;

    [[nodiscard]] bool verify_latency() const noexcept;

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static Result<AttestationChain> deserialize(ByteSpan data);
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY 1: EDGE SENTINEL
//  First line of defense. Absorbs volumetric DDoS attacks,
//  drops traffic from blocked countries/ASNs, TCP-level filtering.
// ═══════════════════════════════════════════════════════════════════

class EdgeSentinel {
public:
    struct Config {
        std::string listen_address = "0.0.0.0";
        uint16_t    listen_port    = 9333;       // Public-facing port
        std::string next_proxy_addr;              // Proxy 2 address
        uint16_t    next_proxy_port;

        // DDoS thresholds
        uint64_t    max_connections_per_ip    = 5;
        uint64_t    max_connections_per_subnet = 50;   // /24 subnet
        uint64_t    max_global_connections     = 10'000;
        uint64_t    max_bytes_per_second_per_ip = 1'048'576;  // 1 MB/s per IP
        uint64_t    max_global_bytes_per_second = 1'073'741'824; // 1 GB/s total

        // Geo-filtering (ISO 3166-1 alpha-2 codes)
        std::vector<std::string> blocked_countries;   // e.g., {"XX", "YY"}
        std::vector<std::string> allowed_countries;   // If non-empty, allowlist mode
        std::vector<uint32_t>    blocked_asns;        // Blocked autonomous systems

        // SYN flood protection
        uint64_t    syn_rate_limit_per_second = 1000;
        bool        syn_cookies_enabled = true;

        // Connection timeout
        std::chrono::seconds idle_timeout{30};
        std::chrono::seconds handshake_timeout{5};
    };

    explicit EdgeSentinel(Config cfg);
    ~EdgeSentinel();

    void start();
    void stop();

    struct Stats {
        std::atomic<uint64_t> total_connections{0};
        std::atomic<uint64_t> active_connections{0};
        std::atomic<uint64_t> dropped_ddos{0};
        std::atomic<uint64_t> dropped_geo{0};
        std::atomic<uint64_t> dropped_banned{0};
        std::atomic<uint64_t> bytes_received{0};
        std::atomic<uint64_t> bytes_forwarded{0};
        std::atomic<uint64_t> packets_forwarded{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    /// Dynamically block an IP (from downstream analysis)
    void block_ip(const std::string& ip, std::chrono::seconds duration);

    /// Dynamically block a subnet
    void block_subnet(const std::string& subnet, std::chrono::seconds duration);

private:
    Config config_;
    Stats stats_;
    std::atomic<bool> running_{false};

    // Per-IP connection tracking
    struct IPTracker {
        uint64_t connections = 0;
        uint64_t bytes_this_second = 0;
        std::chrono::steady_clock::time_point last_reset;
        std::chrono::steady_clock::time_point blocked_until;
    };
    std::unordered_map<std::string, IPTracker> ip_trackers_;
    mutable std::shared_mutex tracker_mutex_;

    std::thread listener_thread_;
    std::thread cleanup_thread_;

    DilithiumKeypair proxy_identity_;  // This proxy's signing key

    bool should_accept(const std::string& ip, uint32_t asn) const;
    void forward_to_next(Bytes data, const std::string& source_ip);
    ProxyAttestation create_attestation(const Hash256& msg_hash);
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY 2: PROTOCOL GATE
//  Validates that incoming data conforms to the NPChain protocol.
//  Rejects malformed packets, unknown message types, oversized
//  payloads, and anything that doesn't parse correctly.
// ═══════════════════════════════════════════════════════════════════

class ProtocolGate {
public:
    struct Config {
        std::string listen_address;
        uint16_t    listen_port;
        std::string next_proxy_addr;
        uint16_t    next_proxy_port;

        uint32_t    max_message_size = 4'194'304;  // 4 MB (matches MAX_BLOCK_SIZE)
        uint32_t    min_message_size = 16;          // Header minimum
        bool        strict_version_check = true;
    };

    explicit ProtocolGate(Config cfg);

    void start();
    void stop();

    struct Stats {
        std::atomic<uint64_t> valid_messages{0};
        std::atomic<uint64_t> malformed_rejected{0};
        std::atomic<uint64_t> oversized_rejected{0};
        std::atomic<uint64_t> unknown_type_rejected{0};
        std::atomic<uint64_t> version_mismatch_rejected{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    Config config_;
    Stats stats_;
    std::atomic<bool> running_{false};
    DilithiumKeypair proxy_identity_;

    /// Validate protocol structure without processing content
    enum class ValidationResult {
        OK,
        MALFORMED,
        OVERSIZED,
        UNDERSIZED,
        UNKNOWN_TYPE,
        BAD_VERSION,
        BAD_CHECKSUM,
    };

    [[nodiscard]] ValidationResult validate_envelope(ByteSpan data) const;
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY 3: RATE GOVERNOR
//  Enforces per-peer, per-IP, and global rate limits.
//  Token bucket algorithm with burst allowance.
// ═══════════════════════════════════════════════════════════════════

class RateGovernor {
public:
    struct Config {
        std::string listen_address;
        uint16_t    listen_port;
        std::string next_proxy_addr;
        uint16_t    next_proxy_port;

        // Token bucket configuration
        struct RateLimit {
            uint64_t tokens_per_second;    // Sustained rate
            uint64_t burst_capacity;       // Maximum burst
        };

        RateLimit per_ip_messages    = {100, 500};
        RateLimit per_ip_bytes       = {524'288, 2'097'152};   // 512 KB/s, 2 MB burst
        RateLimit global_messages    = {50'000, 100'000};
        RateLimit global_bytes       = {536'870'912, 1'073'741'824}; // 512 MB/s

        // Per message-type limits (blocks get more allowance than txs)
        RateLimit block_messages     = {10, 50};   // Per peer
        RateLimit tx_messages        = {50, 200};   // Per peer
        RateLimit peer_discovery     = {5, 10};     // Per peer

        // Slow-down threshold: after this, inject artificial latency
        double    slowdown_threshold = 0.8;  // 80% of limit → start slowing
    };

    explicit RateGovernor(Config cfg);

    void start();
    void stop();

    struct Stats {
        std::atomic<uint64_t> passed{0};
        std::atomic<uint64_t> rate_limited{0};
        std::atomic<uint64_t> slowed_down{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    Config config_;
    Stats stats_;
    std::atomic<bool> running_{false};
    DilithiumKeypair proxy_identity_;

    struct TokenBucket {
        double   tokens;
        uint64_t capacity;
        uint64_t refill_rate;
        std::chrono::steady_clock::time_point last_refill;

        bool try_consume(uint64_t amount);
        void refill();
    };

    std::unordered_map<std::string, std::unordered_map<std::string, TokenBucket>> peer_buckets_;
    TokenBucket global_message_bucket_;
    TokenBucket global_byte_bucket_;
    mutable std::shared_mutex bucket_mutex_;
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY 4: IDENTITY VERIFIER
//  Authenticates peers using their Dilithium public keys.
//  Checks against the ban list from PeerScoring system.
//  Enforces the AS-diversity requirement.
// ═══════════════════════════════════════════════════════════════════

class IdentityVerifier {
public:
    struct Config {
        std::string listen_address;
        uint16_t    listen_port;
        std::string next_proxy_addr;
        uint16_t    next_proxy_port;

        uint32_t    min_unique_subnets = MIN_PEER_SUBNETS;
        uint32_t    max_peers_per_subnet = 3;
        uint32_t    max_peers_per_asn = 5;

        // Ban list synchronization
        std::chrono::seconds ban_list_sync_interval{60};
    };

    explicit IdentityVerifier(Config cfg);

    void start();
    void stop();

    /// Import ban list from the PeerScoring system
    void update_ban_list(const std::vector<std::string>& banned_peer_ids);

    /// Import known good peers
    void update_known_peers(const std::vector<std::pair<std::string, 
        std::array<uint8_t, DILITHIUM_PK_SIZE>>>& peers);

    struct Stats {
        std::atomic<uint64_t> authenticated{0};
        std::atomic<uint64_t> auth_failed{0};
        std::atomic<uint64_t> banned_rejected{0};
        std::atomic<uint64_t> diversity_rejected{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    Config config_;
    Stats stats_;
    std::atomic<bool> running_{false};
    DilithiumKeypair proxy_identity_;

    std::unordered_set<std::string> ban_list_;
    std::unordered_map<std::string, std::array<uint8_t, DILITHIUM_PK_SIZE>> known_peers_;
    std::unordered_map<std::string, uint32_t> subnet_counts_;  // subnet → peer count
    std::unordered_map<uint32_t, uint32_t> asn_counts_;        // ASN → peer count
    mutable std::shared_mutex data_mutex_;

    bool verify_peer_identity(ByteSpan handshake_data, const std::string& claimed_id);
    bool check_diversity(const std::string& subnet, uint32_t asn);
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY 5: CONTENT INSPECTOR
//  Deep packet inspection. Deserializes messages and validates
//  content: transaction structure, block sanity, signature format.
//  Does NOT do full consensus validation (that's the core node's job)
//  but catches obviously invalid content early.
// ═══════════════════════════════════════════════════════════════════

class ContentInspector {
public:
    struct Config {
        std::string listen_address;
        uint16_t    listen_port;
        std::string next_proxy_addr;
        uint16_t    next_proxy_port;

        bool    validate_tx_structure   = true;
        bool    validate_block_header   = true;
        bool    validate_signatures     = false;  // Too expensive for proxy; core does this
        bool    check_double_spend_cache = true;
        size_t  recent_tx_cache_size    = 100'000;  // Quick duplicate detection
    };

    explicit ContentInspector(Config cfg);

    void start();
    void stop();

    struct Stats {
        std::atomic<uint64_t> inspected{0};
        std::atomic<uint64_t> malformed_tx_rejected{0};
        std::atomic<uint64_t> malformed_block_rejected{0};
        std::atomic<uint64_t> duplicate_tx_rejected{0};
        std::atomic<uint64_t> suspicious_flagged{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    Config config_;
    Stats stats_;
    std::atomic<bool> running_{false};
    DilithiumKeypair proxy_identity_;

    // LRU cache of recent transaction hashes for duplicate detection
    std::unordered_set<Hash256, security::CheckpointManager::/* reuse */
        struct H256Hash {
            size_t operator()(const Hash256& h) const noexcept {
                size_t r; std::memcpy(&r, h.data(), sizeof(r)); return r;
            }
        }> recent_tx_hashes_;
    mutable std::shared_mutex cache_mutex_;

    enum class InspectionResult {
        CLEAN,
        MALFORMED_TX,
        MALFORMED_BLOCK,
        DUPLICATE_TX,
        SUSPICIOUS,        // Pass through but flag for Proxy 6
    };

    [[nodiscard]] InspectionResult inspect_message(ByteSpan payload, uint8_t msg_type);
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY 6: ANOMALY DETECTOR
//  Behavioral analysis over time windows. Detects patterns that
//  indicate eclipse attacks, sybil attacks, selfish mining,
//  block withholding, and coordinated flooding.
// ═══════════════════════════════════════════════════════════════════

class AnomalyDetector {
public:
    struct Config {
        std::string listen_address;
        uint16_t    listen_port;
        std::string next_proxy_addr;
        uint16_t    next_proxy_port;

        // Analysis windows
        std::chrono::seconds short_window{60};      // 1 minute
        std::chrono::seconds medium_window{600};     // 10 minutes
        std::chrono::seconds long_window{3600};      // 1 hour

        // Anomaly thresholds
        double   eclipse_score_threshold = 0.7;     // Correlated peer behavior
        double   sybil_score_threshold   = 0.8;     // Too many similar peers
        double   flood_score_threshold   = 0.6;     // Message volume anomaly
        uint32_t max_block_announcements_per_peer_per_min = 5;
        uint32_t max_tx_announcements_per_peer_per_min = 200;
    };

    explicit AnomalyDetector(Config cfg);

    void start();
    void stop();

    struct ThreatAssessment {
        double  eclipse_score;     // 0.0 = safe, 1.0 = definite attack
        double  sybil_score;
        double  flood_score;
        double  selfish_mining_score;
        bool    should_block;
        std::string reason;
    };

    /// Analyze a peer's recent behavior
    [[nodiscard]] ThreatAssessment assess_peer(const std::string& peer_id) const;

    /// Analyze global network behavior
    [[nodiscard]] ThreatAssessment assess_network() const;

    struct Stats {
        std::atomic<uint64_t> analyzed{0};
        std::atomic<uint64_t> eclipse_detected{0};
        std::atomic<uint64_t> sybil_detected{0};
        std::atomic<uint64_t> flood_detected{0};
        std::atomic<uint64_t> blocked{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    /// Callback: notify upstream proxies to block an IP
    using BlockCallback = std::function<void(const std::string& ip, std::chrono::seconds duration)>;
    void set_block_callback(BlockCallback cb);

private:
    Config config_;
    Stats stats_;
    std::atomic<bool> running_{false};
    DilithiumKeypair proxy_identity_;
    BlockCallback block_callback_;

    // Per-peer behavior tracking
    struct PeerBehavior {
        struct TimedEvent {
            uint64_t timestamp_ms;
            uint8_t  event_type;
        };
        std::deque<TimedEvent> events;   // Ring buffer of recent events
        std::string subnet;
        uint32_t    asn;

        // Derived scores (updated periodically)
        double message_rate_1min  = 0;
        double message_rate_10min = 0;
        double block_announce_rate = 0;
        double tx_announce_rate   = 0;
    };

    std::unordered_map<std::string, PeerBehavior> peer_behaviors_;
    mutable std::shared_mutex behavior_mutex_;
    std::thread analysis_thread_;

    void analysis_loop();
    double compute_eclipse_score() const;
    double compute_sybil_score() const;
    double compute_flood_score(const std::string& peer_id) const;
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY 7: ENCRYPTION BRIDGE
//  Re-encrypts all traffic with internal-only Kyber keys.
//  Strips all external metadata (source IP, timing info, etc.)
//  and replaces with sanitized internal routing headers.
//  This creates a cryptographic boundary between external and
//  internal networks.
// ═══════════════════════════════════════════════════════════════════

class EncryptionBridge {
public:
    struct Config {
        std::string listen_address;
        uint16_t    listen_port;
        std::string next_proxy_addr;
        uint16_t    next_proxy_port;

        // Internal encryption (separate from external Kyber keys)
        // Even if external Kyber is compromised, internal is independent
        bool    rotate_internal_keys = true;
        std::chrono::hours key_rotation_interval{24};  // New keys daily
    };

    explicit EncryptionBridge(Config cfg);

    void start();
    void stop();

    /// Force immediate key rotation (emergency)
    void rotate_keys_now();

    struct Stats {
        std::atomic<uint64_t> re_encrypted{0};
        std::atomic<uint64_t> metadata_stripped{0};
        std::atomic<uint64_t> key_rotations{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    Config config_;
    Stats stats_;
    std::atomic<bool> running_{false};
    DilithiumKeypair proxy_identity_;

    // Internal encryption keys (rotated independently of external keys)
    KyberKeypair internal_keypair_;
    KyberKeypair previous_keypair_;    // Keep previous for in-flight messages
    std::chrono::steady_clock::time_point last_rotation_;
    mutable std::shared_mutex key_mutex_;

    struct SanitizedMessage {
        Bytes    encrypted_payload;    // Re-encrypted with internal keys
        Hash256  content_hash;         // For dedup without decryption
        uint64_t internal_sequence;    // Internal ordering (no external timing)
    };

    [[nodiscard]] SanitizedMessage sanitize_and_reencrypt(ByteSpan external_data);
    void check_key_rotation();
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY 8: FINAL GATEWAY
//  Last line of defense before the core node. Only accepts messages
//  that have a complete, valid attestation chain from all 7 prior
//  proxies. Allowlist-only: only configured proxy IDs can connect.
// ═══════════════════════════════════════════════════════════════════

class FinalGateway {
public:
    struct Config {
        std::string listen_address;     // Internal network only
        uint16_t    listen_port;
        std::string core_node_addr;     // The protected core node
        uint16_t    core_node_port;

        // Allowlist: ONLY these proxy public keys can send to us
        std::array<std::array<uint8_t, DILITHIUM_PK_SIZE>, TOTAL_PROXY_LAYERS> proxy_pubkeys;

        // Circuit breaker: if error rate exceeds threshold, shut everything down
        double   circuit_breaker_threshold = 0.1;  // 10% error rate
        uint64_t circuit_breaker_window_size = 1000; // Over last 1000 messages
    };

    explicit FinalGateway(Config cfg);

    void start();
    void stop();

    /// Emergency: block all traffic (manual kill switch)
    void emergency_shutdown();

    /// Resume after emergency
    void resume();

    struct Stats {
        std::atomic<uint64_t> forwarded_to_core{0};
        std::atomic<uint64_t> attestation_incomplete{0};
        std::atomic<uint64_t> attestation_invalid{0};
        std::atomic<uint64_t> ordering_violation{0};
        std::atomic<uint64_t> latency_violation{0};
        std::atomic<uint64_t> unauthorized_source{0};
        std::atomic<uint64_t> circuit_breaker_trips{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    [[nodiscard]] bool is_circuit_open() const noexcept { return circuit_open_; }

private:
    Config config_;
    Stats stats_;
    std::atomic<bool> running_{false};
    std::atomic<bool> circuit_open_{false};
    std::atomic<bool> emergency_{false};
    DilithiumKeypair proxy_identity_;

    // Circuit breaker state
    std::deque<bool> recent_results_;   // true = success, false = failure
    mutable std::mutex circuit_mutex_;

    void check_circuit_breaker(bool success);
    bool verify_full_attestation_chain(const AttestationChain& chain);
};

// ═══════════════════════════════════════════════════════════════════
//  PROXY SHIELD ORCHESTRATOR
//  Manages the full 8-proxy deployment. Handles configuration,
//  health monitoring, and inter-proxy communication setup.
// ═══════════════════════════════════════════════════════════════════

class ProxyShield {
public:
    struct DeploymentConfig {
        // Network topology
        struct ProxyEndpoint {
            std::string host;
            uint16_t    port;
            ProxyLayer  layer;
        };

        std::vector<ProxyEndpoint> edge_sentinels;   // Multiple for geo-distribution
        ProxyEndpoint protocol_gate;
        ProxyEndpoint rate_governor;
        ProxyEndpoint identity_verifier;
        ProxyEndpoint content_inspector;
        ProxyEndpoint anomaly_detector;
        ProxyEndpoint encryption_bridge;
        ProxyEndpoint final_gateway;
        ProxyEndpoint core_node;

        // Monitoring
        std::chrono::seconds health_check_interval{10};
        std::string          metrics_endpoint;   // Prometheus-compatible
        std::string          alert_webhook;      // Slack/PagerDuty webhook
    };

    explicit ProxyShield(DeploymentConfig cfg);
    ~ProxyShield();

    /// Deploy all proxy layers
    void deploy();

    /// Graceful shutdown of all layers (reverse order: P8 → P1)
    void shutdown();

    /// Emergency shutdown (all at once)
    void emergency_shutdown();

    // ─── Health Monitoring ───

    struct ProxyHealth {
        ProxyLayer  layer;
        bool        alive;
        double      cpu_percent;
        uint64_t    memory_bytes;
        uint64_t    messages_per_second;
        uint64_t    error_rate_permille;  // Errors per 1000 messages
        std::chrono::milliseconds avg_latency;
    };

    [[nodiscard]] std::vector<ProxyHealth> health_check() const;

    /// Get aggregated stats across all proxies
    struct AggregateStats {
        uint64_t total_connections;
        uint64_t total_blocked;
        uint64_t total_forwarded;
        uint64_t ddos_absorbed;
        uint64_t malformed_rejected;
        uint64_t rate_limited;
        uint64_t auth_failed;
        uint64_t content_rejected;
        uint64_t anomalies_detected;
        uint64_t attestation_failures;
        double   pipeline_latency_ms;
    };

    [[nodiscard]] AggregateStats aggregate_stats() const;

    /// Print a live dashboard to stdout
    void print_dashboard() const;

    // ─── Dynamic Configuration ───

    /// Hot-reload geo-blocking rules
    void update_geo_rules(
        const std::vector<std::string>& blocked_countries,
        const std::vector<uint32_t>& blocked_asns
    );

    /// Hot-reload rate limits
    void update_rate_limits(const RateGovernor::Config& new_config);

    /// Hot-reload ban list
    void update_ban_list(const std::vector<std::string>& banned_peers);

    /// Force key rotation on encryption bridge
    void rotate_encryption_keys();

private:
    DeploymentConfig config_;
    std::atomic<bool> deployed_{false};

    // Proxy instances (when running in single-process mode for testing)
    std::unique_ptr<EdgeSentinel>     edge_;
    std::unique_ptr<ProtocolGate>     protocol_;
    std::unique_ptr<RateGovernor>     rate_;
    std::unique_ptr<IdentityVerifier> identity_;
    std::unique_ptr<ContentInspector> content_;
    std::unique_ptr<AnomalyDetector>  anomaly_;
    std::unique_ptr<EncryptionBridge> encryption_;
    std::unique_ptr<FinalGateway>     gateway_;

    std::thread health_thread_;

    void health_monitor_loop();
    void send_alert(const std::string& message) const;
};

} // namespace npchain::proxy

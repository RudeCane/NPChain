// ═══════════════════════════════════════════════════════════════════
//  NPChain Proxy Shield — Layer-configurable TCP Proxy
//
//  A single binary that acts as any of the 8 proxy layers based on
//  the PROXY_LAYER environment variable. Each layer applies its
//  specific security filter before forwarding to the next hop.
//
//  Build: g++ -std=c++20 -O2 -pthread -o npchain_proxy proxy.cpp
// ═══════════════════════════════════════════════════════════════════

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <deque>
#include <algorithm>
#include <sstream>
#include <functional>

// ─── Platform ────────────────────────────────────────────────────
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>

using SOCKET = int;
#define INVALID_SOCKET -1
#define CLOSESOCK(s) close(s)

// ─── NPChain Wire Protocol Constants ────────────────────────────
enum MsgType : uint8_t {
    MSG_HELLO      = 0x01,
    MSG_GET_BLOCKS = 0x02,
    MSG_BLOCKS     = 0x03,
    MSG_NEW_BLOCK  = 0x04,
    MSG_PING       = 0x05,
    MSG_PONG       = 0x06
};

static const char* msg_type_name(uint8_t t) {
    switch(t) {
        case 0x01: return "HELLO";
        case 0x02: return "GET_BLOCKS";
        case 0x03: return "BLOCKS";
        case 0x04: return "NEW_BLOCK";
        case 0x05: return "PING";
        case 0x06: return "PONG";
        default:   return "UNKNOWN";
    }
}

// ─── Metrics ─────────────────────────────────────────────────────
struct ProxyMetrics {
    std::atomic<uint64_t> connections_total{0};
    std::atomic<uint64_t> connections_active{0};
    std::atomic<uint64_t> bytes_forwarded{0};
    std::atomic<uint64_t> messages_forwarded{0};
    std::atomic<uint64_t> messages_rejected{0};
    std::atomic<uint64_t> connections_rejected{0};
};

static ProxyMetrics g_metrics;
static std::atomic<bool> g_running{true};

// ─── Rate Limiter (Token Bucket) ─────────────────────────────────
struct TokenBucket {
    double tokens;
    double max_tokens;
    double refill_rate;  // tokens per second
    std::chrono::steady_clock::time_point last_refill;
    std::mutex mtx;

    TokenBucket(double max_t = 100, double rate = 50)
        : tokens(max_t), max_tokens(max_t), refill_rate(rate),
          last_refill(std::chrono::steady_clock::now()) {}

    bool consume(double n = 1.0) {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_refill).count();
        tokens = std::min(max_tokens, tokens + elapsed * refill_rate);
        last_refill = now;
        if (tokens >= n) {
            tokens -= n;
            return true;
        }
        return false;
    }
};

// ─── Per-IP State ────────────────────────────────────────────────
struct IPState {
    TokenBucket rate_bucket;
    uint32_t active_connections = 0;
    uint64_t total_bytes = 0;
    uint64_t messages_seen = 0;
    std::chrono::steady_clock::time_point first_seen;
    std::chrono::steady_clock::time_point last_seen;
    uint32_t violations = 0;
    bool banned = false;

    IPState() : rate_bucket(100, 50),
                first_seen(std::chrono::steady_clock::now()),
                last_seen(std::chrono::steady_clock::now()) {}
};

static std::mutex g_ip_mutex;
static std::map<std::string, IPState> g_ip_states;

static IPState& get_ip_state(const std::string& ip) {
    std::lock_guard<std::mutex> lock(g_ip_mutex);
    return g_ip_states[ip];
}

// ─── Configuration ───────────────────────────────────────────────
struct ProxyConfig {
    int layer = 1;
    std::string listen_host = "0.0.0.0";
    uint16_t listen_port = 19333;
    std::string next_host = "127.0.0.1";
    uint16_t next_port = 19334;
    uint32_t max_conn_per_ip = 5;
    uint32_t max_global_conn = 10000;
    uint32_t max_message_size = 4 * 1024 * 1024;  // 4MB
    double msg_per_sec_per_ip = 100;
    double bytes_per_sec_per_ip = 524288;  // 512KB/s
    double global_msg_per_sec = 50000;
    bool validate_protocol = true;
    bool deep_inspect = false;
    double anomaly_threshold = 0.7;
    std::string proxy_name = "UNKNOWN";
};

static std::string get_env(const char* name, const char* def = "") {
    const char* val = getenv(name);
    return val ? val : def;
}

static ProxyConfig load_config() {
    ProxyConfig cfg;
    cfg.layer = std::stoi(get_env("PROXY_LAYER", "1"));
    cfg.listen_port = static_cast<uint16_t>(std::stoul(get_env("LISTEN_PORT", "19333")));
    cfg.next_host = get_env("NEXT_HOST", "127.0.0.1");
    cfg.next_port = static_cast<uint16_t>(std::stoul(get_env("NEXT_PORT", "19334")));
    cfg.max_conn_per_ip = std::stoul(get_env("MAX_CONN_PER_IP", "5"));
    cfg.max_global_conn = std::stoul(get_env("MAX_GLOBAL_CONN", "10000"));
    cfg.max_message_size = std::stoul(get_env("MAX_MESSAGE_SIZE", "4194304"));
    cfg.msg_per_sec_per_ip = std::stod(get_env("MSG_PER_SEC_PER_IP", "100"));
    cfg.bytes_per_sec_per_ip = std::stod(get_env("BYTES_PER_SEC_PER_IP", "524288"));
    cfg.global_msg_per_sec = std::stod(get_env("GLOBAL_MSG_PER_SEC", "50000"));
    cfg.anomaly_threshold = std::stod(get_env("ANOMALY_THRESHOLD", "0.7"));

    const char* names[] = {
        "EDGE_SENTINEL", "PROTOCOL_GATE", "RATE_GOVERNOR", "IDENTITY_VERIFIER",
        "CONTENT_INSPECTOR", "ANOMALY_DETECTOR", "ENCRYPTION_BRIDGE", "FINAL_GATEWAY"
    };
    if (cfg.layer >= 1 && cfg.layer <= 8) cfg.proxy_name = names[cfg.layer - 1];

    return cfg;
}

// ═══════════════════════════════════════════════════════════════════
//  LAYER-SPECIFIC FILTERS
// ═══════════════════════════════════════════════════════════════════

// Layer 1: Edge Sentinel — connection-level DDoS filtering
static bool filter_edge(const std::string& client_ip, const ProxyConfig& cfg) {
    auto& state = get_ip_state(client_ip);
    if (state.banned) {
        g_metrics.connections_rejected++;
        return false;
    }
    if (state.active_connections >= cfg.max_conn_per_ip) {
        state.violations++;
        if (state.violations > 50) state.banned = true;
        g_metrics.connections_rejected++;
        return false;
    }
    if (g_metrics.connections_active >= cfg.max_global_conn) {
        g_metrics.connections_rejected++;
        return false;
    }
    return true;
}

// Layer 2: Protocol Gate — validate message framing
static bool filter_protocol(const uint8_t* data, size_t len, const ProxyConfig& cfg) {
    if (len < 5) return false;  // Minimum: 4 byte len + 1 byte type
    uint32_t msg_len;
    std::memcpy(&msg_len, data, 4);
    if (msg_len > cfg.max_message_size) return false;
    if (msg_len < 1) return false;

    uint8_t msg_type = data[4];
    if (msg_type < MSG_HELLO || msg_type > MSG_PONG) return false;

    return true;
}

// Layer 3: Rate Governor — per-IP token bucket
static bool filter_rate(const std::string& client_ip, size_t bytes, const ProxyConfig& cfg) {
    auto& state = get_ip_state(client_ip);
    state.rate_bucket.max_tokens = cfg.msg_per_sec_per_ip;
    state.rate_bucket.refill_rate = cfg.msg_per_sec_per_ip;
    if (!state.rate_bucket.consume(1.0)) {
        state.violations++;
        g_metrics.messages_rejected++;
        return false;
    }
    return true;
}

// Layer 4: Identity Verifier — check HELLO messages have valid structure
static bool filter_identity(const uint8_t* data, size_t len) {
    if (len < 5) return true;  // Not a complete message, let it pass to accumulate
    uint8_t msg_type = data[4];
    if (msg_type == MSG_HELLO) {
        uint32_t msg_len;
        std::memcpy(&msg_len, data, 4);
        if (msg_len < 44) return false;  // HELLO must have height(8)+hash(32)+port(2)+alen(2) minimum
    }
    return true;
}

// Layer 5: Content Inspector — validate block/tx structure
static bool filter_content(const uint8_t* data, size_t len) {
    if (len < 5) return true;
    uint32_t msg_len;
    std::memcpy(&msg_len, data, 4);
    uint8_t msg_type = data[4];

    if (msg_type == MSG_NEW_BLOCK || msg_type == MSG_BLOCKS) {
        // Block wire minimum: version(4)+height(8)+prev_hash(32)+merkle(32)+
        //                     ts(8)+diff(4)+vars(4)+clauses(4)+whash(32)+reward(8)+hash(32)+wsize(4)+alen(2)=174
        if (msg_type == MSG_NEW_BLOCK && msg_len < 175) return false;
    }
    return true;
}

// Layer 6: Anomaly Detector — behavioral scoring
struct BehaviorProfile {
    std::deque<std::chrono::steady_clock::time_point> msg_times;
    std::map<uint8_t, uint64_t> msg_type_counts;
    double anomaly_score = 0;

    void record(uint8_t msg_type) {
        auto now = std::chrono::steady_clock::now();
        msg_times.push_back(now);
        msg_type_counts[msg_type]++;

        // Prune old entries (keep last 60 seconds)
        while (!msg_times.empty() &&
               std::chrono::duration<double>(now - msg_times.front()).count() > 60.0) {
            msg_times.pop_front();
        }

        // Anomaly scoring: too many messages, weird type distribution
        double msg_rate = msg_times.size() / 60.0;
        if (msg_rate > 200) anomaly_score += 0.1;

        // Check for unusual patterns (e.g., only GET_BLOCKS = chain scraping)
        uint64_t total = 0;
        for (auto& [t, c] : msg_type_counts) total += c;
        if (total > 100) {
            double get_blocks_ratio = static_cast<double>(msg_type_counts[MSG_GET_BLOCKS]) / total;
            if (get_blocks_ratio > 0.9) anomaly_score += 0.2;
        }

        // Decay score over time
        anomaly_score *= 0.999;
    }
};

static std::mutex g_behavior_mutex;
static std::map<std::string, BehaviorProfile> g_behaviors;

static bool filter_anomaly(const std::string& client_ip, const uint8_t* data, size_t len,
                           const ProxyConfig& cfg) {
    if (len < 5) return true;
    uint8_t msg_type = data[4];

    std::lock_guard<std::mutex> lock(g_behavior_mutex);
    auto& profile = g_behaviors[client_ip];
    profile.record(msg_type);

    if (profile.anomaly_score > cfg.anomaly_threshold) {
        g_metrics.messages_rejected++;
        return false;
    }
    return true;
}

// Layer 7: Encryption Bridge — in production would re-encrypt with internal keys
// For testnet, acts as metadata stripper (validates + forwards clean)
static bool filter_encrypt(const uint8_t* data, size_t len) {
    // Verify message integrity
    if (len < 5) return false;
    uint32_t msg_len;
    std::memcpy(&msg_len, data, 4);
    if (5 + msg_len - 1 > len) return false;  // Incomplete message
    return true;
}

// Layer 8: Final Gateway — circuit breaker
struct CircuitBreaker {
    std::deque<bool> results;  // true=success, false=failure
    uint32_t window_size = 1000;
    double threshold = 0.1;  // Trip if >10% failures
    bool tripped = false;
    std::chrono::steady_clock::time_point trip_time;
    std::mutex mtx;

    void record(bool success) {
        std::lock_guard<std::mutex> lock(mtx);
        results.push_back(success);
        while (results.size() > window_size) results.pop_front();

        if (results.size() >= 100) {
            uint32_t failures = std::count(results.begin(), results.end(), false);
            double fail_rate = static_cast<double>(failures) / results.size();
            if (fail_rate > threshold) {
                tripped = true;
                trip_time = std::chrono::steady_clock::now();
            }
        }

        // Auto-reset after 30 seconds
        if (tripped) {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - trip_time).count();
            if (elapsed > 30.0) {
                tripped = false;
                results.clear();
            }
        }
    }

    bool is_open() {
        std::lock_guard<std::mutex> lock(mtx);
        return tripped;
    }
};

static CircuitBreaker g_circuit_breaker;

static bool filter_gateway() {
    return !g_circuit_breaker.is_open();
}

// ═══════════════════════════════════════════════════════════════════
//  TCP FORWARDING ENGINE
// ═══════════════════════════════════════════════════════════════════

static SOCKET connect_to_next(const ProxyConfig& cfg) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.next_port);
    inet_pton(AF_INET, cfg.next_host.c_str(), &addr.sin_addr);

    // Set connect timeout
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        CLOSESOCK(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

// Apply the filter for this proxy layer
static bool apply_filter(int layer, const std::string& client_ip,
                         const uint8_t* data, size_t len, const ProxyConfig& cfg) {
    switch (layer) {
        case 1: return true;  // Edge does connection-level filtering (already done)
        case 2: return filter_protocol(data, len, cfg);
        case 3: return filter_rate(client_ip, len, cfg);
        case 4: return filter_identity(data, len);
        case 5: return filter_content(data, len);
        case 6: return filter_anomaly(client_ip, data, len, cfg);
        case 7: return filter_encrypt(data, len);
        case 8: return filter_gateway();
        default: return true;
    }
}

// Bidirectional pipe between client and next hop
static void handle_connection(SOCKET client_sock, const std::string& client_ip,
                              const ProxyConfig& cfg) {
    g_metrics.connections_total++;
    g_metrics.connections_active++;

    auto& ip_state = get_ip_state(client_ip);
    ip_state.active_connections++;
    ip_state.last_seen = std::chrono::steady_clock::now();

    // Connect to next hop
    SOCKET next_sock = connect_to_next(cfg);
    if (next_sock == INVALID_SOCKET) {
        std::cerr << "[P" << cfg.layer << "] Cannot connect to next hop "
                  << cfg.next_host << ":" << cfg.next_port << "\n";
        g_circuit_breaker.record(false);
        CLOSESOCK(client_sock);
        ip_state.active_connections--;
        g_metrics.connections_active--;
        return;
    }
    g_circuit_breaker.record(true);

    std::atomic<bool> alive{true};
    uint8_t buf[65536];

    // Client → Next (with filtering)
    auto forward_thread = std::thread([&]() {
        while (alive && g_running) {
            ssize_t n = recv(client_sock, buf, sizeof(buf), 0);
            if (n <= 0) { alive = false; break; }

            if (!apply_filter(cfg.layer, client_ip, buf, static_cast<size_t>(n), cfg)) {
                g_metrics.messages_rejected++;
                ip_state.violations++;
                if (ip_state.violations > 100) {
                    std::cout << "[P" << cfg.layer << "] BANNED " << client_ip
                              << " (too many violations)\n";
                    ip_state.banned = true;
                    alive = false;
                    break;
                }
                continue;  // Drop the message but keep connection
            }

            ssize_t sent = 0;
            while (sent < n) {
                ssize_t s = send(next_sock, buf + sent, n - sent, 0);
                if (s <= 0) { alive = false; break; }
                sent += s;
            }
            g_metrics.bytes_forwarded += n;
            g_metrics.messages_forwarded++;
            ip_state.messages_seen++;
        }
    });

    // Next → Client (pass-through)
    while (alive && g_running) {
        ssize_t n = recv(next_sock, buf, sizeof(buf), 0);
        if (n <= 0) { alive = false; break; }

        ssize_t sent = 0;
        while (sent < n) {
            ssize_t s = send(client_sock, buf + sent, n - sent, 0);
            if (s <= 0) { alive = false; break; }
            sent += s;
        }
        g_metrics.bytes_forwarded += n;
    }

    alive = false;
    CLOSESOCK(client_sock);
    CLOSESOCK(next_sock);
    if (forward_thread.joinable()) forward_thread.join();

    ip_state.active_connections--;
    g_metrics.connections_active--;
}

// ═══════════════════════════════════════════════════════════════════
//  METRICS HTTP SERVER (lightweight, runs on listen_port + 1000)
// ═══════════════════════════════════════════════════════════════════

static void metrics_server(uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) return;
    listen(sock, 5);

    while (g_running) {
        struct pollfd pfd{sock, POLLIN, 0};
        if (poll(&pfd, 1, 1000) <= 0) continue;

        SOCKET client = accept(sock, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        char req[4096];
        recv(client, req, sizeof(req), 0);

        std::ostringstream body;
        body << "{\"layer\":" << get_env("PROXY_LAYER", "0")
             << ",\"connections_total\":" << g_metrics.connections_total.load()
             << ",\"connections_active\":" << g_metrics.connections_active.load()
             << ",\"bytes_forwarded\":" << g_metrics.bytes_forwarded.load()
             << ",\"messages_forwarded\":" << g_metrics.messages_forwarded.load()
             << ",\"messages_rejected\":" << g_metrics.messages_rejected.load()
             << ",\"connections_rejected\":" << g_metrics.connections_rejected.load()
             << "}";

        std::string json = body.str();
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Content-Length: " << json.size() << "\r\n\r\n"
             << json;
        std::string r = resp.str();
        send(client, r.c_str(), r.size(), 0);
        CLOSESOCK(client);
    }
    CLOSESOCK(sock);
}

// ═══════════════════════════════════════════════════════════════════
//  MAIN — Listen, filter, forward
// ═══════════════════════════════════════════════════════════════════

int main() {
    ProxyConfig cfg = load_config();

    std::cout << "╔═══════════════════════════════════════════════════╗\n";
    std::cout << "║  NPChain Proxy Shield — Layer " << cfg.layer << "                  ║\n";
    std::cout << "║  " << cfg.proxy_name;
    for (size_t i = cfg.proxy_name.size(); i < 47; ++i) std::cout << ' ';
    std::cout << "║\n";
    std::cout << "╚═══════════════════════════════════════════════════╝\n\n";
    std::cout << "  Listen:    " << cfg.listen_host << ":" << cfg.listen_port << "\n";
    std::cout << "  Next hop:  " << cfg.next_host << ":" << cfg.next_port << "\n";
    std::cout << "  Metrics:   http://0.0.0.0:" << (cfg.listen_port + 1000) << "/metrics\n\n";

    // Start metrics server
    std::thread metrics_thread(metrics_server, cfg.listen_port + 1000);
    metrics_thread.detach();

    // Main listener
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        std::cerr << "FATAL: Cannot create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(cfg.listen_port);

    if (bind(listen_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "FATAL: Cannot bind port " << cfg.listen_port << "\n";
        return 1;
    }

    listen(listen_sock, 128);
    std::cout << "[P" << cfg.layer << "] Listening on port " << cfg.listen_port << "...\n";

    // Status printer
    std::thread status_thread([&]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            std::cout << "[P" << cfg.layer << "] Status: "
                      << "active=" << g_metrics.connections_active.load()
                      << " total=" << g_metrics.connections_total.load()
                      << " fwd=" << g_metrics.messages_forwarded.load()
                      << " rej=" << g_metrics.messages_rejected.load()
                      << " bytes=" << (g_metrics.bytes_forwarded.load() / 1024) << "KB\n";
        }
    });
    status_thread.detach();

    while (g_running) {
        struct pollfd pfd{listen_sock, POLLIN, 0};
        if (poll(&pfd, 1, 1000) <= 0) continue;

        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        SOCKET client = accept(listen_sock, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client == INVALID_SOCKET) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        std::string client_ip(ip);

        // Layer 1 connection-level filter
        if (cfg.layer == 1 && !filter_edge(client_ip, cfg)) {
            CLOSESOCK(client);
            continue;
        }

        std::cout << "[P" << cfg.layer << "] Connection from " << client_ip << "\n";

        std::thread(handle_connection, client, client_ip, std::cref(cfg)).detach();
    }

    CLOSESOCK(listen_sock);
    return 0;
}

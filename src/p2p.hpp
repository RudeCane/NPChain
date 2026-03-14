// ═══════════════════════════════════════════════════════════════════
//  NPChain P2P Network Module
//
//  Simple TCP peer-to-peer networking for the testnet.
//  - Peer discovery via seed nodes
//  - Block propagation (mine → broadcast to all peers)
//  - Chain sync (connect → download missing blocks)
//  - Length-prefixed binary protocol
//
//  This file is #included by testnet_node.cpp — not compiled separately.
// ═══════════════════════════════════════════════════════════════════

#pragma once

#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <deque>
#include <functional>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <memory>

using namespace npchain;
using namespace npchain::crypto;

#ifdef _WIN32
#ifndef _WINSOCK2_INCLUDED
#define _WINSOCK2_INCLUDED
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSESOCK closesocket
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#define CLOSESOCK close
typedef int SOCKET;
constexpr SOCKET INVALID_SOCKET = -1;
#endif

// ─── Wire Protocol ──────────────────────────────────────────────
// Message format: [4 bytes length][1 byte type][payload]

enum MsgType : uint8_t {
    MSG_HELLO       = 0x01,  // {height:8, hash:32, port:2, addr_len:2, addr:N}
    MSG_GET_BLOCKS  = 0x02,  // {from_height:8}
    MSG_BLOCKS      = 0x03,  // {count:4, [block_wire...]}
    MSG_NEW_BLOCK   = 0x04,  // {block_wire}
    MSG_PING        = 0x05,  // empty
    MSG_PONG        = 0x06,  // empty
};

// ─── Block Wire Format ──────────────────────────────────────────
// Serialize a block for network transmission

static Bytes block_to_wire(const Block& b) {
    Bytes w;
    auto push = [&](const void* p, size_t n) {
        const uint8_t* bp = static_cast<const uint8_t*>(p);
        w.insert(w.end(), bp, bp + n);
    };
    push(&b.version, 4);
    push(&b.height, 8);
    push(b.prev_hash.data(), 32);
    push(b.merkle_root.data(), 32);
    push(&b.timestamp, 8);
    push(&b.difficulty, 4);
    push(&b.num_variables, 4);
    push(&b.num_clauses, 4);
    push(b.witness_hash.data(), 32);
    push(&b.reward, 8);
    push(b.hash.data(), 32);

    // Witness bits (packed into bytes)
    uint32_t wsize = b.num_variables;
    push(&wsize, 4);
    Bytes wbits((wsize + 7) / 8, 0);
    for (uint32_t i = 0; i < wsize; ++i)
        if (i < b.witness.size() && b.witness[i])
            wbits[i / 8] |= (1 << (i % 8));
    w.insert(w.end(), wbits.begin(), wbits.end());

    // Miner address
    uint16_t alen = static_cast<uint16_t>(b.miner_address.size());
    push(&alen, 2);
    w.insert(w.end(), b.miner_address.begin(), b.miner_address.end());

    return w;
}

static bool wire_to_block(const uint8_t* data, size_t len, Block& b) {
    // Minimum size check: fixed fields = 4+8+32+32+8+4+4+4+32+8+32+4+2 = 174
    if (len < 174) return false;

    size_t pos = 0;
    auto read = [&](void* dst, size_t n) {
        if (pos + n > len) return false;
        std::memcpy(dst, data + pos, n);
        pos += n;
        return true;
    };

    if (!read(&b.version, 4)) return false;
    if (!read(&b.height, 8)) return false;
    if (!read(b.prev_hash.data(), 32)) return false;
    if (!read(b.merkle_root.data(), 32)) return false;
    if (!read(&b.timestamp, 8)) return false;
    if (!read(&b.difficulty, 4)) return false;
    if (!read(&b.num_variables, 4)) return false;
    if (!read(&b.num_clauses, 4)) return false;
    if (!read(b.witness_hash.data(), 32)) return false;
    if (!read(&b.reward, 8)) return false;
    if (!read(b.hash.data(), 32)) return false;

    // Witness
    uint32_t wsize;
    if (!read(&wsize, 4)) return false;
    if (wsize > 10000) return false;  // Sanity check
    uint32_t wbytes = (wsize + 7) / 8;
    if (pos + wbytes > len) return false;
    b.witness.resize(wsize);
    for (uint32_t i = 0; i < wsize; ++i)
        b.witness[i] = (data[pos + i / 8] >> (i % 8)) & 1;
    pos += wbytes;

    // Miner address
    uint16_t alen;
    if (!read(&alen, 2)) return false;
    if (alen > 200 || pos + alen > len) return false;
    b.miner_address = std::string(reinterpret_cast<const char*>(data + pos), alen);
    pos += alen;

    return true;
}

// ─── Message Builder ────────────────────────────────────────────

static Bytes build_message(MsgType type, const Bytes& payload = {}) {
    uint32_t len = static_cast<uint32_t>(1 + payload.size());
    Bytes msg(4 + 1 + payload.size());
    std::memcpy(msg.data(), &len, 4);
    msg[4] = static_cast<uint8_t>(type);
    if (!payload.empty())
        std::memcpy(msg.data() + 5, payload.data(), payload.size());
    return msg;
}

// ─── Peer Connection ────────────────────────────────────────────

struct Peer {
    SOCKET sock = INVALID_SOCKET;
    std::string address;  // "ip:port"
    uint64_t chain_height = 0;
    Hash256 best_hash{};
    std::thread recv_thread;
    std::atomic<bool> alive{true};
    uint16_t listen_port = 0;
    std::string miner_addr;

    void disconnect() {
        alive = false;
        if (sock != INVALID_SOCKET) {
            CLOSESOCK(sock);
            sock = INVALID_SOCKET;
        }
    }
};

// ─── P2P Manager ────────────────────────────────────────────────

class P2PManager {
public:
    using BlockCallback = std::function<bool(const Block&)>;
    using GetChainCallback = std::function<std::vector<Block>(uint64_t from)>;

    P2PManager(uint16_t listen_port, const std::string& miner_addr)
        : listen_port_(listen_port), miner_addr_(miner_addr) {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    }

    ~P2PManager() { stop(); }

    void set_callbacks(BlockCallback on_block, GetChainCallback get_chain) {
        on_new_block_ = std::move(on_block);
        get_chain_ = std::move(get_chain);
    }

    void set_chain_info(uint64_t height, const Hash256& tip) {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        local_height_ = height;
        local_tip_ = tip;
    }

    void start() {
        running_ = true;
        listener_thread_ = std::thread(&P2PManager::listen_loop, this);
    }

    void stop() {
        running_ = false;
        if (listener_sock_ != INVALID_SOCKET) {
            CLOSESOCK(listener_sock_);
            listener_sock_ = INVALID_SOCKET;
        }
        if (listener_thread_.joinable()) listener_thread_.join();

        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (auto& p : peers_) {
            p->disconnect();
            if (p->recv_thread.joinable()) p->recv_thread.join();
        }
        peers_.clear();
    }

    // Connect to a seed node
    bool connect_to(const std::string& host, uint16_t port) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        // Resolve hostname
        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
            CLOSESOCK(sock);
            return false;
        }

        if (connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) < 0) {
            freeaddrinfo(result);
            CLOSESOCK(sock);
            return false;
        }
        freeaddrinfo(result);

        auto peer = std::make_shared<Peer>();
        peer->sock = sock;
        peer->address = host + ":" + std::to_string(port);
        peer->miner_addr = miner_addr_;

        // Send HELLO
        send_hello(peer);

        // Start receive thread
        peer->recv_thread = std::thread(&P2PManager::peer_recv_loop, this, peer);

        std::lock_guard<std::mutex> lock(peers_mutex_);
        peers_.push_back(peer);
        std::cout << "[P2P] Connected to " << peer->address << "\n";
        return true;
    }

    // Broadcast a new block to all peers
    void broadcast_block(const Block& block) {
        Bytes wire = block_to_wire(block);
        Bytes msg = build_message(MSG_NEW_BLOCK, wire);

        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (auto& p : peers_) {
            if (p->alive) {
                send(p->sock, reinterpret_cast<const char*>(msg.data()),
                     static_cast<int>(msg.size()), 0);
            }
        }
    }

    size_t peer_count() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        size_t count = 0;
        for (const auto& p : peers_)
            if (p->alive) ++count;
        return count;
    }

    std::vector<std::string> peer_list() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        std::vector<std::string> list;
        for (const auto& p : peers_)
            if (p->alive) list.push_back(p->address);
        return list;
    }

private:
    uint16_t listen_port_;
    std::string miner_addr_;
    SOCKET listener_sock_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread listener_thread_;
    mutable std::mutex peers_mutex_;
    std::vector<std::shared_ptr<Peer>> peers_;
    BlockCallback on_new_block_;
    GetChainCallback get_chain_;
    uint64_t local_height_ = 0;
    Hash256 local_tip_{};

    void send_hello(std::shared_ptr<Peer> peer) {
        Bytes payload;
        auto push = [&](const void* p, size_t n) {
            const uint8_t* bp = static_cast<const uint8_t*>(p);
            payload.insert(payload.end(), bp, bp + n);
        };
        push(&local_height_, 8);
        push(local_tip_.data(), 32);
        push(&listen_port_, 2);
        uint16_t alen = static_cast<uint16_t>(miner_addr_.size());
        push(&alen, 2);
        payload.insert(payload.end(), miner_addr_.begin(), miner_addr_.end());

        Bytes msg = build_message(MSG_HELLO, payload);
        send(peer->sock, reinterpret_cast<const char*>(msg.data()),
             static_cast<int>(msg.size()), 0);
    }

    void request_blocks(std::shared_ptr<Peer> peer, uint64_t from_height) {
        Bytes payload(8);
        std::memcpy(payload.data(), &from_height, 8);
        Bytes msg = build_message(MSG_GET_BLOCKS, payload);
        send(peer->sock, reinterpret_cast<const char*>(msg.data()),
             static_cast<int>(msg.size()), 0);
    }

    void send_blocks(std::shared_ptr<Peer> peer, uint64_t from_height) {
        if (!get_chain_) return;
        auto blocks = get_chain_(from_height);
        if (blocks.empty()) return;

        Bytes payload;
        uint32_t count = static_cast<uint32_t>(blocks.size());
        payload.resize(4);
        std::memcpy(payload.data(), &count, 4);

        for (const auto& b : blocks) {
            Bytes wire = block_to_wire(b);
            uint32_t blen = static_cast<uint32_t>(wire.size());
            const uint8_t* bp = reinterpret_cast<const uint8_t*>(&blen);
            payload.insert(payload.end(), bp, bp + 4);
            payload.insert(payload.end(), wire.begin(), wire.end());
        }

        Bytes msg = build_message(MSG_BLOCKS, payload);
        send(peer->sock, reinterpret_cast<const char*>(msg.data()),
             static_cast<int>(msg.size()), 0);
    }

    bool recv_exactly(SOCKET sock, uint8_t* buf, size_t len) {
        size_t total = 0;
        while (total < len) {
            int n = recv(sock, reinterpret_cast<char*>(buf + total),
                        static_cast<int>(len - total), 0);
            if (n <= 0) return false;
            total += static_cast<size_t>(n);
        }
        return true;
    }

    void handle_message(std::shared_ptr<Peer> peer, MsgType type, const uint8_t* data, size_t len) {
        switch (type) {
        case MSG_HELLO: {
            if (len < 44) break;
            std::memcpy(&peer->chain_height, data, 8);
            std::memcpy(peer->best_hash.data(), data + 8, 32);
            uint16_t port;
            std::memcpy(&port, data + 40, 2);
            peer->listen_port = port;
            if (len >= 44) {
                uint16_t alen;
                std::memcpy(&alen, data + 42, 2);
                if (44 + alen <= len)
                    peer->miner_addr = std::string(reinterpret_cast<const char*>(data + 44), alen);
            }
            std::cout << "[P2P] Hello from " << peer->address
                      << " (height=" << peer->chain_height
                      << ", miner=" << peer->miner_addr.substr(0, 20) << "...)\n";

            // If they have a longer chain, request blocks
            if (peer->chain_height > local_height_ || (local_height_ == 0 && peer->chain_height >= 0)) {
                uint64_t from = (local_height_ == 0 && local_tip_ == Hash256{}) ? 0 : local_height_ + 1;
                std::cout << "[P2P] Peer has longer chain (" << peer->chain_height
                          << " vs " << local_height_ << "), syncing from #" << from << "...\n";
                request_blocks(peer, from);
            }
            // Send our hello back
            send_hello(peer);
            break;
        }

        case MSG_GET_BLOCKS: {
            if (len < 8) break;
            uint64_t from;
            std::memcpy(&from, data, 8);
            std::cout << "[P2P] " << peer->address << " requesting blocks from #" << from << "\n";
            send_blocks(peer, from);
            break;
        }

        case MSG_BLOCKS: {
            if (len < 4) break;
            uint32_t count;
            std::memcpy(&count, data, 4);
            if (count > 1000) break;  // Sanity

            size_t pos = 4;
            uint32_t accepted = 0;
            for (uint32_t i = 0; i < count && pos < len; ++i) {
                if (pos + 4 > len) break;
                uint32_t blen;
                std::memcpy(&blen, data + pos, 4);
                pos += 4;
                if (pos + blen > len || blen > 100000) break;

                Block b;
                if (wire_to_block(data + pos, blen, b)) {
                    if (on_new_block_ && on_new_block_(b)) ++accepted;
                }
                pos += blen;
            }
            std::cout << "[P2P] Received " << count << " blocks from "
                      << peer->address << " (accepted " << accepted << ")\n";
            break;
        }

        case MSG_NEW_BLOCK: {
            Block b;
            if (wire_to_block(data, len, b)) {
                std::cout << "[P2P] New block #" << b.height << " from "
                          << peer->address << " (miner=" << b.miner_address.substr(0, 20) << "...)\n";
                if (on_new_block_) on_new_block_(b);
            }
            break;
        }

        case MSG_PING: {
            Bytes pong = build_message(MSG_PONG);
            send(peer->sock, reinterpret_cast<const char*>(pong.data()),
                 static_cast<int>(pong.size()), 0);
            break;
        }

        case MSG_PONG:
            break;

        default:
            break;
        }
    }

    void peer_recv_loop(std::shared_ptr<Peer> peer) {
        while (peer->alive && running_) {
            // Read message header: 4 bytes length
            uint8_t hdr[4];
            if (!recv_exactly(peer->sock, hdr, 4)) break;

            uint32_t msg_len;
            std::memcpy(&msg_len, hdr, 4);
            if (msg_len == 0 || msg_len > 10'000'000) break;  // Max 10MB message

            // Read message body
            Bytes body(msg_len);
            if (!recv_exactly(peer->sock, body.data(), msg_len)) break;

            MsgType type = static_cast<MsgType>(body[0]);
            handle_message(peer, type, body.data() + 1, msg_len - 1);
        }

        peer->alive = false;
        std::cout << "[P2P] Peer " << peer->address << " disconnected\n";
    }

    void listen_loop() {
        listener_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_sock_ == INVALID_SOCKET) {
            std::cerr << "[P2P] Failed to create listener socket\n";
            return;
        }

        int opt = 1;
        setsockopt(listener_sock_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(listen_port_);

        if (bind(listener_sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[P2P] Failed to bind port " << listen_port_ << "\n";
            CLOSESOCK(listener_sock_);
            listener_sock_ = INVALID_SOCKET;
            return;
        }

        listen(listener_sock_, 20);
        std::cout << "[P2P] Listening on port " << listen_port_ << "\n";

        while (running_) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(listener_sock_, &fds);
            struct timeval tv{1, 0};

            int sel = select(static_cast<int>(listener_sock_) + 1, &fds, nullptr, nullptr, &tv);
            if (sel <= 0) continue;

            struct sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            SOCKET client = accept(listener_sock_,
                reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
            if (client == INVALID_SOCKET) continue;

            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
            uint16_t client_port = ntohs(client_addr.sin_port);

            auto peer = std::make_shared<Peer>();
            peer->sock = client;
            peer->address = std::string(ip) + ":" + std::to_string(client_port);

            // Send HELLO
            send_hello(peer);

            // Start receive thread
            peer->recv_thread = std::thread(&P2PManager::peer_recv_loop, this, peer);

            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                peers_.push_back(peer);
            }
            std::cout << "[P2P] Incoming connection from " << peer->address << "\n";
        }

        CLOSESOCK(listener_sock_);
        listener_sock_ = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

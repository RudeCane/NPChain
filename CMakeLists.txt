cmake_minimum_required(VERSION 3.20)
project(NPChain VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Security-hardened compiler flags ───
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    -fPIE
    -Wformat -Wformat-security
    -fno-strict-aliasing
    -Wshadow -Wconversion
)

# Sanitizers for debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ─── Thread support ───
find_package(Threads REQUIRED)

# ─── Core library ───
add_library(npchain_core STATIC
    src/core/block.cpp
    src/core/transaction.cpp
    src/core/chain.cpp
    src/core/mempool.cpp
)
target_include_directories(npchain_core PUBLIC include)
target_link_libraries(npchain_core PUBLIC Threads::Threads)

# ─── Crypto library ───
add_library(npchain_crypto STATIC
    src/crypto/hash.cpp
    src/crypto/dilithium.cpp
    src/crypto/kyber.cpp
    src/crypto/commitment.cpp
)
target_include_directories(npchain_crypto PUBLIC include)

# ─── Consensus engine ───
add_library(npchain_consensus STATIC
    src/consensus/ponw_engine.cpp
    src/consensus/instance_generator.cpp
    src/consensus/difficulty.cpp
    src/consensus/vdf.cpp
)
target_include_directories(npchain_consensus PUBLIC include)
target_link_libraries(npchain_consensus PUBLIC npchain_core npchain_crypto)

# ─── Mining solvers ───
add_library(npchain_mining STATIC
    src/mining/sat_solver.cpp
    src/mining/subset_solver.cpp
    src/mining/graph_color_solver.cpp
    src/mining/hamiltonian_solver.cpp
    src/mining/solver_pool.cpp
)
target_include_directories(npchain_mining PUBLIC include)
target_link_libraries(npchain_mining PUBLIC npchain_core npchain_consensus Threads::Threads)

# ─── Privacy module ───
add_library(npchain_privacy STATIC
    src/privacy/stealth_address.cpp
    src/privacy/confidential_tx.cpp
    src/privacy/zk_proof.cpp
)
target_include_directories(npchain_privacy PUBLIC include)
target_link_libraries(npchain_privacy PUBLIC npchain_crypto)

# ─── Network layer ───
add_library(npchain_network STATIC
    src/network/peer.cpp
    src/network/gossip.cpp
    src/network/transport.cpp
)
target_include_directories(npchain_network PUBLIC include)
target_link_libraries(npchain_network PUBLIC npchain_crypto Threads::Threads)

# ─── Security module ───
add_library(npchain_security STATIC
    src/security/threshold_sig.cpp
    src/security/anti_flood.cpp
    src/security/peer_scoring.cpp
)
target_include_directories(npchain_security PUBLIC include)
target_link_libraries(npchain_security PUBLIC npchain_crypto)

# ─── Main executables ───
add_executable(npchain_node src/main_node.cpp)
target_link_libraries(npchain_node PRIVATE
    npchain_core npchain_consensus npchain_mining
    npchain_crypto npchain_privacy npchain_network npchain_security
)

add_executable(npchain_miner src/main_miner.cpp)
target_link_libraries(npchain_miner PRIVATE
    npchain_core npchain_consensus npchain_mining
    npchain_crypto npchain_network
)

add_executable(npchain_wallet src/main_wallet.cpp)
target_link_libraries(npchain_wallet PRIVATE
    npchain_core npchain_crypto npchain_privacy
)

# ─── Genesis block generator ───
add_executable(npchain_genesis src/genesis_generator.cpp)
target_include_directories(npchain_genesis PRIVATE include)
target_link_libraries(npchain_genesis PRIVATE
    npchain_core npchain_consensus npchain_mining npchain_crypto npchain_privacy
)

# ─── Testnet compile flag ───
option(NPCHAIN_TESTNET "Build for testnet configuration" OFF)
if(NPCHAIN_TESTNET)
    add_compile_definitions(NPCHAIN_TESTNET=1)
    message(STATUS "Building for TESTNET (15s blocks, easy difficulty)")
endif()

# ─── Tests ───
enable_testing()
add_executable(test_consensus tests/test_consensus.cpp)
target_link_libraries(test_consensus PRIVATE npchain_consensus npchain_mining)
add_test(NAME ConsensusTests COMMAND test_consensus)

add_executable(test_crypto tests/test_crypto.cpp)
target_link_libraries(test_crypto PRIVATE npchain_crypto)
add_test(NAME CryptoTests COMMAND test_crypto)

add_executable(test_mining tests/test_mining.cpp)
target_link_libraries(test_mining PRIVATE npchain_mining npchain_consensus)
add_test(NAME MiningTests COMMAND test_mining)

add_executable(test_emission tests/test_emission.cpp)
target_include_directories(test_emission PRIVATE include)
add_test(NAME EmissionTests COMMAND test_emission)

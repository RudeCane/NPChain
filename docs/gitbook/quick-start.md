# Quick Start Guide

Get mining on the NPChain testnet in 5 minutes.

## Requirements

- Windows PC with [MSYS2](https://www.msys2.org/) installed
- Internet connection

## Step 1: Install Build Tools

Open **MSYS2 UCRT64** from your Start menu, then run:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc git mingw-w64-ucrt-x86_64-python
```

## Step 2: Download NPChain

```bash
git clone https://github.com/RudeCane/NPChain.git
cd NPChain
```

## Step 3: Build the Node

```bash
g++ -std=c++20 -O2 -pthread -I include -I src \
    src/testnet_node.cpp src/crypto/hash.cpp src/crypto/dilithium.cpp \
    -o npchain_testnet.exe -lws2_32
```

Ignore any warnings about `memcpy` — these are harmless.

## Step 4: Create Your Wallet

Start the wallet web server in one terminal:

```bash
cd web
python3 -m http.server 8888
```

Open **http://localhost:8888** in your browser.

1. Create a password (8+ characters)
2. Copy your `cert1...` address
3. Copy the mining command shown on screen

## Step 5: Start Mining

Open a **second MSYS2 terminal** and paste your mining command:

```bash
cd NPChain
./npchain_testnet.exe --address cert1YOUR_ADDRESS_HERE
```

To join the public testnet:

```bash
./npchain_testnet.exe --address cert1YOUR_ADDRESS_HERE --seed 47.197.198.200:19333
```

## Step 6: Watch Your Balance

Go back to **http://localhost:8888**. Your dashboard shows:

- Balance increasing with each block you mine
- Chain status (height, difficulty, total supply)
- Recent blocks — highlights yours with "You mined!"
- Green dot = node connected

Each block earns **~47,564 Certs**.

## What's Next?

- [Submit a governance proposal](governance.md)
- [Check your mainnet migration status](migration.md)
- [Understand the mining hardware](hardware.md)
- [Explore the RPC API](rpc-api.md)

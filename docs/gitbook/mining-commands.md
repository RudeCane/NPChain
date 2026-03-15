# Mining Commands

## Start Mining

```bash
./npchain_testnet.exe --address cert1YOUR_ADDRESS
```

## Join Public Testnet

```bash
./npchain_testnet.exe --address cert1YOUR_ADDRESS --seed 47.197.198.200:19333
```

## Full Options

```
--address <cert1...>     Your wallet address (required)
--seed <host:port>       Seed node to connect to
--p2p-port <port>        P2P listen port (default: 19333)
--rpc-port <port>        RPC/HTTP port (default: 18333)
--block-time <seconds>   Target block time (default: 15)
--admin-password <pw>    Admin password for migration endpoints
--data-dir <path>        Directory for chain data persistence
--blocks <N>             Stop after N blocks (0 = unlimited)
--fast                   No delays (testing only)
```

## Check Your Balance

```bash
curl http://localhost:18333/api/v1/balance/cert1YOUR_ADDRESS
```

## Run Your Own Seed Node

```bash
./npchain_testnet.exe --address cert1YOUR_ADDRESS --p2p-port 19333
```

Other miners connect to you with `--seed YOUR_IP:19333`.

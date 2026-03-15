# Address Format

## cert1... Addresses

NPChain uses a custom address format starting with `cert1`:

```
cert1 + SHA3-256(public_key)[0:40]
```

Example: `cert1682094378797e70b8046bf03b6a902f7d4f1b568`

The address is derived from the SHA3-256 hash of the public key, truncated to 40 hex characters (160 bits), prefixed with `cert1`.

## Address Properties

- **Deterministic** — same public key always produces same address
- **Non-reversible** — cannot derive public key from address
- **Post-quantum safe** — SHA3-256 derivation
- **45 characters total** — 5 prefix + 40 hex

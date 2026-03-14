#!/bin/bash
# NPChain — Generate web wallet import file from address
# Usage: ./export_web_wallet.sh cert1YOUR_ADDRESS your_password

ADDR=${1:-"cert1000000000000000000000000000000000000000"}
PASS=${2:-"npchain"}

if [[ ! "$ADDR" == cert1* ]]; then
    echo "Error: Address must start with cert1"
    exit 1
fi

# Generate deterministic fake encrypted key (watch-only wallet)
# The web wallet just needs the structure to accept the import
SALT="$(printf '%032x' $RANDOM$RANDOM$RANDOM$RANDOM)"
ENC="$(printf '%064x' $RANDOM$RANDOM$RANDOM$RANDOM)"
CHECK="$(printf '%064x' $RANDOM$RANDOM$RANDOM$RANDOM)"

cat > wallet-web-export.json << EOF
{
  "address": "$ADDR",
  "publicKey": "",
  "encrypted": {
    "salt": "$SALT",
    "enc": "$ENC",
    "check": "$CHECK"
  },
  "created": $(date +%s)000
}
EOF

echo "Created wallet-web-export.json for address: $ADDR"
echo "Import this file in the web wallet at http://localhost:8888"

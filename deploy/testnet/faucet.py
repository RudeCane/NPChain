#!/usr/bin/env python3
"""
NPChain Testnet Faucet
Dispenses free test Certs to any valid address.
"""

import os
import time
import json
import requests
from flask import Flask, request, jsonify, render_template_string

app = Flask(__name__)

RPC_URL      = os.environ.get("RPC_URL", "http://172.28.0.30:18332")
RPC_KEY      = os.environ.get("RPC_KEY", "testnet-rpc-public-key")
DRIP_AMOUNT  = int(os.environ.get("DRIP_AMOUNT", "10000"))
COOLDOWN     = int(os.environ.get("COOLDOWN_SEC", "3600"))
LISTEN_PORT  = int(os.environ.get("LISTEN_PORT", "8080"))

# In-memory cooldown tracker (resets on restart — fine for testnet)
last_drip = {}  # address → timestamp

FAUCET_HTML = """
<!DOCTYPE html>
<html>
<head>
    <title>NPChain Testnet Faucet</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Courier New', monospace; background: #0a0a0a; color: #00ff88; padding: 40px; }
        .container { max-width: 600px; margin: 0 auto; }
        h1 { font-size: 24px; margin-bottom: 8px; color: #00ff88; }
        .subtitle { color: #666; margin-bottom: 40px; }
        .info-box { background: #111; border: 1px solid #333; padding: 20px; margin-bottom: 30px; border-radius: 4px; }
        .info-row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #222; }
        .info-label { color: #888; }
        .info-value { color: #00ff88; font-weight: bold; }
        input { width: 100%; padding: 14px; background: #111; border: 1px solid #333; color: #fff; font-family: 'Courier New', monospace; font-size: 14px; margin-bottom: 16px; border-radius: 4px; }
        input:focus { outline: none; border-color: #00ff88; }
        button { width: 100%; padding: 14px; background: #00ff88; color: #000; border: none; font-family: 'Courier New', monospace; font-size: 16px; font-weight: bold; cursor: pointer; border-radius: 4px; }
        button:hover { background: #00cc66; }
        button:disabled { background: #333; color: #666; cursor: not-allowed; }
        .result { margin-top: 20px; padding: 16px; border-radius: 4px; }
        .result.success { background: #0a2a0a; border: 1px solid #00ff88; }
        .result.error { background: #2a0a0a; border: 1px solid #ff4444; color: #ff4444; }
        .stats { margin-top: 40px; color: #444; font-size: 12px; text-align: center; }
    </style>
</head>
<body>
    <div class="container">
        <h1>NPChain Testnet Faucet</h1>
        <p class="subtitle">Free test Certs for development and testing</p>

        <div class="info-box">
            <div class="info-row">
                <span class="info-label">Network</span>
                <span class="info-value">NPChain Testnet</span>
            </div>
            <div class="info-row">
                <span class="info-label">Drip Amount</span>
                <span class="info-value">{{ drip_amount }} Certs</span>
            </div>
            <div class="info-row">
                <span class="info-label">Cooldown</span>
                <span class="info-value">{{ cooldown_min }} minutes</span>
            </div>
            <div class="info-row">
                <span class="info-label">Consensus</span>
                <span class="info-value">Proof-of-NP-Witness</span>
            </div>
            <div class="info-row">
                <span class="info-label">Crypto</span>
                <span class="info-value">Dilithium + Kyber (Post-Quantum)</span>
            </div>
        </div>

        <input type="text" id="address" placeholder="Enter your NPChain testnet address (cert1...)" />
        <button onclick="requestDrip()" id="btn">Request {{ drip_amount }} Certs</button>

        <div id="result" style="display:none"></div>

        <div class="stats">
            NPChain — Proof-of-NP-Witness • Quantum-Resistant • Privacy-Preserving
        </div>
    </div>

    <script>
    async function requestDrip() {
        const addr = document.getElementById('address').value.trim();
        const btn = document.getElementById('btn');
        const result = document.getElementById('result');

        if (!addr) { alert('Please enter an address'); return; }

        btn.disabled = true;
        btn.textContent = 'Sending...';

        try {
            const res = await fetch('/api/drip', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({address: addr})
            });
            const data = await res.json();

            result.style.display = 'block';
            if (data.success) {
                result.className = 'result success';
                result.innerHTML = '✓ Sent ' + data.amount + ' Certs<br>TX: ' + data.tx_id;
            } else {
                result.className = 'result error';
                result.innerHTML = '✗ ' + data.error;
            }
        } catch(e) {
            result.style.display = 'block';
            result.className = 'result error';
            result.innerHTML = '✗ Network error: ' + e.message;
        }

        btn.disabled = false;
        btn.textContent = 'Request {{ drip_amount }} Certs';
    }
    </script>
</body>
</html>
"""

@app.route("/")
def index():
    return render_template_string(FAUCET_HTML,
        drip_amount=f"{DRIP_AMOUNT:,}",
        cooldown_min=COOLDOWN // 60)

@app.route("/api/drip", methods=["POST"])
def drip():
    data = request.get_json()
    if not data or "address" not in data:
        return jsonify({"success": False, "error": "Missing address"}), 400

    address = data["address"].strip()

    # Basic address validation
    if not address.startswith("cert1") or len(address) < 20:
        return jsonify({"success": False, "error": "Invalid address format (expected cert1...)"}), 400

    # Check cooldown
    now = time.time()
    if address in last_drip:
        elapsed = now - last_drip[address]
        if elapsed < COOLDOWN:
            remaining = int(COOLDOWN - elapsed)
            return jsonify({
                "success": False,
                "error": f"Cooldown active. Try again in {remaining // 60}m {remaining % 60}s"
            }), 429

    # Send via RPC
    try:
        rpc_response = requests.post(
            f"{RPC_URL}/api/v1/send",
            json={
                "to": address,
                "amount": DRIP_AMOUNT,
                "memo": "Testnet faucet drip"
            },
            headers={"Authorization": f"Bearer {RPC_KEY}"},
            timeout=30
        )

        if rpc_response.status_code == 200:
            result = rpc_response.json()
            last_drip[address] = now
            return jsonify({
                "success": True,
                "amount": f"{DRIP_AMOUNT:,}",
                "tx_id": result.get("tx_id", "pending"),
                "address": address
            })
        else:
            return jsonify({
                "success": False,
                "error": f"RPC error: {rpc_response.status_code}"
            }), 502

    except requests.exceptions.ConnectionError:
        return jsonify({
            "success": False,
            "error": "Cannot reach node RPC. Chain may still be syncing."
        }), 503
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500

@app.route("/api/status")
def status():
    try:
        r = requests.get(f"{RPC_URL}/api/v1/status", timeout=5)
        chain = r.json() if r.status_code == 200 else {}
    except:
        chain = {"error": "cannot reach node"}

    return jsonify({
        "faucet": "online",
        "drip_amount": DRIP_AMOUNT,
        "cooldown_sec": COOLDOWN,
        "addresses_served": len(last_drip),
        "chain": chain
    })

if __name__ == "__main__":
    print(f"\n  NPChain Testnet Faucet")
    print(f"  RPC:        {RPC_URL}")
    print(f"  Drip:       {DRIP_AMOUNT:,} Certs")
    print(f"  Cooldown:   {COOLDOWN}s")
    print(f"  Listening:  0.0.0.0:{LISTEN_PORT}\n")
    app.run(host="0.0.0.0", port=LISTEN_PORT)

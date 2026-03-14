@echo off
REM ═══════════════════════════════════════════════════════════════════
REM  NPChain 8-Proxy Shield — Windows Launcher
REM  Requires Docker Desktop running
REM ═══════════════════════════════════════════════════════════════════

if "%1"=="" (
    set MINER_ADDRESS=cert189e6b5cd23fe7fabb499b8a7ae781e8b77df5755
) else (
    set MINER_ADDRESS=%1
)

echo.
echo ╔═══════════════════════════════════════════════════════════╗
echo ║       NPChain 8-Proxy Shield — Testnet Launch            ║
echo ║       Miner: %MINER_ADDRESS%
echo ╚═══════════════════════════════════════════════════════════╝
echo.

echo [SHIELD] Building containers...
docker-compose build --parallel

echo.
echo [SHIELD] Starting 8-proxy shield + core node...
docker-compose up -d

echo.
echo [SHIELD] Container status:
docker-compose ps

echo.
echo ═══════════════════════════════════════════════════════════════
echo   SHIELD ACTIVE
echo   P2P:  port 19333    (forward this on your router)
echo   RPC:  port 18333    (forward this on your router)
echo.
echo   Testers connect with:
echo   npchain_testnet.exe --seed YOUR_PUBLIC_IP:19333
echo.
echo   Logs:  docker-compose logs -f
echo   Stop:  docker-compose down
echo ═══════════════════════════════════════════════════════════════
pause

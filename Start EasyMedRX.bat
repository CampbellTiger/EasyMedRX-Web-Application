@echo off
:: Launches EasyMedRX from WSL with elevated privileges so netsh port proxy works.
:: Must be run as Administrator (UAC prompt will appear if not already elevated).

:: ── Self-elevate if not already running as Administrator ──────────────────────
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

:: ── Firewall — ensure ports 8000 and 8443 are open ───────────────────────────
echo Configuring firewall...
powershell -NoProfile -Command "Remove-NetFirewallRule -DisplayName 'EasyMedRX' -ErrorAction SilentlyContinue; New-NetFirewallRule -DisplayName 'EasyMedRX' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8000,8443 | Out-Null; Write-Host '  Firewall: ports 8000 and 8443 open'"

:: ── Launch startup.py inside WSL ──────────────────────────────────────────────
echo Starting EasyMedRX...
wsl -e bash -c "cd /home/tiger/EasyMedRX-Web-Application && ./venv/bin/python startup.py"

echo.
echo Startup complete. You can close this window.
pause

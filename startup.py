import subprocess
import os
import time
import glob
import socket

def get_wsl_ip():
    """Return the WSL2 LAN IP (the address Daphne will bind to)."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return '127.0.0.1'


def get_windows_lan_ip():
    """Return the Windows host's Wi-Fi LAN IP by querying PowerShell."""
    try:
        result = subprocess.run(
            ['powershell.exe', '-NoProfile', '-Command',
             "(Get-NetIPAddress -AddressFamily IPv4 | Where-Object "
             "{ $_.InterfaceAlias -like 'Wi-Fi' -and $_.AddressState -eq 'Preferred' }).IPAddress"],
            capture_output=True, text=True, timeout=5
        )
        ip = result.stdout.strip()
        if ip:
            return ip
    except Exception:
        pass
    return None


def setup_port_proxy(wsl_ip):
    """Forward Windows 0.0.0.0:8000 → WSL2 {wsl_ip}:8000 so LAN devices can reach Daphne."""
    # Remove any stale entry first (ignore errors if none exists)
    subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command',
         'netsh interface portproxy delete v4tov4 listenport=8000 listenaddress=0.0.0.0'],
        capture_output=True
    )
    result = subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command',
         f'netsh interface portproxy add v4tov4 listenport=8000 '
         f'listenaddress=0.0.0.0 connectport=8000 connectaddress={wsl_ip}'],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        print(f"  Port proxy: 0.0.0.0:8000 → {wsl_ip}:8000")
    else:
        print(f"  Port proxy: FAILED — try running startup.py as admin")
        print(f"    {result.stderr.strip()}")


def ensure_firewall_rule():
    """Add a Windows Firewall inbound rule for port 8000 if one doesn't exist."""
    check_cmd = "Get-NetFirewallRule -DisplayName 'EasyMedRX' -ErrorAction SilentlyContinue"
    result = subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command', check_cmd],
        capture_output=True, text=True
    )
    if not result.stdout.strip():
        add_cmd = (
            "New-NetFirewallRule -DisplayName 'EasyMedRX' "
            "-Direction Inbound -Action Allow -Protocol TCP -LocalPort 8000 | Out-Null"
        )
        subprocess.run(
            ['powershell.exe', '-NoProfile', '-Command', add_cmd],
            capture_output=True
        )
        print("  Firewall: inbound rule added for port 8000")
    else:
        print("  Firewall: rule already exists")


WSL_IP      = get_wsl_ip()
WINDOWS_IP  = get_windows_lan_ip()


# ── Clean up stale state ───────────────────────────────────────────────────────

for f in glob.glob("celerybeat-schedule*"):
    os.remove(f)
    print(f"Removed {f}")

print("\nStopping stale processes...")
subprocess.run(["pkill", "-9", "-f", "celery"],  stderr=subprocess.DEVNULL)
subprocess.run(["pkill", "-9", "-f", "daphne"],  stderr=subprocess.DEVNULL)

# Wait until port 8000 is actually free (mirrored networking can delay release)
print("Waiting for port 8000 to be released...", end="", flush=True)
for _ in range(30):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("0.0.0.0", 8000))
            break
        except OSError:
            print(".", end="", flush=True)
            time.sleep(1)
else:
    print("\nERROR: Port 8000 still in use after 30s. Aborting.")
    raise SystemExit(1)
print(" free!")

for f in ["celerybeat-schedule", "celerybeat-schedule.db"]:
    if os.path.exists(f):
        os.remove(f)
        print(f"Removed {f}")


# ── Network / Firewall ────────────────────────────────────────────────────────

print("\nConfiguring network access...")
setup_port_proxy(WSL_IP)
ensure_firewall_rule()

# Pass both IPs to Django via environment so ALLOWED_HOSTS stays up to date
os.environ['DJANGO_WSL_IP']     = WSL_IP
os.environ['DJANGO_WINDOWS_IP'] = WINDOWS_IP or ''

print("\n  Access URLs:")
print(f"    WSL2    →  http://{WSL_IP}:8000")
if WINDOWS_IP:
    print(f"    LAN/MCU →  http://{WINDOWS_IP}:8000")
print()


# ── Start services ─────────────────────────────────────────────────────────────

print("Starting services...")
beat_log   = open("logs/celery_beat.log",   "w")
worker_log = open("logs/celery_worker.log", "w")
daphne_log = open("logs/daphne.log",        "w")

subprocess.Popen(["celery", "-A", "prescription_system", "beat",   "--loglevel=info"],              stdout=beat_log,   stderr=beat_log)
subprocess.Popen(["celery", "-A", "prescription_system", "worker", "-n", "worker1@%h", "--loglevel=info"], stdout=worker_log, stderr=worker_log)
subprocess.Popen(["daphne", "-b", "0.0.0.0", "-p", "8000", "prescription_system.asgi:application"], stdout=daphne_log, stderr=daphne_log)

print("All services started!")
print("Logs: logs/celery_beat.log | logs/celery_worker.log | logs/daphne.log")

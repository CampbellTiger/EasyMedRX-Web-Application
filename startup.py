import subprocess
import os
import time
import glob


# ── Helpers ────────────────────────────────────────────────────────────────────

def get_wsl_ip():
    """Return the WSL interface's own IP address."""
    try:
        result = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
        return result.stdout.strip().split()[0]
    except Exception:
        return '127.0.0.1'


# Static LAN IP — update this if the Windows host IP changes
WINDOWS_IP = '10.102.253.90'


def setup_port_proxy(wsl_ip):
    """
    Forward Windows host 0.0.0.0:8000 → WSL IP:8000 using netsh portproxy.
    Replaces proxy.bat — runs via PowerShell so no separate .bat file is needed.
    """
    ps_cmd = (
        f"netsh interface portproxy delete v4tov4 listenport=8000 listenaddress=0.0.0.0 2>$null; "
        f"netsh interface portproxy add v4tov4 listenport=8000 listenaddress=0.0.0.0 "
        f"connectport=8000 connectaddress={wsl_ip}"
    )
    result = subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command', ps_cmd],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        print(f"  Port proxy: Windows 0.0.0.0:8000 → WSL {wsl_ip}:8000")
    else:
        print(f"  Port proxy failed (run as Administrator): {result.stderr.strip()}")


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


def update_windows_hosts():
    """
    Add '127.0.0.1 easymedrx.local' to the Windows hosts file so the host
    machine can reach the app by name. LAN clients use the Windows LAN IP directly.
    """
    hosts_path = '/mnt/c/Windows/System32/drivers/etc/hosts'
    hostname = 'easymedrx.local'
    entry = f'127.0.0.1 {hostname}'
    try:
        with open(hosts_path, 'r') as f:
            content = f.read()
        if hostname not in content:
            with open(hosts_path, 'a') as f:
                f.write(f'\n{entry}\n')
            print(f"  Hosts file: added '{entry}'")
        else:
            print(f"  Hosts file: '{hostname}' already present")
    except PermissionError:
        print(f"  Hosts file: permission denied — run as Administrator to enable {hostname}")
    except Exception as e:
        print(f"  Hosts file: could not update ({e})")


# ── Clean up stale state ───────────────────────────────────────────────────────

for f in glob.glob("celerybeat-schedule*"):
    os.remove(f)
    print(f"Removed {f}")

print("\nStopping stale processes...")
subprocess.run(["pkill", "-9", "-f", "celery"],  stderr=subprocess.DEVNULL)
subprocess.run(["pkill", "-9", "-f", "daphne"],  stderr=subprocess.DEVNULL)
time.sleep(2)

for f in ["celerybeat-schedule", "celerybeat-schedule.db"]:
    if os.path.exists(f):
        os.remove(f)
        print(f"Removed {f}")


# ── LAN / WSL network setup ───────────────────────────────────────────────────

print("\nConfiguring LAN access...")
wsl_ip = get_wsl_ip()

setup_port_proxy(wsl_ip)
ensure_firewall_rule()
update_windows_hosts()

print("\n  Access URLs:")
print(f"    Local  →  http://easymedrx.local:8000")
print(f"    LAN    →  http://{WINDOWS_IP}:8000")
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

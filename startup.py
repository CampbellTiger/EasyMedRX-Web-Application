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
    """Forward Windows 0.0.0.0:{port} → WSL2 {wsl_ip}:{port} for both HTTP and HTTPS."""
    for port in (8000, 8443):
        subprocess.run(
            ['powershell.exe', '-NoProfile', '-Command',
             f'netsh interface portproxy delete v4tov4 listenport={port} listenaddress=0.0.0.0'],
            capture_output=True
        )
        result = subprocess.run(
            ['powershell.exe', '-NoProfile', '-Command',
             f'netsh interface portproxy add v4tov4 listenport={port} '
             f'listenaddress=0.0.0.0 connectport={port} connectaddress={wsl_ip}'],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            print(f"  Port proxy: 0.0.0.0:{port} → {wsl_ip}:{port}")
        else:
            print(f"  Port proxy {port}: FAILED — try running startup.py as admin")
            print(f"    {result.stderr.strip()}")


def ensure_vapid_keys():
    """Generate VAPID keys and write them to .env if they are blank."""
    env_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
    if not os.path.exists(env_path):
        print("  VAPID: .env not found, skipping")
        return

    with open(env_path, "r") as f:
        contents = f.read()

    # Check if either key is blank
    import re
    pub_match  = re.search(r'^VAPID_PUBLIC_KEY=(.*)$',  contents, re.MULTILINE)
    priv_match = re.search(r'^VAPID_PRIVATE_KEY=(.*)$', contents, re.MULTILINE)
    if pub_match and priv_match and pub_match.group(1).strip() and priv_match.group(1).strip():
        print("  VAPID: keys already set")
        return

    print("  VAPID: generating new key pair...")
    try:
        import base64
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives import serialization

        private_key = ec.generate_private_key(ec.SECP256R1())
        public_key  = private_key.public_key()

        pub = base64.urlsafe_b64encode(
            public_key.public_bytes(
                serialization.Encoding.X962,
                serialization.PublicFormat.UncompressedPoint
            )
        ).decode().rstrip("=")

        priv = base64.urlsafe_b64encode(
            private_key.private_bytes(
                serialization.Encoding.DER,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption()
            )
        ).decode().rstrip("=")

        contents = re.sub(r'^VAPID_PUBLIC_KEY=.*$',  f'VAPID_PUBLIC_KEY={pub}',  contents, flags=re.MULTILINE)
        contents = re.sub(r'^VAPID_PRIVATE_KEY=.*$', f'VAPID_PRIVATE_KEY={priv}', contents, flags=re.MULTILINE)

        with open(env_path, "w") as f:
            f.write(contents)
        print("  VAPID: keys written to .env")
    except Exception as ex:
        print(f"  VAPID: generation failed — {ex}")


def ensure_ssl_cert():
    """Generate a self-signed cert/key if either file is missing."""
    if os.path.exists("cert.pem") and os.path.exists("key.pem"):
        print("  SSL: certificate already exists")
        return
    print("  SSL: generating self-signed certificate...")
    result = subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048",
         "-keyout", "key.pem", "-out", "cert.pem",
         "-days", "365", "-nodes",
         "-subj", "/CN=easymrdrx.local"],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        print("  SSL: cert.pem and key.pem generated")
    else:
        print("  SSL: FAILED to generate certificate")
        print(f"    {result.stderr.strip()}")
        raise SystemExit(1)


def ensure_firewall_rule():
    """Add a Windows Firewall inbound rule for ports 8000 and 8443 if one doesn't exist."""
    check_cmd = "Get-NetFirewallRule -DisplayName 'EasyMedRX' -ErrorAction SilentlyContinue"
    result = subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command', check_cmd],
        capture_output=True, text=True
    )
    if not result.stdout.strip():
        add_cmd = (
            "New-NetFirewallRule -DisplayName 'EasyMedRX' "
            "-Direction Inbound -Action Allow -Protocol TCP -LocalPort 8000,8443 | Out-Null"
        )
        subprocess.run(
            ['powershell.exe', '-NoProfile', '-Command', add_cmd],
            capture_output=True
        )
        print("  Firewall: inbound rule added for ports 8000 and 8443")
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
ensure_vapid_keys()
ensure_ssl_cert()
setup_port_proxy(WSL_IP)
ensure_firewall_rule()

# Pass both IPs to Django via environment so ALLOWED_HOSTS stays up to date
os.environ['DJANGO_WSL_IP']     = WSL_IP
os.environ['DJANGO_WINDOWS_IP'] = WINDOWS_IP or ''

print("\n  Access URLs:")
print(f"    WSL2    →  http://{WSL_IP}:8000   (HTTP)")
print(f"    WSL2    →  https://{WSL_IP}:8443  (HTTPS)")
if WINDOWS_IP:
    print(f"    LAN/MCU →  http://{WINDOWS_IP}:8000   (HTTP)")
    print(f"    LAN/MCU →  https://{WINDOWS_IP}:8443  (HTTPS)")
print()


# ── Start services ─────────────────────────────────────────────────────────────

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
VENV_BIN = os.path.join(BASE_DIR, "venv", "bin")
CELERY  = os.path.join(VENV_BIN, "celery")
DAPHNE  = os.path.join(VENV_BIN, "daphne")

print("Starting services...")
os.makedirs("logs", exist_ok=True)
beat_log   = open("logs/celery_beat.log",   "w")
worker_log = open("logs/celery_worker.log", "w")
daphne_log = open("logs/daphne.log",        "w")

ssl_log = open("logs/daphne_ssl.log", "w")

subprocess.Popen([CELERY, "-A", "prescription_system", "beat",   "--loglevel=info"],              stdout=beat_log,   stderr=beat_log,   start_new_session=True)
subprocess.Popen([CELERY, "-A", "prescription_system", "worker", "-n", "worker1@%h", "--loglevel=info"], stdout=worker_log, stderr=worker_log, start_new_session=True)
subprocess.Popen([DAPHNE, "-b", "0.0.0.0", "-p", "8000", "prescription_system.asgi:application"],                                                                   stdout=daphne_log, stderr=daphne_log, start_new_session=True)
subprocess.Popen([DAPHNE, "-e", f"ssl:8443:privateKey={BASE_DIR}/key.pem:certKey={BASE_DIR}/cert.pem", "prescription_system.asgi:application"], stdout=ssl_log, stderr=ssl_log, start_new_session=True)

print("All services started!")
print("Logs: logs/celery_beat.log | logs/celery_worker.log | logs/daphne.log | logs/daphne_ssl.log")

import subprocess
import os
import time
import glob
import socket


# ── Paths ──────────────────────────────────────────────────────────────────────

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
VENV_BIN = os.path.join(BASE_DIR, "venv", "bin")
PYTHON  = os.path.join(VENV_BIN, "python")
CELERY  = os.path.join(VENV_BIN, "celery")
DAPHNE  = os.path.join(VENV_BIN, "daphne")
CERT    = os.path.join(BASE_DIR, "cert.pem")
KEY     = os.path.join(BASE_DIR, "key.pem")


# ── Helpers ────────────────────────────────────────────────────────────────────

def get_pi_ip():
    """Return the Pi's primary LAN IP address."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return '127.0.0.1'


def ensure_redis():
    """Start Redis if it isn't already running."""
    result = subprocess.run(
        ["redis-cli", "ping"],
        capture_output=True, text=True
    )
    if result.stdout.strip() == "PONG":
        print("  Redis: already running")
    else:
        print("  Redis: starting...")
        subprocess.Popen(
            ["redis-server", "--daemonize", "yes"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        # Wait up to 5s for Redis to come up
        for _ in range(10):
            time.sleep(0.5)
            r = subprocess.run(["redis-cli", "ping"], capture_output=True, text=True)
            if r.stdout.strip() == "PONG":
                print("  Redis: started")
                return
        print("  Redis: WARNING — did not respond after 5s, continuing anyway")


def ensure_vapid_keys():
    """Generate VAPID keys and write them to .env if they are blank."""
    env_path = os.path.join(BASE_DIR, ".env")
    if not os.path.exists(env_path):
        print("  VAPID: .env not found, skipping")
        return

    with open(env_path, "r") as f:
        contents = f.read()

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
    if os.path.exists(CERT) and os.path.exists(KEY):
        print("  SSL: certificate already exists")
        return
    print("  SSL: generating self-signed certificate...")
    result = subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048",
         "-keyout", KEY, "-out", CERT,
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


def open_firewall_ports():
    """Allow ports 8000 and 8443 through ufw if ufw is active."""
    status = subprocess.run(
        ["sudo", "ufw", "status"],
        capture_output=True, text=True
    )
    if "Status: active" not in status.stdout:
        print("  Firewall: ufw inactive, skipping")
        return
    for port in (8000, 8443):
        subprocess.run(
            ["sudo", "ufw", "allow", str(port), "comment", "EasyMedRX"],
            capture_output=True
        )
    print("  Firewall: ports 8000 and 8443 open")


# ── Clean up stale state ───────────────────────────────────────────────────────

for f in glob.glob(os.path.join(BASE_DIR, "celerybeat-schedule*")):
    os.remove(f)
    print(f"Removed {f}")

print("\nStopping stale processes...")
subprocess.run(["pkill", "-9", "-f", "celery"], stderr=subprocess.DEVNULL)
subprocess.run(["pkill", "-9", "-f", "daphne"], stderr=subprocess.DEVNULL)

# Wait until port 8000 is actually free
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


# ── Redis ──────────────────────────────────────────────────────────────────────

print("\nChecking dependencies...")
ensure_redis()


# ── Database migrations ────────────────────────────────────────────────────────

print("\nRunning migrations...")
result = subprocess.run(
    [PYTHON, "manage.py", "migrate", "--run-syncdb"],
    capture_output=True, text=True,
    cwd=BASE_DIR
)
if result.returncode == 0:
    print("  Migrations: OK")
else:
    print("  Migrations: FAILED")
    print(result.stderr.strip())
    raise SystemExit(1)


# ── Network ────────────────────────────────────────────────────────────────────

PI_IP = get_pi_ip()

print("\nConfiguring network access...")
ensure_vapid_keys()
ensure_ssl_cert()
open_firewall_ports()

os.environ['DJANGO_WSL_IP']     = PI_IP
os.environ['DJANGO_WINDOWS_IP'] = PI_IP

print("\n  Access URLs:")
print(f"    Local   →  http://localhost:8000        (HTTP)")
print(f"    Local   →  https://localhost:8443       (HTTPS)")
print(f"    LAN/MCU →  http://{PI_IP}:8000   (HTTP)")
print(f"    LAN/MCU →  https://{PI_IP}:8443  (HTTPS)")
print()


# ── Start services ─────────────────────────────────────────────────────────────

def _rotate_log(path, keep=3):
    """Rename path → path.1 → path.2 … up to `keep` backups, then truncate."""
    for i in range(keep - 1, 0, -1):
        src = f"{path}.{i}"
        dst = f"{path}.{i + 1}"
        if os.path.exists(src):
            os.replace(src, dst)
    if os.path.exists(path):
        os.replace(path, f"{path}.1")

print("Starting services...")
os.makedirs(os.path.join(BASE_DIR, "logs"), exist_ok=True)
for _name in ["celery_beat.log", "celery_worker.log", "daphne.log", "daphne_ssl.log"]:
    _rotate_log(os.path.join(BASE_DIR, "logs", _name))

beat_log   = open(os.path.join(BASE_DIR, "logs", "celery_beat.log"),   "w")
worker_log = open(os.path.join(BASE_DIR, "logs", "celery_worker.log"), "w")
daphne_log = open(os.path.join(BASE_DIR, "logs", "daphne.log"),        "w")
ssl_log    = open(os.path.join(BASE_DIR, "logs", "daphne_ssl.log"),    "w")

subprocess.Popen(
    [CELERY, "-A", "prescription_system", "beat", "--loglevel=info"],
    stdout=beat_log, stderr=beat_log, start_new_session=True, cwd=BASE_DIR
)
subprocess.Popen(
    [CELERY, "-A", "prescription_system", "worker", "-n", "worker1@%h", "--loglevel=info"],
    stdout=worker_log, stderr=worker_log, start_new_session=True, cwd=BASE_DIR
)
subprocess.Popen(
    [DAPHNE, "-b", "0.0.0.0", "-p", "8000", "prescription_system.asgi:application"],
    stdout=daphne_log, stderr=daphne_log, start_new_session=True, cwd=BASE_DIR
)
subprocess.Popen(
    [DAPHNE, "-e", f"ssl:8443:privateKey={KEY}:certKey={CERT}", "prescription_system.asgi:application"],
    stdout=ssl_log, stderr=ssl_log, start_new_session=True, cwd=BASE_DIR
)

print("All services started!")
print("Logs: logs/celery_beat.log | logs/celery_worker.log | logs/daphne.log | logs/daphne_ssl.log")

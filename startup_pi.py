import subprocess
import os
import time
import glob


# ── Helpers ────────────────────────────────────────────────────────────────────

def get_pi_ip():
    """Return the Pi's primary LAN IP address."""
    try:
        result = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
        return result.stdout.strip().split()[0]
    except Exception:
        return '127.0.0.1'


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


# ── Network info ──────────────────────────────────────────────────────────────

pi_ip = get_pi_ip()

print("\n  Access URLs:")
print(f"    Local  →  http://localhost:8000")
print(f"    LAN    →  http://{pi_ip}:8000")
print()


# ── Start services ─────────────────────────────────────────────────────────────

print("Starting services...")
os.makedirs("logs", exist_ok=True)
beat_log   = open("logs/celery_beat.log",   "w")
worker_log = open("logs/celery_worker.log", "w")
daphne_log = open("logs/daphne.log",        "w")

subprocess.Popen(["celery", "-A", "prescription_system", "beat",   "--loglevel=info"],              stdout=beat_log,   stderr=beat_log)
subprocess.Popen(["celery", "-A", "prescription_system", "worker", "-n", "worker1@%h", "--loglevel=info"], stdout=worker_log, stderr=worker_log)
subprocess.Popen(["daphne", "-b", "0.0.0.0", "-p", "8000", "prescription_system.asgi:application"], stdout=daphne_log, stderr=daphne_log)

print("All services started!")
print("Logs: logs/celery_beat.log | logs/celery_worker.log | logs/daphne.log")

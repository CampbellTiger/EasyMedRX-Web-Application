import subprocess
import os
import time
import glob

# Clean all variants of the schedule file
for f in glob.glob("celerybeat-schedule*"):
    os.remove(f)
    print(f"Removed {f}")

print("Stopping stale processes...")
subprocess.run(["pkill", "-f", "celery beat"],   stderr=subprocess.DEVNULL)
subprocess.run(["pkill", "-f", "celery worker"], stderr=subprocess.DEVNULL)
subprocess.run(["pkill", "-f", "daphne"],        stderr=subprocess.DEVNULL)
time.sleep(2)


# Clean stale beat schedule lock
for f in ["celerybeat-schedule", "celerybeat-schedule.db"]:
    if os.path.exists(f):
        os.remove(f)
        print(f"Removed {f}")

print("Starting services...")
subprocess.Popen(["celery", "-A", "prescription_system", "beat",   "--loglevel=info"])
subprocess.Popen(["celery", "-A", "prescription_system", "worker", "-n", "worker1@%h", "--loglevel=info"])
subprocess.Popen(["daphne", "-b", "0.0.0.0", "-p", "8000", "prescription_system.asgi:application"])

print("All services started!")
# startup.py
import subprocess
import os
import time

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
os.chdir(PROJECT_DIR)

VENV_ACTIVATE = os.path.join(PROJECT_DIR, "venv/bin/activate")

if not os.path.exists(VENV_ACTIVATE):
    print("Virtual environment not found. Please create it first.")
    exit(1)

def run_bg(cmd, name):
    print(f"[STARTING] {name}...")
    # Run command in background
    subprocess.Popen(f"bash -c 'source venv/bin/activate && {cmd}'", shell=True)
    time.sleep(1)

# Start Redis
run_bg("sudo service redis-server start && redis-cli ping", "Redis")

# Start Celery Worker
run_bg("celery -A prescription_system worker -l info", "Celery Worker")

# Start Celery Beat
run_bg("celery -A prescription_system beat -l info", "Celery Beat")

# Start Daphne
run_bg("daphne -b 0.0.0.0 -p 8000 prescription_system.asgi:application", "Daphne")

print("All services started in background!")
print("Check your terminal with `ps aux | grep python` or logs in VS Code terminal.")
print("Visit http://localhost:8000 to test the application.")
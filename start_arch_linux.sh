#!/bin/bash

echo "Activating virtual environment..."
source venv/bin/activate

echo "Starting Redis..."
redis-server &

sleep 2

echo "Starting Celery Worker..."
celery -A prescription_system worker -l info &

echo "Starting Celery Beat..."
celery -A prescription_system beat -l info &

sleep 2

echo "Starting Daphne ASGI server..."
daphne -b 0.0.0.0 -p 8000 prescription_system.asgi:application

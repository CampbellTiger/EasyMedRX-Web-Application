import os
from celery import Celery
from celery.schedules import crontab

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'prescription_system.settings')

app = Celery('prescription_system')
app.config_from_object('django.conf:settings', namespace='CELERY')
app.autodiscover_tasks()


app.conf.beat_schedule = {
    'send-prescription-reminders-every-minute': {
        'task': 'prescriptions.tasks.send_due_prescription_notifications',
        'schedule': crontab(),  # runs every minute
    },
}
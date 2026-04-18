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
    'reset-prescription-ready-flags-daily': {
        'task': 'prescriptions.tasks.reset_prescription_ready_flags',
        'schedule': crontab(hour=0, minute=0),  # runs at midnight
    },
    'send-daily-report': {
        'task': 'prescriptions.tasks.send_daily_report',
        'schedule': crontab(hour=23, minute=0),  # runs at 11 PM daily
    },
    'send-weekly-report': {
        'task': 'prescriptions.tasks.send_weekly_report',
        'schedule': crontab(hour=23, minute=0, day_of_week=0),  # Sunday 11 PM
    },
}
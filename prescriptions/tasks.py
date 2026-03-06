from celery import shared_task
from channels.layers import get_channel_layer
from asgiref.sync import async_to_sync
from django.utils import timezone
from .models import Prescription
from .models import PrescriptionLogging

@shared_task
def send_due_prescription_notifications():
    now = timezone.localtime()
    due_prescriptions = Prescription.objects.filter(
        scheduled_time__hour=now.hour,
        scheduled_time__minute=now.minute,
        ready=False)
    channel_layer = get_channel_layer()

    for p in due_prescriptions:

        async_to_sync(channel_layer.group_send)(
            "notifications",
            {
                "type": "send_notification",
                "message": f"Time to take {p.medication_name}!"
            }
        )
        PrescriptionLogging.objects.create(
            user_Logs=p.user,
            prescriptions_logs=p,
            event_type_logs="REMINDER_SENT",
            scheduled_time_logs=p.scheduled_time
        )
        p.ready = True
        p.save()
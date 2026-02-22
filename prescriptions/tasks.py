from celery import shared_task
from channels.layers import get_channel_layer
from asgiref.sync import async_to_sync
from django.utils import timezone
from .models import Prescription

@shared_task
def send_due_prescription_notifications():
    now = timezone.now()
    due_prescriptions = Prescription.objects.filter(scheduled_time__lte=now, ready=False)
    channel_layer = get_channel_layer()

    for p in due_prescriptions:
        p.ready = True
        p.save()
        async_to_sync(channel_layer.group_send)(
            "notifications",
            {
                "type": "send_notification",
                "message": f"Time to take {p.medication_name}!"
            }
        )
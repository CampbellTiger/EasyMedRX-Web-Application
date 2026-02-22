from django.db.models.signals import post_save
from django.dispatch import receiver
from .models import Prescription
from channels.layers import get_channel_layer
from asgiref.sync import async_to_sync

@receiver(post_save, sender=Prescription)
def notify_ready_prescription(sender, instance, created, **kwargs):
    # Only send notification if ready just became True
    if instance.ready:
        channel_layer = get_channel_layer()
        async_to_sync(channel_layer.group_send)(
            "prescriptions",
            {
                "type": "prescription_ready",
                "message": f"Prescription {instance.id} is ready!"
            }
        )
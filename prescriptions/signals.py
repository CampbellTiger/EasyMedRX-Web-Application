from django.db.models.signals import post_save
from django.dispatch import receiver
from .models import Prescription
from .consumers import notify_user

@receiver(post_save, sender=Prescription)
def notify_ready_prescription(sender, instance, created, **kwargs):
    # Only notify if ready was set outside of the scheduled task (e.g. via admin)
    if instance.ready and instance.last_notified is None:
        notify_user(instance.user_id, f"Prescription {instance.medication_name} is ready!")
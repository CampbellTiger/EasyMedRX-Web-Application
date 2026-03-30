from django.db.models.signals import post_save
from django.contrib.auth.signals import user_login_failed
from django.dispatch import receiver
from .models import Prescription, FailedLoginLog
from .consumers import notify_user


@receiver(post_save, sender=Prescription)
def notify_ready_prescription(sender, instance, created, **kwargs):
    # Only notify if ready was set outside of the scheduled task (e.g. via admin)
    if instance.ready and instance.last_notified is None:
        notify_user(instance.user_id, f"Prescription {instance.medication_name} is ready!")


@receiver(user_login_failed)
def log_failed_login(sender, credentials, request, **kwargs):
    ip = (
        request.META.get('HTTP_X_FORWARDED_FOR', '').split(',')[0].strip()
        or request.META.get('REMOTE_ADDR')
    ) if request else None
    FailedLoginLog.objects.create(
        username=credentials.get('username', ''),
        ip=ip or None,
    )
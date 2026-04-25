from django.db.models.signals import post_save
from django.contrib.auth.signals import user_login_failed
from django.dispatch import receiver
from .models import Prescription, FailedLoginLog, NotificationPreference
from .consumers import notify_user
from .tasks import _mask


@receiver(post_save, sender=Prescription)
def notify_ready_prescription(sender, instance, created, **kwargs):
    # Only notify if ready was set outside of the scheduled task (e.g. via admin)
    if instance.ready and instance.last_notified is None:
        notify_user(instance.user_id, f"Prescription {instance.medication_name} is ready!")


@receiver(post_save, sender=Prescription)
def prescription_change_alert(sender, instance, created, **kwargs):
    try:
        prefs, _ = NotificationPreference.objects.get_or_create(user=instance.user)
        if not prefs.prescription_reminder_enabled:
            return
        action = "added" if created else "updated"
        masked_med = _mask(instance.medication_name)
        message = f"Your prescription for {masked_med} has been {action}."
        notify_user(instance.user.id, message)
        if prefs.email_enabled and instance.user.email:
            from django.core.mail import send_mail
            from django.conf import settings
            app_url = getattr(settings, 'APP_BASE_URL', 'http://localhost:8000')
            display_name = _mask(instance.user.first_name or instance.user.username)
            send_mail(
                subject=f"Prescription {action.capitalize()}: {masked_med}",
                message=(
                    f"Hello {display_name},\n\n"
                    f"Your prescription has been {action}.\n\n"
                    f"Medication:  {masked_med}\n"
                    f"Dosage:      {instance.dosage}\n"
                    f"Doses/day:   {instance.doses_per_day}\n"
                    f"Container:   {instance.container}\n"
                    f"Doctor:      {instance.prescribing_doctor}\n"
                    + (f"Instructions: {instance.instructions}\n" if instance.instructions else "")
                    + f"\n─────────────────────────────\n"
                    f"Open EasyMedRX: {app_url}\n"
                ),
                from_email=settings.DEFAULT_FROM_EMAIL,
                recipient_list=[instance.user.email],
                fail_silently=True,
            )
    except Exception:
        pass


@receiver(post_save, sender=Prescription)
def low_stock_alert(sender, instance, **kwargs):
    if instance.stock_count >= 5:
        return
    try:
        prefs, _ = NotificationPreference.objects.get_or_create(user=instance.user)
        if not prefs.low_stock_alert_enabled:
            return
        notify_user(instance.user.id, f"Low stock: {instance.medication_name} has {instance.stock_count} pill(s) remaining.")
        if prefs.email_enabled and instance.user.email:
            from django.core.mail import send_mail
            from django.conf import settings
            app_url = getattr(settings, 'APP_BASE_URL', 'http://localhost:8000')
            send_mail(
                subject=f"Low Stock Alert: {instance.medication_name}",
                message=(
                    f"Hello,\n\n"
                    f"The stock for {instance.medication_name} is running low.\n\n"
                    f"Current stock: {instance.stock_count} pill(s) remaining\n"
                    f"Container:     {instance.container}\n\n"
                    f"Please refill soon.\n\n"
                    f"─────────────────────────────\n"
                    f"Open EasyMedRX: {app_url}\n"
                ),
                from_email=settings.DEFAULT_FROM_EMAIL,
                recipient_list=[instance.user.email],
                fail_silently=True,
            )
    except Exception:
        pass


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
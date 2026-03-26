from celery import shared_task
from django.db.models import Q
from django.utils import timezone
from django.core.mail import send_mail
from django.conf import settings

from .models import Prescription, PrescriptionLogging
from .consumers import notify_user


@shared_task
def reset_prescription_ready_flags():
    """Reset ready=True prescriptions to False each midnight so they can fire again the next day."""
    updated = Prescription.objects.filter(ready=True).update(ready=False)
    return f"Reset {updated} prescription(s) to ready=False"


@shared_task
def send_due_prescription_notifications():
    now   = timezone.localtime()
    today = now.date()

    due_prescriptions = Prescription.objects.select_related('user').filter(
        scheduled_time__hour=now.hour,
        scheduled_time__minute=now.minute,
        ready=False,
        start_date__lte=today,
    ).filter(
        Q(end_date__isnull=True) | Q(end_date__gte=today)
    )

    for p in due_prescriptions:
        p.ready = True
        p.last_notified = now
        p.save(update_fields=['ready', 'last_notified'])

        # WebSocket notification — targeted to this user only
        print(f"[TASK] Sending notification for {p.medication_name} to user_id={p.user.id}")
        try:
            notify_user(p.user.id, f"Time to take {p.medication_name}!")
            print(f"[TASK] notify_user succeeded")
        except Exception as e:
            print(f"[TASK] notify_user FAILED: {e}")

        # Prescription logging
        PrescriptionLogging.objects.create(
            user=p.user,
            prescription=p,
            event_type="REMINDER_SENT",
            scheduled_time=p.scheduled_time,
        )

        # Email — only if the user has an address on file
        if p.user.email:
            end_date_line = f"End date:   {p.end_date}\n" if p.end_date else ""
            body = (
                f"Hello {p.user.first_name or p.user.username},\n\n"
                f"This is a reminder to take your medication.\n\n"
                f"Medication:  {p.medication_name}\n"
                f"Dosage:      {p.dosage}\n"
                f"Frequency:   {p.frequency}\n"
                f"Scheduled:   {p.scheduled_time.strftime('%I:%M %p')}\n"
                f"Start date:  {p.start_date}\n"
                f"{end_date_line}"
                f"Doctor:      {p.prescribing_doctor}\n"
            )
            if p.instructions:
                body += f"\nInstructions:\n{p.instructions}\n"

            send_mail(
                subject=f"Medication Reminder: {p.medication_name}",
                message=body,
                from_email=settings.DEFAULT_FROM_EMAIL,
                recipient_list=[p.user.email],
                fail_silently=True,
            )

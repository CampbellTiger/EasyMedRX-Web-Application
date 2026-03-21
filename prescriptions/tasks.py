from celery import shared_task
from django.utils import timezone
from django.core.mail import send_mail
from django.conf import settings

from .models import Prescription, PrescriptionLogging
from .consumers import notify_user


@shared_task
def send_due_prescription_notifications():
    now = timezone.localtime()
    due_prescriptions = Prescription.objects.select_related('user').filter(
        scheduled_time__hour=now.hour,
        scheduled_time__minute=now.minute,
        ready=False,
    )
    for p in due_prescriptions:
        p.ready = True
        p.last_notified = now
        p.save()

        # WebSocket notification — targeted to this user only
        notify_user(p.user.id, f"Time to take {p.medication_name}!")

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

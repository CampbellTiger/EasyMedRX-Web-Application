from django.db import models
from django.contrib.auth.models import User
from django.utils import timezone

class Prescription(models.Model):
    user = models.ForeignKey(
        User,
        on_delete=models.CASCADE,
        related_name='prescriptions'
    )
    medication_name = models.CharField(max_length=200)
    dosage = models.CharField(max_length=100)
    frequency = models.CharField(max_length=100)
    instructions = models.TextField(blank=True)
    prescribing_doctor = models.CharField(max_length=200)
    start_date = models.DateField()
    end_date = models.DateField(blank=True, null=True)
    scheduled_time = models.DateTimeField(default=timezone.now)
    ready = models.BooleanField(default=False)
    last_notified = models.DateTimeField(blank=True, null=True)
    def __str__(self):
        return f"{self.medication_name} ({self.user.username})"

class Device(models.Model):
    device_id = models.CharField(max_length=64, unique=True)
    patient = models.ForeignKey(User, on_delete=models.CASCADE)
    #RFID = models.CharField(max_length=64, unique=True)

#MODEL FOR LOGGING DATA FOR MISSES AND GRABBED PRECRIPTIONS
class PrescriptionLogging(models.Model):
    EVENT_TYPES = [
        ("REMINDER_SENT", "Reminder Sent"),
        ("TAKEN", 'Taken'),
        ("MISSED", "Missed")
    ]

    prescription = models.ForeignKey(
        "Prescription", 
        on_delete=models.CASCADE, 
        related_name="logs"  # Reverse: prescription.logs.all()
    )
    user = models.ForeignKey(
        User, 
        on_delete=models.CASCADE, 
        related_name="prescription_logs"  # Reverse: user.prescription_logs.all()
    )

    event_type = models.CharField(max_length=20, choices=EVENT_TYPES)
    scheduled_time = models.DateTimeField()  # When it was supposed to be taken
    event_time = models.DateTimeField(auto_now_add=True)  # When the log was recorded

    def __str__(self):
        return f"{self.event_type} - {self.prescription.medication_name} ({self.user.username})"
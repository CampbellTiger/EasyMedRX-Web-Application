from django.db import models
from django.contrib.auth.models import User
from django.utils import timezone


class UserProfile(models.Model):
    """Stores extra contact info not on Django's built-in User model."""
    user  = models.OneToOneField(User, on_delete=models.CASCADE, related_name='profile')
    phone = models.CharField(max_length=30, blank=True, default='')

    def __str__(self):
        return f"Profile({self.user.username})"


class Prescription(models.Model):
    user = models.ForeignKey(
        User,
        on_delete=models.CASCADE,
        related_name='prescriptions'
    )
    medication_name = models.CharField(max_length=200)
    dosage = models.CharField(max_length=100)
    # Number of units (pills) per dose — used in the MCU flat format 
    dose_count  = models.PositiveIntegerField(default=1)
    # Current stock level on the device — kept in sync via MCU push 
    stock_count = models.PositiveIntegerField(default=0)
    frequency = models.CharField(max_length=100)
    instructions = models.TextField(blank=True)
    prescribing_doctor = models.CharField(max_length=200)
    start_date = models.DateField()
    end_date = models.DateField(blank=True, null=True)
    scheduled_time = models.DateTimeField(default=timezone.now, db_index=True)
    ready = models.BooleanField(default=False, db_index=True)
    last_notified = models.DateTimeField(blank=True, null=True)
    def __str__(self):
        return f"{self.medication_name} ({self.user.username})"

class Device(models.Model):
    device_id = models.CharField(max_length=64, unique=True)
    patient = models.ForeignKey(User, on_delete=models.CASCADE)

    def __str__(self):
        return f"{self.device_id} ({self.patient.username})"

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

class ErrorLog(models.Model):
    device_id  = models.CharField(max_length=255)
    error_type = models.CharField(max_length=100)
    detail     = models.TextField()
    timestamp  = models.DateTimeField(auto_now_add=True)
    user       = models.ForeignKey(
        User,
        on_delete=models.SET_NULL,
        null=True,
        blank=True,
        related_name='error_logs'
    )

    def __str__(self):
        return f"[{self.timestamp}] {self.error_type} — {self.device_id}"


class FailedLoginLog(models.Model):
    username  = models.CharField(max_length=150)
    ip        = models.GenericIPAddressField(null=True, blank=True)
    timestamp = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"[{self.timestamp}] Failed login — {self.username} from {self.ip}"
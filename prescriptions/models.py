from django.db import models
from django.contrib.auth.models import User
from django.utils import timezone
from django.core.validators import MaxValueValidator, MinValueValidator

MAX_STOCK = 30


class UserProfile(models.Model):
    """Stores extra contact info not on Django's built-in User model."""
    user     = models.OneToOneField(User, on_delete=models.CASCADE, related_name='profile')
    phone    = models.CharField(max_length=30, blank=True, default='')
    rfid_uid = models.CharField(
        max_length=64,
        blank=True,
        default='',
        help_text='RFID card UID for this patient (e.g. 87447d33)',
    )

    def __str__(self):
        return f"Profile({self.user.username})"


class Device(models.Model):
    device_id = models.CharField(max_length=64, unique=True)
    patients  = models.ManyToManyField(User, related_name='devices', blank=True)

    def __str__(self):
        return self.device_id


class Prescription(models.Model):
    CONTAINER_CHOICES = [
        (1, 'Container 1'), (2, 'Container 2'),
        (3, 'Container 3'), (4, 'Container 4'),
    ]

    user   = models.ForeignKey(User, on_delete=models.CASCADE, related_name='prescriptions')
    device = models.ForeignKey(
        Device,
        on_delete=models.SET_NULL,
        null=True,
        blank=True,
        related_name='prescriptions',
    )
    medication_name    = models.CharField(max_length=200)
    dosage             = models.CharField(max_length=100)
    dose_count         = models.PositiveIntegerField(default=1)
    stock_count        = models.PositiveIntegerField(default=0, validators=[MaxValueValidator(MAX_STOCK)])
    add_stock          = models.PositiveIntegerField(default=0, validators=[MaxValueValidator(MAX_STOCK)], help_text='Pills to add on top of the next MCU stock report (capped so total does not exceed 30)')
    doses_per_day      = models.PositiveSmallIntegerField(default=1, help_text='Number of doses per day')
    window_before_minutes = models.PositiveSmallIntegerField(default=15, validators=[MinValueValidator(5)], help_text='Minutes before dose time the window opens (minimum 5)')
    window_minutes        = models.PositiveSmallIntegerField(default=30, validators=[MinValueValidator(5)], help_text='Minutes after dose time before window expires (minimum 5)')
    instructions       = models.TextField(blank=True)
    prescribing_doctor = models.CharField(max_length=200)
    start_date         = models.DateField()
    end_date           = models.DateField(blank=True, null=True)
    scheduled_time     = models.DateTimeField(default=timezone.now, db_index=True)
    ready              = models.BooleanField(default=False, db_index=True)
    last_notified      = models.DateTimeField(blank=True, null=True)
    container          = models.PositiveSmallIntegerField(
        choices=CONTAINER_CHOICES,
        blank=True,
        null=True,
    )

    def __str__(self):
        return f"{self.medication_name} ({self.user.username})"


class DoseTime(models.Model):
    """One scheduled dose time for a prescription. A prescription with doses_per_day=3 has 3 rows."""
    prescription = models.ForeignKey(Prescription, on_delete=models.CASCADE, related_name='dose_times')
    time_of_day  = models.TimeField()
    label        = models.CharField(max_length=50, blank=True, default='', help_text='e.g. Morning, Afternoon, Evening')

    class Meta:
        ordering = ['time_of_day']

    def __str__(self):
        return f"{self.prescription.medication_name} at {self.time_of_day}"


class PrescriptionLogging(models.Model):
    EVENT_TYPES = [
        ("REMINDER_SENT",  "Reminder Sent"),
        ("WARNING",        "Window Warning"),
        ("TAKEN",          "Taken"),
        ("MISSED",         "Missed"),
        ("REFILL",         "Refill"),
        ("DISPENSE_SENT",  "Dispense Sent to MCU"),
    ]

    prescription = models.ForeignKey(
        "Prescription",
        on_delete=models.CASCADE,
        related_name="logs",
    )
    user = models.ForeignKey(
        User,
        on_delete=models.CASCADE,
        related_name="prescription_logs",
    )

    event_type     = models.CharField(max_length=20, choices=EVENT_TYPES)
    scheduled_time = models.DateTimeField()
    event_time     = models.DateTimeField(auto_now_add=True)
    quantity       = models.PositiveIntegerField(null=True, blank=True, help_text='Pills involved in this event')

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
        related_name='error_logs',
    )

    def __str__(self):
        return f"[{self.timestamp}] {self.error_type} — {self.device_id}"


class FailedLoginLog(models.Model):
    username  = models.CharField(max_length=150)
    ip        = models.GenericIPAddressField(null=True, blank=True)
    timestamp = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"[{self.timestamp}] Failed login — {self.username} from {self.ip}"


class NotificationPreference(models.Model):
    user                         = models.OneToOneField(User, on_delete=models.CASCADE, related_name='notification_preferences')
    email_enabled                = models.BooleanField(default=True)
    desktop_notification_enabled = models.BooleanField(default=True)
    prescription_reminder_enabled = models.BooleanField(default=True)
    low_stock_alert_enabled      = models.BooleanField(default=True)
    missed_dose_alert_enabled    = models.BooleanField(default=True)
    device_error_alert_enabled   = models.BooleanField(default=True)
    push_enabled                 = models.BooleanField(default=True)

    def __str__(self):
        return f"NotificationPreference({self.user.username})"


class PushSubscription(models.Model):
    user       = models.ForeignKey(User, on_delete=models.CASCADE, related_name='push_subscriptions')
    endpoint   = models.TextField(unique=True)
    p256dh     = models.TextField()
    auth       = models.TextField()
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"PushSubscription({self.user.username})"


class RFIDCard(models.Model):
    uid         = models.CharField(max_length=64, unique=True, help_text='Physical RFID card UID (e.g. 87447d33)')
    assigned_to = models.OneToOneField(
        User,
        on_delete=models.SET_NULL,
        null=True,
        blank=True,
        related_name='rfid_card',
        help_text='User this card is assigned to (blank = available)',
    )

    class Meta:
        ordering = ['id']

    def __str__(self):
        return f"RFIDCard({self.uid})"


class MCUSession(models.Model):
    """One active user per MCU device, set via the MCU Login web page.

    device_id matches the uid/UID field the MCU sends in every request.
    When a new user logs in for a device, the old session for that device is
    replaced.  Multiple devices can each have a different active user.
    """
    user         = models.ForeignKey(User, on_delete=models.CASCADE, related_name='mcu_sessions')
    device_id    = models.CharField(max_length=64, unique=True, help_text="MCU's own uid/UID identifier")
    logged_in_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"MCUSession(device={self.device_id}, user={self.user.username})"

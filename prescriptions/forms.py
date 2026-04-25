from django import forms
from django.forms import modelformset_factory
from .models import Prescription, NotificationPreference, DoseTime, UserProfile, RFIDCard

class PrescriptionForm(forms.ModelForm):
    class Meta:
        model = Prescription
        exclude = ('user',)


class CompartmentEditForm(forms.ModelForm):
    """Full-model edit form used on the Rename/Edit Medication page."""

    rfid_uid = forms.ChoiceField(
        required=False,
        label='UID',
        help_text='RFID card UID for this patient',
        choices=[],   # populated at runtime in __init__
    )

    class Meta:
        model  = Prescription
        fields = [
            'user',
            'medication_name', 'dosage', 'dose_count', 'stock_count', 'add_stock',
            'doses_per_day', 'instructions', 'prescribing_doctor',
            'start_date', 'end_date', 'scheduled_time',
            'ready', 'container',
        ]
        widgets = {
            'start_date':     forms.DateInput(attrs={'type': 'date'}),
            'end_date':       forms.DateInput(attrs={'type': 'date'}),
            'scheduled_time': forms.DateTimeInput(
                attrs={'type': 'datetime-local'}, format='%Y-%m-%dT%H:%M'
            ),
            'instructions':   forms.Textarea(attrs={'rows': 3}),
        }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        # Build UID choices from the RFIDCard table (physical cards in the system).
        # Show unassigned cards first, then already-assigned ones so the admin can
        # see what's available at a glance.
        uid_choices = [('', '— none —')]
        for card in RFIDCard.objects.select_related('assigned_to').order_by('uid'):
            if card.assigned_to:
                label = f"{card.uid}  — assigned to {card.assigned_to.username}"
            else:
                label = f"{card.uid}  — available"
            uid_choices.append((card.uid, label))
        self.fields['rfid_uid'].choices = uid_choices

        # Pre-format the scheduled_time initial value for datetime-local
        if self.instance and self.instance.pk and self.instance.scheduled_time:
            from django.utils.timezone import localtime
            self.initial['scheduled_time'] = localtime(
                self.instance.scheduled_time
            ).strftime('%Y-%m-%dT%H:%M')

        # Pre-select the current patient's RFID UID
        if self.instance and self.instance.pk:
            try:
                self.initial['rfid_uid'] = self.instance.user.profile.rfid_uid
            except Exception:
                self.initial['rfid_uid'] = ''

    def clean_rfid_uid(self):
        rfid_uid = self.cleaned_data.get('rfid_uid', '').strip()
        if not rfid_uid:
            return rfid_uid
        # Reject if this UID is already assigned to a different user's profile
        qs = UserProfile.objects.filter(rfid_uid=rfid_uid)
        if self.instance and self.instance.pk and self.instance.user_id:
            qs = qs.exclude(user_id=self.instance.user_id)
        if qs.exists():
            taken_by = qs.first().user.username
            raise forms.ValidationError(
                f"This RFID card is already assigned to {taken_by}."
            )
        return rfid_uid

    def clean_add_stock(self):
        return self.cleaned_data.get('add_stock', 0)


class NotificationPreferenceForm(forms.ModelForm):
    class Meta:
        model  = NotificationPreference
        fields = [
            'email_enabled',
            'desktop_notification_enabled',
            'prescription_reminder_enabled',
            'low_stock_alert_enabled',
            'missed_dose_alert_enabled',
            'device_error_alert_enabled',
            'push_enabled',
        ]
        labels = {
            'email_enabled':                 'Email notifications',
            'desktop_notification_enabled':  'Desktop / browser notifications',
            'prescription_reminder_enabled': 'Prescription reminders',
            'low_stock_alert_enabled':       'Low stock alerts',
            'missed_dose_alert_enabled':     'Missed dose alerts',
            'device_error_alert_enabled':    'Device error alerts',
            'push_enabled':                  'Push notifications',
        }


class DoseTimeForm(forms.ModelForm):
    class Meta:
        model  = DoseTime
        fields = ['time_of_day', 'label']
        widgets = {
            'time_of_day': forms.TimeInput(attrs={'type': 'time'}),
            'label':       forms.TextInput(attrs={'placeholder': 'e.g. Morning'}),
        }
        labels = {
            'time_of_day': 'Time',
            'label':       'Label',
        }


DoseTimeFormSet = modelformset_factory(
    DoseTime,
    form=DoseTimeForm,
    extra=0,
    can_delete=True,
)

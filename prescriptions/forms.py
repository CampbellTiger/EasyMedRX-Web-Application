from django import forms
from django.forms import modelformset_factory
from .models import Prescription, NotificationPreference, DoseTime

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

        # Build UID choices from all UserProfiles that have an rfid_uid set
        from .models import UserProfile
        uid_choices = [('', '— none —')]
        for profile in UserProfile.objects.exclude(rfid_uid='').select_related('user'):
            label = f"{profile.rfid_uid}  ({profile.user.username})"
            uid_choices.append((profile.rfid_uid, label))
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

    def clean_stock_count(self):
        value = self.cleaned_data.get('stock_count', 0)
        if value > 30:
            raise forms.ValidationError("Stock cannot exceed 30 pills.")
        return value

    def clean_add_stock(self):
        add   = self.cleaned_data.get('add_stock', 0)
        stock = self.cleaned_data.get('stock_count') or (self.instance.stock_count if self.instance else 0)
        if stock + add > 30:
            allowed = max(0, 30 - stock)
            raise forms.ValidationError(
                f"Adding {add} would exceed the 30-pill limit. "
                f"Maximum you can add is {allowed}."
            )
        return add


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

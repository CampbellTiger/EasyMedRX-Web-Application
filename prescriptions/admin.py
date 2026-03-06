from django.contrib import admin
from .models import Prescription
from .models import PrescriptionLogging

@admin.register(Prescription)
class PrescriptionAdmin(admin.ModelAdmin):
    list_display = (
        'medication_name',
        'user',
        'dosage',
        'frequency',
        'prescribing_doctor',
        'start_date',
        'end_date',
    )
    search_fields = ('medication_name', 'user__username')
    list_filter = ('prescribing_doctor',)


@admin.register(PrescriptionLogging)
class PrescriptionLoggingAdmin(admin.ModelAdmin):
    list_display = ("user", "prescription", "event_type", "scheduled_time", "event_time")

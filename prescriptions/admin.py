from django.contrib import admin
from django.contrib.auth.admin import UserAdmin as BaseUserAdmin
from django.contrib.auth.models import User
from django.contrib.admin.models import LogEntry
from .models import Prescription, PrescriptionLogging, ErrorLog, FailedLoginLog, UserProfile, RFIDCard, Device, DoseTime


class UserProfileInline(admin.StackedInline):
    model = UserProfile
    can_delete = False
    verbose_name_plural = 'Profile'
    fields = ('phone', 'rfid_uid')


class UserAdmin(BaseUserAdmin):
    inlines = (UserProfileInline,)


admin.site.unregister(User)
admin.site.register(User, UserAdmin)


@admin.register(RFIDCard)
class RFIDCardAdmin(admin.ModelAdmin):
    list_display  = ('uid', 'assigned_to')
    search_fields = ('uid', 'assigned_to__username')
    autocomplete_fields = ('assigned_to',)


@admin.register(Device)
class DeviceAdmin(admin.ModelAdmin):
    list_display  = ('device_id',)
    search_fields = ('device_id',)
    filter_horizontal = ('patients',)


def make_clear_action(label):
    """Return an admin action that deletes all records of the registered model."""
    def clear_all(modeladmin, request, queryset):
        count, _ = modeladmin.model.objects.all().delete()
        modeladmin.message_user(request, f"Cleared {count} {label}.")
    clear_all.short_description = f"Clear ALL {label}"
    clear_all.__name__ = f"clear_all_{label.replace(' ', '_')}"
    return clear_all


class DoseTimeInline(admin.TabularInline):
    model                = DoseTime
    extra                = 0
    fields               = ('time_of_day', 'label')
    ordering             = ('time_of_day',)
    verbose_name         = 'Dose Time'
    verbose_name_plural  = 'Dose Schedule'
    can_delete           = True

    def get_extra(self, request, obj=None, **kwargs):
        if obj is None:
            return 1
        existing = obj.dose_times.count()
        return max(0, obj.doses_per_day - existing)


@admin.register(Prescription)
class PrescriptionAdmin(admin.ModelAdmin):
    list_display = (
        'medication_name',
        'user',
        'dosage',
        'doses_per_day',
        'prescribing_doctor',
        'start_date',
        'end_date',
    )
    search_fields = ('medication_name', 'user__username')
    list_filter   = ('prescribing_doctor',)
    inlines       = [DoseTimeInline]


@admin.register(DoseTime)
class DoseTimeAdmin(admin.ModelAdmin):
    list_display = ('prescription', 'time_of_day', 'label')
    list_filter  = ('prescription__user',)


@admin.register(PrescriptionLogging)
class PrescriptionLoggingAdmin(admin.ModelAdmin):
    list_display = ("user", "prescription", "event_type", "scheduled_time", "event_time")
    list_filter  = ("event_type",)
    actions      = [make_clear_action("prescription event logs")]


@admin.register(ErrorLog)
class ErrorLogAdmin(admin.ModelAdmin):
    list_display = ("timestamp", "user", "error_type", "device_id", "detail")
    list_filter  = ("error_type",)
    actions      = [make_clear_action("error logs")]


@admin.register(FailedLoginLog)
class FailedLoginLogAdmin(admin.ModelAdmin):
    list_display = ("timestamp", "username", "ip")
    actions      = [make_clear_action("failed login logs")]


@admin.register(LogEntry)
class LogEntryAdmin(admin.ModelAdmin):
    list_display = ("action_time", "user", "content_type", "object_repr", "action_flag", "change_message")
    list_filter  = ("action_flag", "content_type")
    search_fields = ("user__username", "object_repr", "change_message")
    actions      = [make_clear_action("admin audit logs")]

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

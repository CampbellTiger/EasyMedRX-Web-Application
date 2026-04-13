from django.conf import settings
from django.db import migrations, models
import django.db.models.deletion


def copy_patient_to_patients(apps, schema_editor):
    """Copy the existing FK patient_id into the new M2M patients table."""
    Device = apps.get_model('prescriptions', 'Device')
    for d in Device.objects.exclude(patient_id__isnull=True):
        d.patients.add(d.patient_id)


class Migration(migrations.Migration):

    dependencies = [
        ('prescriptions', '0016_prescription_users_m2m'),
        migrations.swappable_dependency(settings.AUTH_USER_MODEL),
    ]

    operations = [
        # ── Device: patient FK → patients M2M ────────────────────────────────
        migrations.AddField(
            model_name='device',
            name='patients',
            field=models.ManyToManyField(
                to=settings.AUTH_USER_MODEL,
                related_name='devices',
                blank=True,
            ),
        ),
        migrations.RunPython(copy_patient_to_patients, migrations.RunPython.noop),
        migrations.RemoveField(
            model_name='device',
            name='patient',
        ),

        # ── Prescription: add device FK ───────────────────────────────────────
        migrations.AddField(
            model_name='prescription',
            name='device',
            field=models.ForeignKey(
                to='prescriptions.Device',
                on_delete=django.db.models.deletion.SET_NULL,
                null=True,
                blank=True,
                related_name='prescriptions',
            ),
        ),
    ]

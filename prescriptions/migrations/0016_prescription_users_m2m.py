from django.conf import settings
from django.db import migrations, models


def copy_user_to_users(apps, schema_editor):
    """Copy the existing FK user_id into the new M2M users table."""
    Prescription = apps.get_model('prescriptions', 'Prescription')
    for p in Prescription.objects.exclude(user_id__isnull=True):
        p.users.add(p.user_id)


class Migration(migrations.Migration):

    dependencies = [
        ('prescriptions', '0015_prescription_container'),
        migrations.swappable_dependency(settings.AUTH_USER_MODEL),
    ]

    operations = [
        # 1. Add the new M2M field alongside the old FK (temp related_name to avoid clash).
        migrations.AddField(
            model_name='prescription',
            name='users',
            field=models.ManyToManyField(
                to=settings.AUTH_USER_MODEL,
                related_name='prescriptions_new',
                blank=True,
            ),
        ),

        # 2. Copy existing FK data into the M2M table.
        migrations.RunPython(copy_user_to_users, migrations.RunPython.noop),

        # 3. Remove the old FK.
        migrations.RemoveField(
            model_name='prescription',
            name='user',
        ),

        # 4. Fix the related_name now that the FK is gone.
        migrations.AlterField(
            model_name='prescription',
            name='users',
            field=models.ManyToManyField(
                to=settings.AUTH_USER_MODEL,
                related_name='prescriptions',
                blank=True,
            ),
        ),
    ]

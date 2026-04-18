from django.conf import settings
from django.db import migrations, models
import django.db.models.deletion


def copy_users_m2m_to_fk(apps, schema_editor):
    """Restore user_id FK from the M2M table (each prescription had exactly one user)."""
    db = schema_editor.connection.alias
    Prescription = apps.get_model('prescriptions', 'Prescription')
    for p in Prescription.objects.using(db).all():
        first_user = p.users.first()
        if first_user:
            p.user_id = first_user.pk
            p.save(using=db, update_fields=['user_id'])


class Migration(migrations.Migration):

    dependencies = [
        ('prescriptions', '0020_merge_0012_failedloginlog_0019_rfid_card_pool'),
        migrations.swappable_dependency(settings.AUTH_USER_MODEL),
    ]

    operations = [
        # 1. Add user FK as nullable so existing rows don't violate the constraint.
        migrations.AddField(
            model_name='prescription',
            name='user',
            field=models.ForeignKey(
                to=settings.AUTH_USER_MODEL,
                on_delete=django.db.models.deletion.CASCADE,
                related_name='prescriptions',
                null=True,
            ),
        ),

        # 2. Populate user_id from the M2M table.
        migrations.RunPython(copy_users_m2m_to_fk, migrations.RunPython.noop),

        # 3. Remove the M2M field.
        migrations.RemoveField(
            model_name='prescription',
            name='users',
        ),

        # 4. Make user non-nullable now that all rows have a value.
        migrations.AlterField(
            model_name='prescription',
            name='user',
            field=models.ForeignKey(
                to=settings.AUTH_USER_MODEL,
                on_delete=django.db.models.deletion.CASCADE,
                related_name='prescriptions',
            ),
        ),
    ]

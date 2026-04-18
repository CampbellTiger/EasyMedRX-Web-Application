import django.db.models.deletion
from django.conf import settings
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('prescriptions', '0011_add_userprofile_dose_stock'),
        migrations.swappable_dependency(settings.AUTH_USER_MODEL),
    ]

    operations = [
        migrations.CreateModel(
            name='NotificationPreference',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('email_enabled', models.BooleanField(default=True)),
                ('desktop_notification_enabled', models.BooleanField(default=True)),
                ('prescription_reminder_enabled', models.BooleanField(default=True)),
                ('low_stock_alert_enabled', models.BooleanField(default=True)),
                ('missed_dose_alert_enabled', models.BooleanField(default=True)),
                ('device_error_alert_enabled', models.BooleanField(default=True)),
                ('sms_enabled', models.BooleanField(default=False)),
                ('user', models.OneToOneField(
                    on_delete=django.db.models.deletion.CASCADE,
                    related_name='notification_preferences',
                    to=settings.AUTH_USER_MODEL,
                )),
            ],
        ),
    ]

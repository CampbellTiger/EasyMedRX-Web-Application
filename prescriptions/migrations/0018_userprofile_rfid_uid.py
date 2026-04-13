from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('prescriptions', '0017_device_multipatient'),
    ]

    operations = [
        migrations.AddField(
            model_name='userprofile',
            name='rfid_uid',
            field=models.CharField(
                blank=True,
                default='',
                max_length=64,
                help_text='RFID card UID for this patient (e.g. 87447d33)',
            ),
        ),
    ]

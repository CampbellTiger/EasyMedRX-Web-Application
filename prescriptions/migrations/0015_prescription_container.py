from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('prescriptions', '0014_remove_sms_enabled'),
    ]

    operations = [
        migrations.AddField(
            model_name='prescription',
            name='container',
            field=models.PositiveSmallIntegerField(
                blank=True,
                null=True,
                choices=[(1, 'Container 1'), (2, 'Container 2'), (3, 'Container 3'), (4, 'Container 4')],
            ),
        ),
    ]

from django.db import migrations


def create_superuser(apps, schema_editor):
    from django.contrib.auth.models import User
    if not User.objects.filter(username='admin').exists():
        User.objects.create_superuser(
            username='admin',
            email='',
            password='EasyMedRX',
        )


class Migration(migrations.Migration):

    dependencies = [
        ('prescriptions', '0030_window_minimum_5'),
    ]

    operations = [
        migrations.RunPython(create_superuser, migrations.RunPython.noop),
    ]

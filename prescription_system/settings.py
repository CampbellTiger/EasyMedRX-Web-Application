from pathlib import Path
import os
from dotenv import load_dotenv

load_dotenv(Path(__file__).resolve().parent.parent / '.env')

# Build paths inside the project like this: BASE_DIR / 'subdir'.
BASE_DIR = Path(__file__).resolve().parent.parent


# Quick-start development settings - unsuitable for production
# See https://docs.djangoproject.com/en/6.0/howto/deployment/checklist/

# SECURITY WARNING: keep the secret key used in production secret!
SECRET_KEY = os.environ.get('SECRET_KEY', 'django-insecure-changeme-run-generate_secrets-py')

# SECURITY WARNING: don't run with debug turned on in production!
DEBUG = True

#allows other computers to access the code
# from .ip_address import get_ip_address

# ALLOWED_HOSTS = ["127.0.0.1",get_ip_address(), 'localhost']
# CSRF_TRUSTED_ORIGINS = ['http://127.0.0.1:8000', f"http://{get_ip_address()}"]

# sys.stdout.write(f"http://{get_ip_address()}\n") #get ip address from command line

import socket as _socket

def _get_lan_ip():
    try:
        s = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return '127.0.0.1'

LOCAL_IP = _get_lan_ip()

# Set by startup.py at launch; empty string when running outside startup.py
WINDOWS_HOST_IP = os.environ.get('DJANGO_WINDOWS_IP', '')

_extra_hosts = [h for h in [WINDOWS_HOST_IP] if h]

ALLOWED_HOSTS = ['*']  # dev only — MCU Host header varies by network
CSRF_TRUSTED_ORIGINS = [
    'http://127.0.0.1:8000',
    f'http://{LOCAL_IP}:8000',
    'http://easymedrx.local:8000',
    'https://127.0.0.1:8443',
    f'https://{LOCAL_IP}:8443',
    'https://easymedrx.local:8443',
] + [f'http://{h}:8000' for h in _extra_hosts] + [f'https://{h}:8443' for h in _extra_hosts]

# Application definition

#Celery
CELERY_BROKER_URL = 'redis://127.0.0.1:6379/0'
CELERY_RESULT_BACKEND = 'redis://127.0.0.1:6379/0'
CELERY_ACCEPT_CONTENT = ['json']
CELERY_TASK_SERIALIZER = 'json'
CELERY_RESULT_SERIALIZER = 'json'

INSTALLED_APPS = [
    'django.contrib.admin',
    'django.contrib.auth',
    'django.contrib.contenttypes',
    'django.contrib.sessions',
    'django.contrib.messages',
    'django.contrib.staticfiles',
    'prescriptions',
    'rest_framework',
    'channels',
]


ASGI_APPLICATION = "prescription_system.asgi.application"

# CHANNEL_LAYERS = {
#     "default": {
#         "BACKEND": "channels.layers.InMemoryChannelLayer",
#     },
# }
#For production, switch to Redis channel layer:
CHANNEL_LAYERS = {
    "default": {
        "BACKEND": "channels_redis.core.RedisChannelLayer",
        "CONFIG": {
            "hosts": [("127.0.0.1", 6379)],
        },
    },
}


MIDDLEWARE = [
    'django.middleware.security.SecurityMiddleware',
    'whitenoise.middleware.WhiteNoiseMiddleware',
    'django.contrib.sessions.middleware.SessionMiddleware',
    'django.middleware.common.CommonMiddleware',
    'django.middleware.csrf.CsrfViewMiddleware',
    'django.contrib.auth.middleware.AuthenticationMiddleware',
    'django.contrib.messages.middleware.MessageMiddleware',
    'django.middleware.clickjacking.XFrameOptionsMiddleware',
]

ROOT_URLCONF = 'prescription_system.urls'
APPEND_SLASH = False

TEMPLATES = [
    {
        'BACKEND': 'django.template.backends.django.DjangoTemplates',
        'DIRS': [BASE_DIR / 'templates'],
        'APP_DIRS': True,
        'OPTIONS': {
            'context_processors': [
                'django.template.context_processors.debug',
                'django.template.context_processors.request',
                'django.contrib.auth.context_processors.auth',
                'django.contrib.messages.context_processors.messages',
            ],
        },
    },
]


WSGI_APPLICATION = 'prescription_system.wsgi.application'


# Database
# https://docs.djangoproject.com/en/6.0/ref/settings/#databases

DATABASES = {
    'default': {
        'ENGINE': 'django.db.backends.sqlite3',
        'NAME': BASE_DIR / 'db.sqlite3',
    }
}

# Public base URL used in outgoing emails
APP_BASE_URL = os.environ.get('APP_BASE_URL', f'http://{LOCAL_IP}:8000')

# Email — configured via .env
EMAIL_BACKEND    = 'django.core.mail.backends.smtp.EmailBackend'
EMAIL_HOST       = os.environ.get('EMAIL_HOST', 'smtp.gmail.com')
EMAIL_PORT       = int(os.environ.get('EMAIL_PORT', 587))
EMAIL_USE_TLS    = os.environ.get('EMAIL_USE_TLS', 'True') == 'True'
EMAIL_HOST_USER  = os.environ.get('EMAIL_HOST_USER', '')
EMAIL_HOST_PASSWORD = os.environ.get('EMAIL_HOST_PASSWORD', '')
DEFAULT_FROM_EMAIL  = os.environ.get('DEFAULT_FROM_EMAIL', EMAIL_HOST_USER)

# Password validation
# https://docs.djangoproject.com/en/6.0/ref/settings/#auth-password-validators

AUTH_PASSWORD_VALIDATORS = [
    {
        'NAME': 'django.contrib.auth.password_validation.UserAttributeSimilarityValidator',
    },
    {
        'NAME': 'django.contrib.auth.password_validation.MinimumLengthValidator',
    },
    {
        'NAME': 'django.contrib.auth.password_validation.CommonPasswordValidator',
    },
    {
        'NAME': 'django.contrib.auth.password_validation.NumericPasswordValidator',
    },
]


# Internationalization
# https://docs.djangoproject.com/en/6.0/topics/i18n/

LANGUAGE_CODE = 'en-us'

# Set to the timezone where the server/patients are located.
# Full list: https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
TIME_ZONE = 'America/Chicago'

USE_I18N = True

USE_TZ = True


# Static files (CSS, JavaScript, Images)
# https://docs.djangoproject.com/en/6.0/howto/static-files/

STATIC_URL = 'static/'
STATIC_ROOT = BASE_DIR / 'staticfiles'
STATICFILES_STORAGE = "whitenoise.storage.CompressedManifestStaticFilesStorage"


LOGIN_URL = '/login/'
LOGIN_REDIRECT_URL = '/'
LOGOUT_REDIRECT_URL = '/login/'

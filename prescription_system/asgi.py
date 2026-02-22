import os

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'prescription_system.settings')

from django.core.asgi import get_asgi_application

# This initializes Django BEFORE routing is imported
django_asgi_app = get_asgi_application()

from channels.routing import ProtocolTypeRouter, URLRouter
from channels.auth import AuthMiddlewareStack
import prescriptions.routing

application = ProtocolTypeRouter({
    "http": django_asgi_app,
    "websocket": AuthMiddlewareStack(
        URLRouter(
            prescriptions.routing.websocket_urlpatterns
        )
    ),
})
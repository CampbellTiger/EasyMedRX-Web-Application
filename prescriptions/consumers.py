import json
from channels.generic.websocket import AsyncWebsocketConsumer
from channels.layers import get_channel_layer
from asgiref.sync import async_to_sync


class NotificationConsumer(AsyncWebsocketConsumer):
    async def connect(self):
        # Global group for broadcast messages
        self.group_name = "notifications"
        await self.channel_layer.group_add(self.group_name, self.channel_name)

        # User-specific group for targeted notifications
        user = self.scope.get('user')
        if user and user.is_authenticated:
            self.user_group = f"user_{user.id}"
            await self.channel_layer.group_add(self.user_group, self.channel_name)
        else:
            self.user_group = None

        await self.accept()
        print(f"[WS CONNECT] {self.channel_name} joined {self.group_name}"
              + (f" and {self.user_group}" if self.user_group else ""))

    async def disconnect(self, event):
        await self.channel_layer.group_discard(self.group_name, self.channel_name)
        if self.user_group:
            await self.channel_layer.group_discard(self.user_group, self.channel_name)
        print(f"[WS DISCONNECT] {self.channel_name} left {self.group_name}")

    async def send_notification(self, event):
        message = event['message']
        await self.send(text_data=json.dumps({"message": message}))
        print(f"[SEND NOTIFICATION] {message}")


def notify_user(user_id, message):
    """
    Send a WebSocket notification to a specific user's group and the global
    notifications group (so any logged-in staff member also receives it).
    Safe to call from synchronous Django views and Celery tasks.
    """
    channel_layer = get_channel_layer()
    if not channel_layer:
        print("[NOTIFY USER] Channel layer not available.")
        return
    payload = {"type": "send_notification", "message": message}
    async_to_sync(channel_layer.group_send)(f"user_{user_id}", payload)
    async_to_sync(channel_layer.group_send)("notifications", payload)

import asyncio
import json
from channels.generic.websocket import AsyncWebsocketConsumer
from channels.layers import get_channel_layer
from django.utils import timezone
from asgiref.sync import sync_to_async, async_to_sync
from .models import Prescription


class NotificationConsumer(AsyncWebsocketConsumer):
    async def connect(self):
        # All clients join the global "notifications" group
        self.group_name = "notifications"
        await self.channel_layer.group_add(self.group_name, self.channel_name)

        # Also join a user-specific group for targeted notifications
        user = self.scope.get('user')
        if user and user.is_authenticated:
            self.user_group = f"user_{user.id}"
            await self.channel_layer.group_add(self.user_group, self.channel_name)
        else:
            self.user_group = None

        await self.accept()
        print(f"[WS CONNECT] {self.channel_name} joined {self.group_name}"
              + (f" and {self.user_group}" if self.user_group else ""))

        # Start background loop only once
        if not hasattr(self.channel_layer, "notification_loop_started"):
            self.channel_layer.notification_loop_started = True
            asyncio.create_task(self.background_loop())

    async def disconnect(self, event):
        await self.channel_layer.group_discard(self.group_name, self.channel_name)
        if self.user_group:
            await self.channel_layer.group_discard(self.user_group, self.channel_name)
        print(f"[WS DISCONNECT] {self.channel_name} left {self.group_name}")

    async def send_notification(self, event):
        # Called when a message is sent to a group this consumer belongs to
        message = event['message']
        await self.send(text_data=json.dumps({"message": message}))
        print(f"[SEND NOTIFICATION] {message}")

    async def background_loop(self):
        """
        Runs every minute:
        - Sends notifications for prescriptions due at the current time
        - Resets ready flags at the start of a new day
        """
        print("[NOTIFICATION LOOP] Started")
        last_reset_date = timezone.localtime().date()

        while True:
            now = timezone.localtime()

            # Daily reset at midnight
            if now.date() > last_reset_date:
                await sync_to_async(self.reset_ready_flags)()
                last_reset_date = now.date()

            # Check for due prescriptions
            due_prescriptions = await sync_to_async(list)(
                Prescription.objects.filter(
                    scheduled_time__hour=now.hour,
                    scheduled_time__minute=now.minute,
                    ready=False
                )
            )

            for p in due_prescriptions:
                message = f"Time to take {p.medication_name}!"
                await self.channel_layer.group_send(
                    "notifications",
                    {
                        "type": "send_notification",
                        "message": message
                    }
                )
                await sync_to_async(self.mark_ready)(p)

            # Sleep until next minute
            await asyncio.sleep(60)

    def mark_ready(self, prescription):
        prescription.ready = True
        prescription.save()
        print(f"[MARK READY] {prescription.medication_name} marked as ready.")

    def reset_ready_flags(self):
        Prescription.objects.update(ready=False)
        print("[RESET READY FLAGS] All prescriptions reset for new day.")


def notify_user(user_id, message):
    """
    Send a WebSocket notification to a specific user's group.
    Safe to call from synchronous Django views.
    """
    channel_layer = get_channel_layer()
    if not channel_layer:
        print("[NOTIFY USER] Channel layer not available.")
        return
    async_to_sync(channel_layer.group_send)(
        f"user_{user_id}",
        {
            "type": "send_notification",
            "message": message,
        }
    )

import asyncio
import json
from channels.generic.websocket import AsyncWebsocketConsumer
from django.utils import timezone
from asgiref.sync import sync_to_async
from .models import Prescription

class NotificationConsumer(AsyncWebsocketConsumer):
    async def connect(self):
        # All clients join the global "notifications" group
        self.group_name = "notifications"
        await self.channel_layer.group_add(self.group_name, self.channel_name)
        await self.accept()
        print(f"[WS CONNECT] {self.channel_name} joined {self.group_name}")

        # Start background loop only once
        if not hasattr(self.channel_layer, "notification_loop_started"):
            self.channel_layer.notification_loop_started = True
            asyncio.create_task(self.background_loop())

    async def disconnect(self, event):
        await self.channel_layer.group_discard(self.group_name, self.channel_name)
        print(f"[WS DISCONNECT] {self.channel_name} left {self.group_name}")

    async def send_notification(self, event):
        # Called when a message is sent to the "notifications" group
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
                # Send to all connected clients
                await self.channel_layer.group_send(
                    "notifications",
                    {
                        "type": "send_notification",
                        "message": message
                    }
                )
                # Mark prescription ready so it won’t trigger again this minute
                await sync_to_async(self.mark_ready)(p)

            # Sleep until next minute
            await asyncio.sleep(60)

    def mark_ready(self, prescription):
        prescription.ready = True
        prescription.save()
        print(f"[MARK READY] {prescription.medication_name} marked as ready.")

    def reset_ready_flags(self):
        # Reset all prescriptions to ready=False for the new day
        Prescription.objects.update(ready=False)
        print("[RESET READY FLAGS] All prescriptions reset for new day.")
// EasyMedRX Service Worker — handles background push notifications
// Served at /sw.js so its scope covers the entire site (/).

'use strict';

// ── Push received ─────────────────────────────────────────────────────────────
self.addEventListener('push', function (event) {
    let payload = { title: 'EasyMedRX', body: 'You have a new notification.' };

    if (event.data) {
        try {
            payload = event.data.json();
        } catch (e) {
            payload.body = event.data.text();
        }
    }

    const options = {
        body: payload.body || payload.message || '',
        icon: '/static/prescriptions/icon-192.png',
        badge: '/static/prescriptions/icon-192.png',
        vibrate: [200, 100, 200],
        data: { url: payload.url || '/' },
        // Collapse duplicate notifications for the same prescription
        tag: payload.tag || 'easymedrx-notification',
        renotify: true,
    };

    event.waitUntil(
        self.registration.showNotification(payload.title || 'EasyMedRX', options)
    );
});

// ── Notification clicked ──────────────────────────────────────────────────────
self.addEventListener('notificationclick', function (event) {
    event.notification.close();
    const targetUrl = event.notification.data && event.notification.data.url
        ? event.notification.data.url
        : '/';

    event.waitUntil(
        clients.matchAll({ type: 'window', includeUncontrolled: true }).then(function (clientList) {
            // If the app is already open, focus it
            for (const client of clientList) {
                if (client.url.includes(self.location.origin) && 'focus' in client) {
                    return client.focus();
                }
            }
            // Otherwise open a new window
            if (clients.openWindow) {
                return clients.openWindow(targetUrl);
            }
        })
    );
});

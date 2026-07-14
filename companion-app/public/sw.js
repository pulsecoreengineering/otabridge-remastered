// Minimal service worker — exists only to receive Push API events; no asset
// caching/offline support here, that's a separate (larger) PWA-installability
// feature not in scope for this pass.

self.addEventListener("push", (event) => {
  let data = { title: "OTABridge", body: "" };
  try {
    data = event.data.json();
  } catch {
    // ignore — fall back to the default above
  }
  event.waitUntil(
    self.registration.showNotification(data.title, {
      body: data.body,
      icon: undefined,
      tag: "otabridge-flash", // collapses rapid repeat notifications instead of stacking
    }),
  );
});

self.addEventListener("notificationclick", (event) => {
  event.notification.close();
  event.waitUntil(
    self.clients.matchAll({ type: "window" }).then((clients) => {
      for (const client of clients) {
        if ("focus" in client) return client.focus();
      }
      if (self.clients.openWindow) return self.clients.openWindow("/");
    }),
  );
});

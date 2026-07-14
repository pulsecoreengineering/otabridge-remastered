// Minimal service worker — exists only to receive Push API events; no asset
// caching/offline support here, that's a separate (larger) PWA-installability
// feature not in scope for this pass.

// Activate immediately rather than waiting for old tabs to close — relevant
// if a previous/broken worker registration is still hanging around from
// earlier testing.
self.addEventListener("install", () => {
  self.skipWaiting();
});
self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener("push", (event) => {
  console.log("[sw] push event received", event.data ? "(has data)" : "(no data)");

  let data = { title: "OTABridge", body: "" };
  try {
    data = event.data.json();
  } catch (e) {
    console.warn("[sw] push payload wasn't JSON, using default", e);
  }

  event.waitUntil(
    self.registration
      .showNotification(data.title, {
        body: data.body,
        tag: "otabridge-flash", // collapses rapid repeat notifications instead of stacking
      })
      .then(() => console.log("[sw] showNotification resolved"))
      .catch((e) => console.error("[sw] showNotification failed", e)),
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

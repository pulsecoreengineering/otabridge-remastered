import { relayApi } from "./api/client";

export function pushSupported(): boolean {
  return "serviceWorker" in navigator && "PushManager" in window;
}

// PushManager.subscribe wants the VAPID public key as a raw Uint8Array, but
// the relay hands it out base64url-encoded (the format VAPID keys are
// generated/exchanged in) — this is the standard conversion boilerplate.
function urlBase64ToUint8Array(base64Url: string): Uint8Array<ArrayBuffer> {
  const padding = "=".repeat((4 - (base64Url.length % 4)) % 4);
  const base64 = (base64Url + padding).replace(/-/g, "+").replace(/_/g, "/");
  const raw = atob(base64);
  // Uint8Array.from()'s inferred type doesn't satisfy PushManager.subscribe's
  // BufferSource requirement under current lib.dom types (ArrayBufferLike vs
  // concrete ArrayBuffer) — a plain length-constructed array does.
  const bytes = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
  return bytes;
}

export async function enablePushNotifications(): Promise<void> {
  if (!pushSupported()) throw new Error("Push notifications aren't supported in this browser");

  const permission = await Notification.requestPermission();
  if (permission !== "granted") throw new Error("Notification permission denied");

  const registration = await navigator.serviceWorker.register("/sw.js");
  await navigator.serviceWorker.ready;

  const { publicKey } = await relayApi.getPushPublicKey();
  const subscription = await registration.pushManager.subscribe({
    userVisibleOnly: true,
    applicationServerKey: urlBase64ToUint8Array(publicKey),
  });

  await relayApi.subscribePush(subscription.toJSON());
}

export async function disablePushNotifications(): Promise<void> {
  if (!pushSupported()) return;
  const registration = await navigator.serviceWorker.getRegistration();
  const subscription = await registration?.pushManager.getSubscription();
  if (!subscription) return;

  await relayApi.unsubscribePush(subscription.endpoint).catch(() => {});
  await subscription.unsubscribe();
}

export async function isPushEnabled(): Promise<boolean> {
  if (!pushSupported()) return false;
  const registration = await navigator.serviceWorker.getRegistration();
  const subscription = await registration?.pushManager.getSubscription();
  return !!subscription;
}

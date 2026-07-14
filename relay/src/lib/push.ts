import webpush from "web-push";
import { env } from "../env.js";
import { prisma } from "../db.js";

webpush.setVapidDetails(env.VAPID_SUBJECT, env.VAPID_PUBLIC_KEY, env.VAPID_PRIVATE_KEY);

export interface PushPayload {
  title: string;
  body: string;
}

// Notifies every account that owns this device — deliberately not scoped to
// "only if nobody has it open right now": push is for the case where the
// app is backgrounded/closed, and there's no reliable way for the relay to
// know that, so this fires unconditionally on a flash finishing. A user
// watching the live status in the app gets a redundant OS notification;
// that's an acceptable v1 trade for actually working when they've walked
// away from the bench, which is the point of the feature.
export async function notifyDeviceOwners(deviceId: string, payload: PushPayload): Promise<void> {
  const links = await prisma.accountDevice.findMany({
    where: { deviceId },
    select: { accountId: true },
  });
  if (links.length === 0) return;

  const subscriptions = await prisma.pushSubscription.findMany({
    where: { accountId: { in: links.map((l) => l.accountId) } },
  });

  await Promise.all(subscriptions.map((sub) => sendOne(sub, payload)));
}

async function sendOne(
  sub: { endpoint: string; p256dh: string; auth: string },
  payload: PushPayload,
): Promise<void> {
  try {
    await webpush.sendNotification(
      { endpoint: sub.endpoint, keys: { p256dh: sub.p256dh, auth: sub.auth } },
      JSON.stringify(payload),
    );
  } catch (err) {
    const statusCode = (err as { statusCode?: number }).statusCode;
    if (statusCode === 404 || statusCode === 410) {
      // Subscription is gone (browser data cleared, uninstalled, etc.) —
      // clean it up rather than retrying it forever.
      await prisma.pushSubscription.delete({ where: { endpoint: sub.endpoint } }).catch(() => {});
    }
    // Other failures (network blip, push service hiccup) are logged by
    // Fastify's own error handling upstream where this is awaited from;
    // not worth crashing a status broadcast over a single failed push.
  }
}

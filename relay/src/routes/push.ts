import type { FastifyInstance, FastifyRequest, FastifyReply } from "fastify";
import { z } from "zod";
import { prisma } from "../db.js";
import { verifyAccountToken } from "../auth.js";
import { env } from "../env.js";
import { notifyAccount } from "../lib/push.js";

async function requireAccount(req: FastifyRequest, reply: FastifyReply): Promise<string | null> {
  const header = req.headers.authorization;
  if (!header?.startsWith("Bearer ")) {
    reply.code(401).send({ error: "missing_token" });
    return null;
  }
  try {
    return verifyAccountToken(header.slice(7));
  } catch {
    reply.code(401).send({ error: "invalid_token" });
    return null;
  }
}

const subscribeSchema = z.object({
  endpoint: z.string().url(),
  keys: z.object({
    p256dh: z.string(),
    auth: z.string(),
  }),
});

const unsubscribeSchema = z.object({
  endpoint: z.string().url(),
});

export async function pushRoutes(app: FastifyInstance): Promise<void> {
  // Public — the app needs this before it's authenticated with anything
  // push-specific, it's just the key used to create the browser subscription.
  app.get("/push/public-key", async () => ({ publicKey: env.VAPID_PUBLIC_KEY }));

  app.post("/push/subscribe", async (req, reply) => {
    const accountId = await requireAccount(req, reply);
    if (!accountId) return;

    const { endpoint, keys } = subscribeSchema.parse(req.body);
    // Upsert on endpoint — re-subscribing (e.g. after clearing site data,
    // or the push service rotating the endpoint) just replaces the row
    // rather than accumulating stale duplicates.
    await prisma.pushSubscription.upsert({
      where: { endpoint },
      update: { accountId, p256dh: keys.p256dh, auth: keys.auth },
      create: { endpoint, accountId, p256dh: keys.p256dh, auth: keys.auth },
    });

    return { subscribed: true };
  });

  app.delete("/push/subscribe", async (req, reply) => {
    const accountId = await requireAccount(req, reply);
    if (!accountId) return;

    const { endpoint } = unsubscribeSchema.parse(req.body);
    // Scoped to this account's own subscription — deleting by endpoint
    // alone would let any authenticated account remove someone else's.
    await prisma.pushSubscription.deleteMany({ where: { endpoint, accountId } });

    return { unsubscribed: true };
  });

  // Manual test trigger — lets you verify subscribe -> relay -> push
  // service -> browser end-to-end without running a real flash to
  // completion. Not wired into any UI beyond a debug button; harmless to
  // leave in production, it only ever notifies the caller's own account.
  app.post("/push/test", async (req, reply) => {
    const accountId = await requireAccount(req, reply);
    if (!accountId) return;

    const count = await notifyAccount(accountId, {
      title: "OTABridge test notification",
      body: "If you can see this, push is wired up correctly.",
    });

    return { sent: count };
  });
}

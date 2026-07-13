import type { FastifyInstance, FastifyRequest, FastifyReply } from "fastify";
import { z } from "zod";
import { prisma } from "../db.js";
import { verifyAccountToken } from "../auth.js";
import { generateClaimCode, CLAIM_CODE_TTL_MS } from "../lib/claimCode.js";
import { isDeviceOnline, deviceSockets } from "../ws/registry.js";

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

const registerSchema = z.object({
  deviceId: z.string().min(4).max(64),
  publicKey: z.string().length(64), // 32-byte Ed25519 public key, hex-encoded
  name: z.string().min(1).max(32),
});

const claimSchema = z.object({
  code: z.string().min(8),
});

export async function deviceRoutes(app: FastifyInstance): Promise<void> {
  // Called by the device itself over outbound HTTPS, once it has Wi-Fi.
  // Not account-authenticated — the device doesn't know its owner yet, that's
  // exactly what the claim code is for.
  app.post("/devices/register", async (req) => {
    const { deviceId, publicKey, name } = registerSchema.parse(req.body);

    const device = await prisma.device.upsert({
      where: { id: deviceId },
      update: { publicKey, name },
      create: { id: deviceId, publicKey, name },
    });

    if (device.claimedAt) {
      // Already claimed on a previous provision — no new code needed, the
      // device just authenticates over /ws/device from here.
      return { claimed: true };
    }

    const code = generateClaimCode();
    await prisma.claimCode.create({
      data: {
        code,
        deviceId: device.id,
        expiresAt: new Date(Date.now() + CLAIM_CODE_TTL_MS),
      },
    });

    return { claimed: false, code, expiresInSeconds: CLAIM_CODE_TTL_MS / 1000 };
  });

  app.post("/devices/claim", async (req, reply) => {
    const accountId = await requireAccount(req, reply);
    if (!accountId) return;

    const { code } = claimSchema.parse(req.body);
    const claimCode = await prisma.claimCode.findUnique({ where: { code } });

    if (!claimCode || claimCode.usedAt || claimCode.expiresAt < new Date()) {
      return reply.code(410).send({ error: "invalid_or_expired_code" });
    }

    await prisma.$transaction([
      prisma.claimCode.update({ where: { code }, data: { usedAt: new Date() } }),
      prisma.device.update({ where: { id: claimCode.deviceId }, data: { claimedAt: new Date() } }),
      prisma.accountDevice.create({ data: { accountId, deviceId: claimCode.deviceId } }),
    ]);

    // If the device is sitting on an open /ws/device connection right now
    // (the common case — it was just powered on to get this code), push it
    // the claim immediately rather than making it wait for a reboot to find
    // out via a fresh /devices/register call.
    const socket = deviceSockets.get(claimCode.deviceId);
    if (socket) {
      socket.send(JSON.stringify({ type: "claimed" }));
    }

    return { deviceId: claimCode.deviceId };
  });

  app.get("/devices", async (req, reply) => {
    const accountId = await requireAccount(req, reply);
    if (!accountId) return;

    const links = await prisma.accountDevice.findMany({
      where: { accountId },
      include: { device: true },
    });

    return links.map(({ device }) => ({
      id: device.id,
      name: device.name,
      claimedAt: device.claimedAt,
      online: isDeviceOnline(device.id),
    }));
  });
}

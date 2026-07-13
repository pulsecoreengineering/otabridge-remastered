import type { FastifyInstance } from "fastify";
import type { WebSocket } from "ws";
import { prisma } from "../db.js";
import { generateChallenge, verifySignature } from "../lib/ed25519.js";
import { deviceSockets } from "./registry.js";

// Handshake: server sends { type: "challenge", nonce } on connect; the device
// replies { type: "auth", deviceId, signature } where signature is the nonce
// signed with its Ed25519 private key. Verified against the public key it
// registered over /devices/register — nothing secret ever crosses the wire.
//
// Command/data-plane relay (flash progress, debug stream) is a follow-up
// phase; this only establishes and tracks authenticated presence for now.
export async function deviceSocketRoute(app: FastifyInstance): Promise<void> {
  app.get("/ws/device", { websocket: true }, (socket: WebSocket) => {
    const nonce = generateChallenge();
    let authenticated = false;
    let deviceId: string | null = null;

    socket.send(JSON.stringify({ type: "challenge", nonce }));

    const authTimeout = setTimeout(() => {
      if (!authenticated) socket.close(4001, "auth_timeout");
    }, 10_000);

    socket.on("message", async (raw) => {
      if (authenticated) return;
      try {
        const msg = JSON.parse(raw.toString());
        if (msg.type !== "auth") return;

        const device = await prisma.device.findUnique({ where: { id: msg.deviceId } });
        if (!device) return socket.close(4004, "unknown_device");

        const ok = await verifySignature(device.publicKey, nonce, msg.signature);
        if (!ok) return socket.close(4003, "bad_signature");

        authenticated = true;
        deviceId = device.id;
        clearTimeout(authTimeout);
        deviceSockets.set(device.id, socket);
        await prisma.device.update({ where: { id: device.id }, data: { lastSeenAt: new Date() } });
        socket.send(JSON.stringify({ type: "authenticated" }));
      } catch {
        socket.close(4000, "bad_message");
      }
    });

    socket.on("close", () => {
      clearTimeout(authTimeout);
      if (deviceId && deviceSockets.get(deviceId) === socket) {
        deviceSockets.delete(deviceId);
      }
    });
  });
}

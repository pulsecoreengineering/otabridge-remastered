import type { FastifyInstance } from "fastify";
import type { WebSocket } from "ws";
import { prisma } from "../db.js";
import { generateChallenge, verifySignature } from "../lib/ed25519.js";
import { deviceSockets, broadcastToDeviceSubscribers } from "./registry.js";

// Handshake: server sends { type: "challenge", nonce } on connect; the device
// replies { type: "auth", deviceId, signature } where signature is the nonce
// signed with its Ed25519 private key. Verified against the public key it
// registered over /devices/register — nothing secret ever crosses the wire.
//
// Once authenticated, any message the device sends (status/progress/
// debug_line/cmd_result) is forwarded to whichever app sockets are
// subscribed to it, with `deviceId` stamped on before forwarding — a single
// app session can subscribe to several devices (fleet/bench operations) over
// one socket, and without deviceId on each push there'd be no way to tell
// which device a given status/progress/debug_line came from. The relay
// still doesn't interpret the rest of the payload, just adds this one field.
// Commands flow the other way via appSocket.ts writing directly to this
// device's entry in deviceSockets.
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
      if (!authenticated) {
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
        return;
      }

      // Post-auth: data-plane push from the device — stamp deviceId on and
      // forward to whichever app sessions are subscribed to it.
      let payload = raw.toString();
      try {
        const parsed = JSON.parse(payload);
        parsed.deviceId = deviceId;
        payload = JSON.stringify(parsed);
      } catch {
        // Not JSON — forward unchanged rather than drop it; shouldn't happen
        // given firmware always sends JSON, but this is a thin pipe, not a
        // strict validator.
      }
      broadcastToDeviceSubscribers(deviceId!, payload);
    });

    socket.on("close", () => {
      clearTimeout(authTimeout);
      if (deviceId && deviceSockets.get(deviceId) === socket) {
        deviceSockets.delete(deviceId);
      }
    });
  });
}

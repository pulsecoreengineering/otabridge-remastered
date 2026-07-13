import type { FastifyInstance } from "fastify";
import type { WebSocket } from "ws";
import { verifyAccountToken } from "../auth.js";
import { prisma } from "../db.js";
import { appSockets, deviceSockets, subscribeAppToDevice, unsubscribeAppSocket } from "./registry.js";

async function ownsDevice(accountId: string, deviceId: string): Promise<boolean> {
  const link = await prisma.accountDevice.findUnique({
    where: { accountId_deviceId: { accountId, deviceId } },
  });
  return !!link;
}

// Companion app connects with ?token=<account JWT>. Two message types once
// connected:
//   {"type":"subscribe","deviceId":"..."} — start receiving that device's
//     status/progress/debug_line/cmd_result pushes (ownership-checked once,
//     here, not re-checked per push).
//   {"type":"cmd","deviceId":"...","action":"...", ...} — forwarded verbatim
//     (minus deviceId) to the device's /ws/device socket if the app owns it
//     and the device is online. The relay doesn't interpret `action` — the
//     device decides what it recognizes.
export async function appSocketRoute(app: FastifyInstance): Promise<void> {
  app.get("/ws/app", { websocket: true }, (socket: WebSocket, req) => {
    const token = (req.query as { token?: string }).token;

    let accountId: string;
    try {
      accountId = verifyAccountToken(token ?? "");
    } catch {
      socket.close(4001, "invalid_token");
      return;
    }

    if (!appSockets.has(accountId)) appSockets.set(accountId, new Set());
    appSockets.get(accountId)!.add(socket);

    socket.on("message", async (raw) => {
      let msg: any;
      try {
        msg = JSON.parse(raw.toString());
      } catch {
        return;
      }
      if (typeof msg.deviceId !== "string") return;

      if (msg.type === "subscribe") {
        if (!(await ownsDevice(accountId, msg.deviceId))) {
          socket.send(JSON.stringify({ type: "error", message: "not_owned" }));
          return;
        }
        subscribeAppToDevice(socket, msg.deviceId);
        socket.send(JSON.stringify({ type: "subscribed", deviceId: msg.deviceId }));
        return;
      }

      if (msg.type === "cmd" && typeof msg.action === "string") {
        if (!(await ownsDevice(accountId, msg.deviceId))) {
          socket.send(JSON.stringify({ type: "error", message: "not_owned" }));
          return;
        }
        const deviceSocket = deviceSockets.get(msg.deviceId);
        if (!deviceSocket) {
          socket.send(JSON.stringify({
            type: "error", message: "device_offline", requestId: msg.requestId ?? null,
          }));
          return;
        }
        const { deviceId, ...forwarded } = msg;
        deviceSocket.send(JSON.stringify(forwarded));
      }
    });

    socket.on("close", () => {
      appSockets.get(accountId)?.delete(socket);
      unsubscribeAppSocket(socket);
    });
  });
}

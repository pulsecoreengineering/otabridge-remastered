import type { FastifyInstance } from "fastify";
import type { WebSocket } from "ws";
import { verifyAccountToken } from "../auth.js";
import { appSockets } from "./registry.js";

// Companion app connects with ?token=<account JWT>. Kept as a presence/session
// channel for now — routing a specific deviceId's commands and streams to this
// socket is the next phase, once the device-side data plane exists too.
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

    socket.on("close", () => {
      appSockets.get(accountId)?.delete(socket);
    });
  });
}

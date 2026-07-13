import type { WebSocket } from "ws";

// In-memory presence registry. Fine for a single relay instance; a multi-instance
// deployment would need a shared layer (e.g. Redis pub/sub) to route across
// processes — not needed until the relay actually scales beyond one container.
export const deviceSockets = new Map<string, WebSocket>();
export const appSockets = new Map<string, Set<WebSocket>>(); // accountId -> sockets

// Data-plane routing: which app sockets get a device's status/progress/
// debug_line/cmd_result pushes. An app subscribes per-device (not implicitly
// via account) so the relay doesn't have to re-derive ownership on every push
// — ownership is checked once, at subscribe time, in appSocket.ts.
const deviceSubscribers = new Map<string, Set<WebSocket>>(); // deviceId -> app sockets
const appSubscriptions = new Map<WebSocket, Set<string>>();  // app socket -> subscribed deviceIds

export function isDeviceOnline(deviceId: string): boolean {
  return deviceSockets.has(deviceId);
}

export function subscribeAppToDevice(socket: WebSocket, deviceId: string): void {
  if (!deviceSubscribers.has(deviceId)) deviceSubscribers.set(deviceId, new Set());
  deviceSubscribers.get(deviceId)!.add(socket);

  if (!appSubscriptions.has(socket)) appSubscriptions.set(socket, new Set());
  appSubscriptions.get(socket)!.add(deviceId);
}

// Call on app socket close — a device socket closing needs no equivalent
// cleanup here, since subscriptions are keyed by deviceId, not by the
// device's (possibly reconnecting) socket.
export function unsubscribeAppSocket(socket: WebSocket): void {
  const subs = appSubscriptions.get(socket);
  if (!subs) return;
  for (const deviceId of subs) {
    deviceSubscribers.get(deviceId)?.delete(socket);
  }
  appSubscriptions.delete(socket);
}

export function broadcastToDeviceSubscribers(deviceId: string, rawMessage: string): void {
  const subs = deviceSubscribers.get(deviceId);
  if (!subs) return;
  for (const socket of subs) socket.send(rawMessage);
}

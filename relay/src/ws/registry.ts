import type { WebSocket } from "ws";

// In-memory presence registry. Fine for a single relay instance; a multi-instance
// deployment would need a shared layer (e.g. Redis pub/sub) to route across
// processes — not needed until the relay actually scales beyond one container.
export const deviceSockets = new Map<string, WebSocket>();
export const appSockets = new Map<string, Set<WebSocket>>(); // accountId -> sockets

export function isDeviceOnline(deviceId: string): boolean {
  return deviceSockets.has(deviceId);
}

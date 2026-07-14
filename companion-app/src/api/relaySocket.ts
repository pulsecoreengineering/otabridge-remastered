import { getRelayWsBase } from "./client";

// deviceId is present on every device->relay->app push (the relay stamps it
// on before forwarding, see deviceSocket.ts) except "error", which the relay
// itself generates and already scopes via requestId. Marked optional rather
// than required since TS can't otherwise express "present on all variants
// but this one."
export type RelayMessage =
  | { type: "subscribed"; deviceId: string }
  | { type: "status"; deviceId?: string; state: string; page?: number; total?: number; lastError?: string }
  | { type: "progress"; deviceId?: string; page: number; total: number; label: string }
  | { type: "debug_line"; deviceId?: string; line: string; ms?: number }
  | { type: "cmd_result"; deviceId?: string; requestId: string; ok: boolean; message?: string }
  | { type: "error"; message: string; requestId?: string | null };

type Listener = (msg: RelayMessage) => void;

// Thin wrapper over the relay's /ws/app protocol — see relay/README.md for
// the wire format. sendCmd() turns the fire-and-forget WS message into a
// promise resolved/rejected by the matching cmd_result, since that's the
// ergonomic the UI actually wants (await the result of a command).
export class RelaySocket {
  private ws: WebSocket | null = null;
  private pending = new Map<string, { resolve: (v: RelayMessage) => void; reject: (e: Error) => void }>();
  private listeners = new Set<Listener>();

  connect(token: string): Promise<void> {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(`${getRelayWsBase()}/ws/app?token=${encodeURIComponent(token)}`);
      this.ws = ws;
      ws.onopen = () => resolve();
      ws.onerror = () => reject(new Error("WebSocket connection failed"));
      ws.onmessage = (evt) => this.handleMessage(evt.data);
      ws.onclose = () => {
        for (const [, p] of this.pending) p.reject(new Error("connection closed"));
        this.pending.clear();
      };
    });
  }

  private handleMessage(raw: string): void {
    let msg: RelayMessage;
    try {
      msg = JSON.parse(raw);
    } catch {
      return;
    }
    if (msg.type === "cmd_result" && this.pending.has(msg.requestId)) {
      const p = this.pending.get(msg.requestId)!;
      this.pending.delete(msg.requestId);
      if (msg.ok) p.resolve(msg);
      else p.reject(new Error(msg.message || "command failed"));
    }
    for (const l of this.listeners) l(msg);
  }

  onMessage(fn: Listener): () => void {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  subscribe(deviceId: string): void {
    this.ws?.send(JSON.stringify({ type: "subscribe", deviceId }));
  }

  sendCmd(
    deviceId: string,
    action: string,
    extra: Record<string, unknown> = {},
    timeoutMs = 15000,
  ): Promise<RelayMessage> {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error("not connected"));
    }
    const requestId = `req-${Math.random().toString(36).slice(2)}`;
    const msg = { type: "cmd", deviceId, action, requestId, ...extra };

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.pending.has(requestId)) {
          this.pending.delete(requestId);
          reject(new Error(`${action} timed out`));
        }
      }, timeoutMs);

      this.pending.set(requestId, {
        resolve: (v) => { clearTimeout(timer); resolve(v); },
        reject: (e) => { clearTimeout(timer); reject(e); },
      });
      this.ws!.send(JSON.stringify(msg));
    });
  }

  close(): void {
    this.ws?.close();
    this.ws = null;
  }
}

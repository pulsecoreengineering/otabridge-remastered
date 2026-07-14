const TOKEN_KEY = "otabridge_token";

export function getToken(): string | null {
  return localStorage.getItem(TOKEN_KEY);
}

export function setToken(token: string): void {
  localStorage.setItem(TOKEN_KEY, token);
}

export function clearToken(): void {
  localStorage.removeItem(TOKEN_KEY);
}

export function getRelayBase(): string {
  // Trim a trailing slash so callers can always do `getRelayBase() + "/path"`.
  const base = import.meta.env.VITE_RELAY_URL as string | undefined;
  return (base ?? "https://otabridge-remastered-production.up.railway.app").replace(/\/$/, "");
}

export function getRelayWsBase(): string {
  return getRelayBase().replace(/^http/, "ws");
}

class ApiError extends Error {
  status: number;
  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

async function request(path: string, init: RequestInit = {}): Promise<any> {
  const token = getToken();
  const headers: Record<string, string> = {
    "Content-Type": "application/json",
    ...(init.headers as Record<string, string> | undefined),
  };
  if (token) headers.Authorization = `Bearer ${token}`;

  const res = await fetch(getRelayBase() + path, { ...init, headers });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new ApiError(data.error || `HTTP ${res.status}`, res.status);
  return data;
}

export interface DeviceSummary {
  id: string;
  name: string;
  claimedAt: string | null;
  online: boolean;
}

export const relayApi = {
  signup: (email: string, password: string): Promise<{ token: string }> =>
    request("/auth/signup", { method: "POST", body: JSON.stringify({ email, password }) }),

  login: (email: string, password: string): Promise<{ token: string }> =>
    request("/auth/login", { method: "POST", body: JSON.stringify({ email, password }) }),

  listDevices: (): Promise<DeviceSummary[]> => request("/devices"),

  claimDevice: (code: string): Promise<{ deviceId: string }> =>
    request("/devices/claim", { method: "POST", body: JSON.stringify({ code }) }),

  renameDevice: (deviceId: string, name: string): Promise<{ id: string; name: string }> =>
    request(`/devices/${encodeURIComponent(deviceId)}/name`, { method: "PATCH", body: JSON.stringify({ name }) }),

  unclaimDevice: (deviceId: string): Promise<{ id: string; unclaimed: boolean }> =>
    request(`/devices/${encodeURIComponent(deviceId)}`, { method: "DELETE" }),

  getPushPublicKey: (): Promise<{ publicKey: string }> => request("/push/public-key"),

  subscribePush: (sub: PushSubscriptionJSON): Promise<{ subscribed: boolean }> =>
    request("/push/subscribe", {
      method: "POST",
      body: JSON.stringify({ endpoint: sub.endpoint, keys: sub.keys }),
    }),

  unsubscribePush: (endpoint: string): Promise<{ unsubscribed: boolean }> =>
    request("/push/subscribe", { method: "DELETE", body: JSON.stringify({ endpoint }) }),
};

export { ApiError };

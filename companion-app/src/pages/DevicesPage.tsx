import { useEffect, useState, useCallback } from "react";
import { relayApi, clearToken, ApiError, type DeviceSummary } from "../api/client";

export function DevicesPage({
  onLogout,
  onSelectDevice,
}: {
  onLogout: () => void;
  onSelectDevice: (deviceId: string) => void;
}) {
  const [devices, setDevices] = useState<DeviceSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [claimCode, setClaimCode] = useState("");
  const [claimBusy, setClaimBusy] = useState(false);
  const [claimMsg, setClaimMsg] = useState<{ text: string; ok: boolean } | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      setDevices(await relayApi.listDevices());
    } catch (e) {
      if (e instanceof ApiError && e.status === 401) {
        clearToken();
        onLogout();
        return;
      }
      setError(e instanceof ApiError ? e.message : "Could not load devices");
    } finally {
      setLoading(false);
    }
  }, [onLogout]);

  useEffect(() => {
    refresh();
  }, [refresh]);

  async function handleClaim() {
    setClaimMsg(null);
    setClaimBusy(true);
    try {
      await relayApi.claimDevice(claimCode.trim());
      setClaimMsg({ text: "Device claimed", ok: true });
      setClaimCode("");
      refresh();
    } catch (e) {
      setClaimMsg({ text: e instanceof ApiError ? e.message : "Claim failed", ok: false });
    } finally {
      setClaimBusy(false);
    }
  }

  const onlineCount = devices.filter((d) => d.online).length;

  return (
    <>
      {/* Claim bar — compact, single row */}
      <div className="panel">
        <div className="panel-body" style={{ padding: "10px 12px" }}>
          <div className="row">
            <input
              style={{ flex: 1 }}
              placeholder="Claim code — XXXX-XXXX"
              value={claimCode}
              onChange={(e) => setClaimCode(e.target.value.toUpperCase())}
              disabled={claimBusy}
              onKeyDown={(e) => e.key === "Enter" && claimCode.trim() && handleClaim()}
            />
            <button className="primary" disabled={claimBusy || !claimCode.trim()} onClick={handleClaim}>
              Claim
            </button>
          </div>
          {claimMsg && <div className={`msg ${claimMsg.ok ? "ok" : "err"}`}>{claimMsg.text}</div>}
        </div>
      </div>

      <div className="panel">
        <div className="panel-header">
          <span className="panel-title">
            Devices{devices.length > 0 && ` — ${onlineCount}/${devices.length} online`}
          </span>
          <button className="ghost" onClick={refresh} disabled={loading}>&#8635;</button>
        </div>
        <div className="panel-body">
          {error && <div className="msg err">{error}</div>}
          {loading ? (
            <p style={{ color: "var(--text-muted)", fontSize: 11 }}>Loading…</p>
          ) : devices.length === 0 ? (
            <p style={{ color: "var(--text-muted)", fontSize: 11 }}>No devices yet — claim one above.</p>
          ) : (
            <ul className="device-list">
              {devices.map((d) => (
                <li key={d.id} className="device-row" onClick={() => onSelectDevice(d.id)}>
                  <div className="device-row-left">
                    <span className={`dot ${d.online ? "online" : ""}`} />
                    <span>{d.name}</span>
                    <span className="device-id">{d.id}</span>
                  </div>
                  <span className={`badge ${d.online ? "online" : ""}`}>{d.online ? "online" : "offline"}</span>
                </li>
              ))}
            </ul>
          )}
        </div>
      </div>

      <div className="row-between">
        <span style={{ fontSize: 10, color: "var(--text-muted)" }}>PulseCore Engineering</span>
        <button className="ghost" onClick={() => { clearToken(); onLogout(); }}>Log out</button>
      </div>
    </>
  );
}

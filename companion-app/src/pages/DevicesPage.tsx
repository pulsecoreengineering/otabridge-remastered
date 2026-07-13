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

  return (
    <>
      <div className="row-between" style={{ marginBottom: 16 }}>
        <h1 style={{ marginBottom: 0 }}>Your devices</h1>
        <button onClick={() => { clearToken(); onLogout(); }}>Log out</button>
      </div>

      <div className="card">
        <h2>Add a device</h2>
        <p className="sub">Enter the claim code shown on the device's serial console after it joins Wi-Fi.</p>
        <div className="row">
          <input
            style={{ flex: 1 }}
            placeholder="XXXX-XXXX"
            value={claimCode}
            onChange={(e) => setClaimCode(e.target.value.toUpperCase())}
            disabled={claimBusy}
          />
          <button className="primary" style={{ width: "auto" }} disabled={claimBusy || !claimCode.trim()} onClick={handleClaim}>
            Claim
          </button>
        </div>
        {claimMsg && <div className={`msg ${claimMsg.ok ? "ok" : "err"}`}>{claimMsg.text}</div>}
      </div>

      <div className="card">
        <div className="row-between">
          <h2 style={{ marginBottom: 0 }}>Devices</h2>
          <button onClick={refresh} disabled={loading}>Refresh</button>
        </div>
        {error && <div className="msg err">{error}</div>}
        {loading ? (
          <p className="sub" style={{ marginTop: 12 }}>Loading…</p>
        ) : devices.length === 0 ? (
          <p className="sub" style={{ marginTop: 12 }}>No devices yet — claim one above.</p>
        ) : (
          <ul className="device-list" style={{ marginTop: 8 }}>
            {devices.map((d) => (
              <li key={d.id} className="device-item" onClick={() => onSelectDevice(d.id)}>
                <span>
                  <span className={`dot ${d.online ? "online" : "offline"}`} />
                  {d.name}
                </span>
                <span className="sub">{d.online ? "online" : "offline"}</span>
              </li>
            ))}
          </ul>
        )}
      </div>
    </>
  );
}

import { useEffect, useState, useCallback } from "react";
import { relayApi, clearToken, ApiError, type DeviceSummary } from "../api/client";
import { DeviceRow } from "../components/DeviceRow";
import { pushSupported, isPushEnabled, enablePushNotifications, disablePushNotifications } from "../push";

export function DevicesPage({
  onLogout,
  onSelectDevice,
  onFlashFleet,
}: {
  onLogout: () => void;
  onSelectDevice: (deviceId: string) => void;
  onFlashFleet: (devices: DeviceSummary[]) => void;
}) {
  const [devices, setDevices] = useState<DeviceSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [claimCode, setClaimCode] = useState("");
  const [claimBusy, setClaimBusy] = useState(false);
  const [claimMsg, setClaimMsg] = useState<{ text: string; ok: boolean } | null>(null);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [pushEnabled, setPushEnabled] = useState(false);
  const [pushBusy, setPushBusy] = useState(false);
  const [pushError, setPushError] = useState<string | null>(null);
  const [pushMsg, setPushMsg] = useState<string | null>(null);

  useEffect(() => {
    if (pushSupported()) isPushEnabled().then(setPushEnabled);
  }, []);

  async function togglePush() {
    setPushBusy(true);
    setPushError(null);
    setPushMsg(null);
    try {
      if (pushEnabled) {
        await disablePushNotifications();
        setPushEnabled(false);
      } else {
        await enablePushNotifications();
        setPushEnabled(true);
      }
    } catch (e) {
      setPushError(e instanceof Error ? e.message : "Failed");
    } finally {
      setPushBusy(false);
    }
  }

  async function sendTestPush() {
    setPushBusy(true);
    setPushError(null);
    setPushMsg(null);
    try {
      const { sent } = await relayApi.sendTestPush();
      setPushMsg(sent > 0 ? `Sent to ${sent} subscription(s) — check for a notification` : "No subscriptions found");
    } catch (e) {
      setPushError(e instanceof ApiError ? e.message : "Failed to send test notification");
    } finally {
      setPushBusy(false);
    }
  }

  const refresh = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const list = await relayApi.listDevices();
      setDevices(list);
      // Drop selections for devices that went offline or disappeared —
      // fleet flash only ever targets devices confirmed online right now.
      const onlineIds = new Set(list.filter((d) => d.online).map((d) => d.id));
      setSelected((prev) => new Set([...prev].filter((id) => onlineIds.has(id))));
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

  function toggleSelected(id: string) {
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id); else next.add(id);
      return next;
    });
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
          <div className="row">
            {selected.size > 0 && (
              <button className="primary" onClick={() => onFlashFleet(devices.filter((d) => selected.has(d.id)))}>
                Flash {selected.size} selected
              </button>
            )}
            <button className="ghost" onClick={refresh} disabled={loading}>&#8635;</button>
          </div>
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
                <DeviceRow
                  key={d.id}
                  device={d}
                  onSelect={() => onSelectDevice(d.id)}
                  onChanged={refresh}
                  checked={selected.has(d.id)}
                  onToggleChecked={() => toggleSelected(d.id)}
                />
              ))}
            </ul>
          )}
        </div>
      </div>

      <div className="row-between">
        <span style={{ fontSize: 10, color: "var(--text-muted)" }}>PulseCore Engineering</span>
        <div className="row">
          {pushSupported() && (
            <>
              <button className="ghost" disabled={pushBusy} onClick={togglePush}>
                {pushEnabled ? "Disable notifications" : "Enable notifications"}
              </button>
              {pushEnabled && (
                <button className="ghost" disabled={pushBusy} onClick={sendTestPush}>
                  Send test
                </button>
              )}
            </>
          )}
          <button className="ghost" onClick={() => { clearToken(); onLogout(); }}>Log out</button>
        </div>
      </div>
      {pushError && <div className="msg err">{pushError}</div>}
      {pushMsg && <div className="msg ok">{pushMsg}</div>}
    </>
  );
}

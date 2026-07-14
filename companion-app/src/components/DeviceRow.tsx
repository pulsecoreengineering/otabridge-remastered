import { useState } from "react";
import { relayApi, ApiError, type DeviceSummary } from "../api/client";

export function DeviceRow({
  device,
  onSelect,
  onChanged,
}: {
  device: DeviceSummary;
  onSelect: () => void;
  onChanged: () => void; // ask the parent to refetch the list after rename/unclaim
}) {
  const [editing, setEditing] = useState(false);
  const [name, setName] = useState(device.name);
  const [confirmingUnclaim, setConfirmingUnclaim] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function saveName() {
    const trimmed = name.trim();
    if (!trimmed || trimmed === device.name) {
      setEditing(false);
      setName(device.name);
      return;
    }
    setBusy(true);
    setError(null);
    try {
      await relayApi.renameDevice(device.id, trimmed);
      setEditing(false);
      onChanged();
    } catch (e) {
      setError(e instanceof ApiError ? e.message : "Rename failed");
    } finally {
      setBusy(false);
    }
  }

  async function unclaim() {
    setBusy(true);
    setError(null);
    try {
      await relayApi.unclaimDevice(device.id);
      onChanged();
    } catch (e) {
      setError(e instanceof ApiError ? e.message : "Unclaim failed");
      setBusy(false);
    }
  }

  return (
    <li className="device-row" onClick={editing || confirmingUnclaim ? undefined : onSelect}>
      <div className="device-row-left">
        <span className={`dot ${device.online ? "online" : ""}`} />
        {editing ? (
          <input
            autoFocus
            style={{ width: 160 }}
            value={name}
            disabled={busy}
            onClick={(e) => e.stopPropagation()}
            onChange={(e) => setName(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter") saveName();
              if (e.key === "Escape") { setEditing(false); setName(device.name); }
            }}
            onBlur={saveName}
          />
        ) : (
          <span
            onClick={(e) => { e.stopPropagation(); setEditing(true); }}
            title="Click to rename"
          >
            {device.name}
          </span>
        )}
        <span className="device-id">{device.id}</span>
      </div>

      <div className="row" onClick={(e) => e.stopPropagation()}>
        {error && <span style={{ fontSize: 10, color: "var(--red)" }}>{error}</span>}
        {confirmingUnclaim ? (
          <>
            <button className="danger" disabled={busy} onClick={unclaim}>Confirm</button>
            <button disabled={busy} onClick={() => setConfirmingUnclaim(false)}>Cancel</button>
          </>
        ) : (
          <>
            <span className={`badge ${device.online ? "online" : ""}`}>{device.online ? "online" : "offline"}</span>
            <button className="ghost" disabled={busy} onClick={() => setConfirmingUnclaim(true)} title="Unclaim device">
              &times;
            </button>
          </>
        )}
      </div>
    </li>
  );
}

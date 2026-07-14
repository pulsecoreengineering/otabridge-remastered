import { useEffect, useRef, useState } from "react";
import { RelaySocket, type RelayMessage } from "../api/relaySocket";
import { getToken, type DeviceSummary } from "../api/client";
import { CHUNK_SIZE, MAX_HEX_BYTES } from "../constants";

type Phase = "idle" | "uploading" | "flashing" | "success" | "error";

type DeviceFlashState = {
  phase: Phase;
  uploadProgress: { sent: number; total: number } | null;
  progress: { page: number; total: number; label: string } | null;
  error: string | null;
};

function initialState(): DeviceFlashState {
  return { phase: "idle", uploadProgress: null, progress: null, error: null };
}

// One relay connection, subscribed to every selected device — the
// "fleet"/bench feature this whole app was originally pitched for
// (README: "Deployable companion app for multi-device benches"). Each
// device's own flash runs concurrently, not sequentially: they're
// independent physical boards, no reason to make someone wait for board 1
// to finish before board 2 even starts uploading.
export function FleetFlashPage({ devices, onBack }: { devices: DeviceSummary[]; onBack: () => void }) {
  const socketRef = useRef<RelaySocket | null>(null);
  const [connected, setConnected] = useState(false);
  const [connError, setConnError] = useState<string | null>(null);
  const [file, setFile] = useState<File | null>(null);
  const [protocol, setProtocol] = useState<"auto" | "v1" | "v2">("auto");
  const [running, setRunning] = useState(false);
  const [states, setStates] = useState<Record<string, DeviceFlashState>>(() =>
    Object.fromEntries(devices.map((d) => [d.id, initialState()])),
  );

  function patchDevice(deviceId: string, patch: Partial<DeviceFlashState>) {
    setStates((prev) => ({ ...prev, [deviceId]: { ...prev[deviceId], ...patch } }));
  }

  useEffect(() => {
    const token = getToken();
    if (!token) return;
    const socket = new RelaySocket();
    socketRef.current = socket;
    let cancelled = false;
    setConnError(null);

    const unsubscribe = socket.onMessage((msg: RelayMessage) => {
      if (!("deviceId" in msg) || !msg.deviceId) return;
      if (msg.type === "status") {
        if (msg.state === "success") patchDevice(msg.deviceId, { phase: "success" });
        else if (msg.state === "error") patchDevice(msg.deviceId, { phase: "error", error: msg.lastError || "flash failed" });
      } else if (msg.type === "progress") {
        patchDevice(msg.deviceId, {
          phase: "flashing",
          progress: { page: msg.page, total: msg.total, label: msg.label },
        });
      }
    });

    socket.connect(token)
      .then(() => {
        if (cancelled) return;
        for (const d of devices) socket.subscribe(d.id);
        setConnected(true);
      })
      .catch((e) => { if (!cancelled) setConnError(e.message); });

    return () => {
      cancelled = true;
      unsubscribe();
      socket.close();
    };
    // devices intentionally not in deps — the selection is fixed for the
    // lifetime of this page, re-subscribing on every render would be wrong.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  async function flashOne(deviceId: string, text: string) {
    const socket = socketRef.current;
    if (!socket) return;
    try {
      patchDevice(deviceId, { phase: "uploading", error: null });
      await socket.sendCmd(deviceId, "program_start", { totalBytes: text.length, protocol });

      const chunks = Math.ceil(text.length / CHUNK_SIZE) || 1;
      for (let i = 0; i < chunks; i++) {
        const chunk = text.slice(i * CHUNK_SIZE, (i + 1) * CHUNK_SIZE);
        await socket.sendCmd(deviceId, "program_chunk", { seq: i, data: chunk });
        patchDevice(deviceId, { uploadProgress: { sent: i + 1, total: chunks } });
      }

      await socket.sendCmd(deviceId, "program_end");
      patchDevice(deviceId, { phase: "flashing" });
    } catch (e) {
      patchDevice(deviceId, { phase: "error", error: e instanceof Error ? e.message : "failed" });
    }
  }

  async function handleStart() {
    if (!file || !connected) return;
    const text = await file.text();
    if (text.length > MAX_HEX_BYTES) {
      for (const d of devices) {
        patchDevice(d.id, { phase: "error", error: `file too large (${text.length} > ${MAX_HEX_BYTES} bytes)` });
      }
      return;
    }
    setRunning(true);
    setStates(Object.fromEntries(devices.map((d) => [d.id, initialState()])));
    await Promise.allSettled(devices.map((d) => flashOne(d.id, text)));
    setRunning(false);
  }

  const badgeClass = (phase: Phase) =>
    phase === "uploading" || phase === "flashing" ? "busy" : phase === "error" ? "error" : phase === "success" ? "success" : "";

  return (
    <>
      <button className="back-link" onClick={onBack}>&larr; devices</button>

      <div className="row-between" style={{ marginBottom: 14 }}>
        <span style={{ fontSize: 13, fontWeight: 600 }}>Fleet flash — {devices.length} devices</span>
      </div>

      {connError && <div className="msg err">{connError}</div>}

      <div className="panel">
        <div className="panel-header">
          <span className="panel-title">Hex file</span>
        </div>
        <div className="panel-body">
          <label className="upload-zone" style={{ display: "block", marginBottom: 10 }}>
            <input type="file" accept=".hex" onChange={(e) => setFile(e.target.files?.[0] ?? null)} disabled={running} />
            {file ? file.name : "Drop .hex or click to browse — flashed to all selected devices"}
          </label>
          <div className="row" style={{ marginBottom: 10 }}>
            <select value={protocol} onChange={(e) => setProtocol(e.target.value as "auto" | "v1" | "v2")} disabled={running}>
              <option value="auto">Auto-detect</option>
              <option value="v1">STK500v1</option>
              <option value="v2">STK500v2</option>
            </select>
          </div>
          <button className="primary" style={{ width: "100%" }} disabled={running || !connected || !file} onClick={handleStart}>
            {running ? "Flashing…" : `Flash ${devices.length} devices`}
          </button>
        </div>
      </div>

      <div className="panel">
        <div className="panel-header">
          <span className="panel-title">Devices</span>
        </div>
        <div className="panel-body">
          <ul className="device-list">
            {devices.map((d) => {
              const s = states[d.id] ?? initialState();
              return (
                <li key={d.id} className="device-row" style={{ cursor: "default" }}>
                  <div className="device-row-left">
                    <span>{d.name}</span>
                    <span className="device-id">{d.id}</span>
                  </div>
                  <div className="row">
                    {s.phase === "uploading" && s.uploadProgress && (
                      <span style={{ fontSize: 10, color: "var(--text-muted)" }}>
                        upload {s.uploadProgress.sent}/{s.uploadProgress.total}
                      </span>
                    )}
                    {s.phase === "flashing" && s.progress && s.progress.total > 0 && (
                      <span style={{ fontSize: 10, color: "var(--text-muted)" }}>
                        {s.progress.label} {s.progress.page}/{s.progress.total}
                      </span>
                    )}
                    {s.error && <span style={{ fontSize: 10, color: "var(--red)" }}>{s.error}</span>}
                    <span className={`badge ${badgeClass(s.phase)}`}>{s.phase}</span>
                  </div>
                </li>
              );
            })}
          </ul>
        </div>
      </div>
    </>
  );
}

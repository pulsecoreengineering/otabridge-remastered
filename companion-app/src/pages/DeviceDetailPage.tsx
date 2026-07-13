import { useEffect, useRef, useState } from "react";
import { RelaySocket, type RelayMessage } from "../api/relaySocket";
import { getToken } from "../api/client";
import { ActivityLog } from "../components/ActivityLog";
import { DebugConsole } from "../components/DebugConsole";
import { nowStamp, type LogLine } from "../types";

const CHUNK_SIZE = 4096; // chars of hex text per program_chunk — see relay/README.md
const MAX_HEX_BYTES = 131072; // must match RELAY_PROGRAM_MAX_HEX_BYTES in firmware

type Status = { state: string; page: number; total: number; lastError: string };

const BUSY_STATES = new Set(["loading", "entering_progmode", "reading_signature", "programming", "exiting"]);

export function DeviceDetailPage({ deviceId, onBack }: { deviceId: string; onBack: () => void }) {
  const socketRef = useRef<RelaySocket | null>(null);
  const [connected, setConnected] = useState(false);
  const [connError, setConnError] = useState<string | null>(null);
  const [status, setStatus] = useState<Status>({ state: "unknown", page: 0, total: 0, lastError: "" });
  const [progress, setProgress] = useState<{ page: number; total: number; label: string } | null>(null);
  // Two separate feeds on purpose — see components/ActivityLog.tsx and
  // components/DebugConsole.tsx: relay/control-plane activity ("my command
  // failed") is a different concern from the target device's own serial
  // output ("the Arduino printed something"), and conflating them made both
  // harder to read.
  const [activityLog, setActivityLog] = useState<LogLine[]>([]);
  const [debugLog, setDebugLog] = useState<LogLine[]>([]);
  const [debugActive, setDebugActive] = useState(false);
  const [file, setFile] = useState<File | null>(null);
  const [protocol, setProtocol] = useState<"auto" | "v1" | "v2">("auto");
  const [uploadProgress, setUploadProgress] = useState<{ sent: number; total: number } | null>(null);
  const [busy, setBusy] = useState(false);

  function logActivity(text: string) {
    setActivityLog((prev) => [...prev.slice(-199), { time: nowStamp(), text }]);
  }
  function logDebug(text: string) {
    setDebugLog((prev) => [...prev.slice(-499), { time: nowStamp(), text }]);
  }

  useEffect(() => {
    const token = getToken();
    if (!token) return;
    const socket = new RelaySocket();
    socketRef.current = socket;
    // React StrictMode (dev only) mounts this effect, cleans it up, then
    // mounts it again — closing this exact socket before its connect()
    // promise settles. Without this guard, that discarded socket's onerror
    // still fires setConnError() after the *second* (real) socket has
    // already connected successfully, leaving a stale error on screen next
    // to a working connection.
    let cancelled = false;
    setConnError(null);

    const unsubscribe = socket.onMessage((msg: RelayMessage) => {
      if (msg.type === "subscribed") {
        setConnected(true);
        logActivity(`subscribed to ${msg.deviceId}`);
      } else if (msg.type === "status") {
        setStatus({
          state: msg.state,
          page: msg.page ?? 0,
          total: msg.total ?? 0,
          lastError: msg.lastError ?? "",
        });
      } else if (msg.type === "progress") {
        setProgress({ page: msg.page, total: msg.total, label: msg.label });
      } else if (msg.type === "debug_line") {
        logDebug(msg.line);
      } else if (msg.type === "error") {
        logActivity(`error: ${msg.message}`);
      }
    });

    socket.connect(token)
      .then(() => { if (!cancelled) socket.subscribe(deviceId); })
      .catch((e) => { if (!cancelled) setConnError(e.message); });

    return () => {
      cancelled = true;
      unsubscribe();
      socket.close();
    };
  }, [deviceId]);

  async function runCmd(action: string, extra: Record<string, unknown> = {}) {
    if (!socketRef.current) return;
    setBusy(true);
    try {
      await socketRef.current.sendCmd(deviceId, action, extra);
      logActivity(`${action}: ok`);
    } catch (e) {
      logActivity(`${action}: ${e instanceof Error ? e.message : "failed"}`);
    } finally {
      setBusy(false);
    }
  }

  async function handleProgram() {
    if (!file || !socketRef.current) return;
    setBusy(true);
    setUploadProgress(null);
    try {
      const text = await file.text();
      if (text.length > MAX_HEX_BYTES) {
        logActivity(`program: file too large for relay upload (${text.length} > ${MAX_HEX_BYTES} bytes) — not supported over the relay yet`);
        return;
      }

      await socketRef.current.sendCmd(deviceId, "program_start", { totalBytes: text.length, protocol });

      const chunks = Math.ceil(text.length / CHUNK_SIZE) || 1;
      for (let i = 0; i < chunks; i++) {
        const chunk = text.slice(i * CHUNK_SIZE, (i + 1) * CHUNK_SIZE);
        await socketRef.current.sendCmd(deviceId, "program_chunk", { seq: i, data: chunk });
        setUploadProgress({ sent: i + 1, total: chunks });
      }

      await socketRef.current.sendCmd(deviceId, "program_end");
      logActivity("program: upload complete, flashing started");
    } catch (e) {
      logActivity(`program: ${e instanceof Error ? e.message : "failed"}`);
    } finally {
      setBusy(false);
    }
  }

  const stateBadgeClass = BUSY_STATES.has(status.state)
    ? "busy"
    : status.state === "error" ? "error" : status.state === "success" ? "success" : "";

  return (
    <>
      <button className="back-link" onClick={onBack}>&larr; devices</button>

      <div className="row-between" style={{ marginBottom: 14 }}>
        <div className="row">
          <span className={`dot ${connected ? "online" : ""}`} />
          <span style={{ fontSize: 13, fontWeight: 600 }}>{deviceId}</span>
        </div>
        <span className={`badge ${stateBadgeClass}`}>{status.state}</span>
      </div>

      {connError && <div className="msg err">{connError}</div>}

      <div className="grid-2">
        {/* Control panel */}
        <div className="panel">
          <div className="panel-header">
            <span className="panel-title">Control</span>
          </div>
          <div className="panel-body">
            {progress && progress.total > 0 ? (
              <>
                <div className="progress-track">
                  <div className="progress-fill" style={{ width: `${(progress.page / progress.total) * 100}%` }} />
                </div>
                <p style={{ fontSize: 10, color: "var(--text-muted)" }}>
                  {progress.label} — page {progress.page}/{progress.total}
                </p>
              </>
            ) : (
              <p style={{ fontSize: 11, color: "var(--text-dim)" }}>
                {connected ? "Idle — no active flash" : "Connecting to relay…"}
              </p>
            )}
            {status.lastError && <div className="msg err">{status.lastError}</div>}

            <div className="row" style={{ marginTop: 12 }}>
              <button disabled={busy || !connected} onClick={() => runCmd("status")}>Refresh</button>
              <button disabled={busy || !connected} onClick={() => runCmd("cancel")}>Cancel</button>
              <button className="danger" disabled={busy || !connected} onClick={() => runCmd("restart")}>Restart</button>
            </div>
          </div>
        </div>

        {/* Program panel */}
        <div className="panel">
          <div className="panel-header">
            <span className="panel-title">Program</span>
            <span className="badge">Uno/Nano/Mini/Leo</span>
          </div>
          <div className="panel-body">
            <label className="upload-zone" style={{ display: "block", marginBottom: 10 }}>
              <input type="file" accept=".hex" onChange={(e) => setFile(e.target.files?.[0] ?? null)} />
              {file ? file.name : "Drop .hex or click to browse"}
            </label>
            <div className="row" style={{ marginBottom: 10 }}>
              <select value={protocol} onChange={(e) => setProtocol(e.target.value as "auto" | "v1" | "v2")}>
                <option value="auto">Auto-detect</option>
                <option value="v1">STK500v1</option>
                <option value="v2">STK500v2</option>
              </select>
            </div>
            <button className="primary" style={{ width: "100%" }} disabled={busy || !connected || !file} onClick={handleProgram}>
              Upload &amp; Flash
            </button>
            {uploadProgress && (
              <div className="progress-track">
                <div className="progress-fill" style={{ width: `${(uploadProgress.sent / uploadProgress.total) * 100}%` }} />
              </div>
            )}
          </div>
        </div>
      </div>

      <ActivityLog lines={activityLog} />

      <DebugConsole
        lines={debugLog}
        active={debugActive}
        disabled={busy || !connected}
        onStart={(baud) => { runCmd("debug_start", { baud }); setDebugActive(true); }}
        onStop={() => { runCmd("debug_stop"); setDebugActive(false); }}
        onSend={(text) => runCmd("debug_send", { text })}
      />
    </>
  );
}

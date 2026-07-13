import { useEffect, useRef, useState } from "react";
import { RelaySocket, type RelayMessage } from "../api/relaySocket";
import { getToken } from "../api/client";

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
  const [log, setLog] = useState<string[]>([]);
  const [debugActive, setDebugActive] = useState(false);
  const [debugText, setDebugText] = useState("");
  const [file, setFile] = useState<File | null>(null);
  const [protocol, setProtocol] = useState<"auto" | "v1" | "v2">("auto");
  const [uploadProgress, setUploadProgress] = useState<{ sent: number; total: number } | null>(null);
  const [busy, setBusy] = useState(false);

  function appendLog(line: string) {
    setLog((prev) => [...prev.slice(-199), line]);
  }

  useEffect(() => {
    const token = getToken();
    if (!token) return;
    const socket = new RelaySocket();
    socketRef.current = socket;

    const unsubscribe = socket.onMessage((msg: RelayMessage) => {
      if (msg.type === "subscribed") {
        setConnected(true);
        appendLog(`subscribed to ${msg.deviceId}`);
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
        appendLog(`[debug] ${msg.line}`);
      } else if (msg.type === "error") {
        appendLog(`[error] ${msg.message}`);
      }
    });

    socket.connect(token)
      .then(() => socket.subscribe(deviceId))
      .catch((e) => setConnError(e.message));

    return () => {
      unsubscribe();
      socket.close();
    };
  }, [deviceId]);

  async function runCmd(action: string, extra: Record<string, unknown> = {}) {
    if (!socketRef.current) return;
    setBusy(true);
    try {
      await socketRef.current.sendCmd(deviceId, action, extra);
      appendLog(`${action}: ok`);
    } catch (e) {
      appendLog(`${action}: ${e instanceof Error ? e.message : "failed"}`);
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
        appendLog(`program: file too large for relay upload (${text.length} > ${MAX_HEX_BYTES} bytes of hex text) — this board/size isn't supported over the relay yet`);
        return;
      }

      await socketRef.current.sendCmd(deviceId, "program_start", {
        totalBytes: text.length,
        protocol,
      });

      const chunks = Math.ceil(text.length / CHUNK_SIZE) || 1;
      for (let i = 0; i < chunks; i++) {
        const chunk = text.slice(i * CHUNK_SIZE, (i + 1) * CHUNK_SIZE);
        await socketRef.current.sendCmd(deviceId, "program_chunk", { seq: i, data: chunk });
        setUploadProgress({ sent: i + 1, total: chunks });
      }

      await socketRef.current.sendCmd(deviceId, "program_end");
      appendLog("program: upload complete, flashing started");
    } catch (e) {
      appendLog(`program: ${e instanceof Error ? e.message : "failed"}`);
    } finally {
      setBusy(false);
    }
  }

  const stateBadgeClass = BUSY_STATES.has(status.state)
    ? "busy"
    : status.state === "error"
      ? "error"
      : status.state === "success"
        ? "success"
        : "";

  return (
    <>
      <button className="back-link" onClick={onBack}>&larr; Devices</button>

      <div className="card">
        <div className="row-between">
          <h1 style={{ marginBottom: 0 }}>{deviceId}</h1>
          <span className={`state-badge ${stateBadgeClass}`}>{status.state}</span>
        </div>
        <p className="sub">{connected ? "Connected to relay" : connError ?? "Connecting…"}</p>
        {progress && progress.total > 0 && (
          <>
            <div className="progress-track">
              <div className="progress-fill" style={{ width: `${(progress.page / progress.total) * 100}%` }} />
            </div>
            <p className="sub">{progress.label} — page {progress.page}/{progress.total}</p>
          </>
        )}
        {status.lastError && <div className="msg err">{status.lastError}</div>}

        <div className="row" style={{ marginTop: 12 }}>
          <button disabled={busy || !connected} onClick={() => runCmd("status")}>Refresh status</button>
          <button disabled={busy || !connected} onClick={() => runCmd("cancel")}>Cancel</button>
          <button disabled={busy || !connected} onClick={() => runCmd("restart")}>Restart device</button>
        </div>
      </div>

      <div className="card">
        <h2>Program</h2>
        <p className="sub">Small/medium boards only for now (Uno, Nano, Pro Mini, Leonardo, Pro Micro) — Mega-class images need streaming ingest, not yet built.</p>
        <div className="field">
          <label>Hex file</label>
          <input type="file" accept=".hex" onChange={(e) => setFile(e.target.files?.[0] ?? null)} />
        </div>
        <div className="field">
          <label>Protocol</label>
          <select value={protocol} onChange={(e) => setProtocol(e.target.value as "auto" | "v1" | "v2")}>
            <option value="auto">Auto-detect</option>
            <option value="v1">STK500v1</option>
            <option value="v2">STK500v2</option>
          </select>
        </div>
        <button className="primary" disabled={busy || !connected || !file} onClick={handleProgram}>
          Upload &amp; Flash
        </button>
        {uploadProgress && (
          <div className="progress-track">
            <div className="progress-fill" style={{ width: `${(uploadProgress.sent / uploadProgress.total) * 100}%` }} />
          </div>
        )}
      </div>

      <div className="card">
        <h2>Debug console</h2>
        <div className="row" style={{ marginBottom: 8 }}>
          <button
            disabled={busy || !connected}
            onClick={() => { runCmd("debug_start"); setDebugActive(true); }}
          >
            Start
          </button>
          <button
            disabled={busy || !connected}
            onClick={() => { runCmd("debug_stop"); setDebugActive(false); }}
          >
            Stop
          </button>
        </div>
        <div className="row" style={{ marginBottom: 8 }}>
          <input
            style={{ flex: 1 }}
            placeholder="Send to target…"
            value={debugText}
            disabled={!debugActive}
            onChange={(e) => setDebugText(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter" && debugText.trim()) {
                runCmd("debug_send", { text: debugText });
                setDebugText("");
              }
            }}
          />
        </div>
        <div id="console-log">{log.join("\n")}</div>
      </div>
    </>
  );
}

import { useEffect, useRef, useState } from "react";
import { RelaySocket, type RelayMessage } from "../api/relaySocket";
import { getToken } from "../api/client";

const CHUNK_SIZE = 4096; // chars of hex text per program_chunk — see relay/README.md
const MAX_HEX_BYTES = 131072; // must match RELAY_PROGRAM_MAX_HEX_BYTES in firmware
const BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 230400, 500000];

type Status = { state: string; page: number; total: number; lastError: string };
type LogLine = { time: string; text: string };

const BUSY_STATES = new Set(["loading", "entering_progmode", "reading_signature", "programming", "exiting"]);

export function DeviceDetailPage({ deviceId, onBack }: { deviceId: string; onBack: () => void }) {
  const socketRef = useRef<RelaySocket | null>(null);
  const consoleRef = useRef<HTMLDivElement>(null);
  const [connected, setConnected] = useState(false);
  const [connError, setConnError] = useState<string | null>(null);
  const [status, setStatus] = useState<Status>({ state: "unknown", page: 0, total: 0, lastError: "" });
  const [progress, setProgress] = useState<{ page: number; total: number; label: string } | null>(null);
  const [log, setLog] = useState<LogLine[]>([]);
  const [debugActive, setDebugActive] = useState(false);
  const [debugBaud, setDebugBaud] = useState(9600);
  const [debugText, setDebugText] = useState("");
  const [file, setFile] = useState<File | null>(null);
  const [protocol, setProtocol] = useState<"auto" | "v1" | "v2">("auto");
  const [uploadProgress, setUploadProgress] = useState<{ sent: number; total: number } | null>(null);
  const [busy, setBusy] = useState(false);

  function appendLog(text: string) {
    const time = new Date().toTimeString().slice(0, 8);
    setLog((prev) => [...prev.slice(-199), { time, text }]);
  }

  useEffect(() => {
    consoleRef.current?.scrollTo({ top: consoleRef.current.scrollHeight });
  }, [log]);

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
        appendLog(msg.line);
      } else if (msg.type === "error") {
        appendLog(`error: ${msg.message}`);
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
        appendLog(`program: file too large for relay upload (${text.length} > ${MAX_HEX_BYTES} bytes) — not supported over the relay yet`);
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
      appendLog("program: upload complete, flashing started");
    } catch (e) {
      appendLog(`program: ${e instanceof Error ? e.message : "failed"}`);
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

      {/* Debug console */}
      <div className="panel">
        <div className="panel-header">
          <span className="panel-title">Debug console</span>
          <div className="row">
            <select
              value={debugBaud}
              onChange={(e) => setDebugBaud(Number(e.target.value))}
              style={{ width: "auto", padding: "3px 6px", fontSize: 10 }}
              disabled={debugActive}
            >
              {BAUD_RATES.map((b) => <option key={b} value={b}>{b}</option>)}
            </select>
            {!debugActive ? (
              <button disabled={busy || !connected} onClick={() => { runCmd("debug_start", { baud: debugBaud }); setDebugActive(true); }}>
                Start
              </button>
            ) : (
              <button className="danger" disabled={busy} onClick={() => { runCmd("debug_stop"); setDebugActive(false); }}>
                Stop
              </button>
            )}
          </div>
        </div>
        <div className="console" ref={consoleRef}>
          {log.map((l, i) => (
            <div className="console-line" key={i}>
              <span className="console-time">{l.time}</span>
              <span className="console-msg">{l.text}</span>
            </div>
          ))}
        </div>
        <div className="row" style={{ padding: "8px 12px", borderTop: "1px solid var(--border)" }}>
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
          <button
            disabled={!debugActive || !debugText.trim()}
            onClick={() => { runCmd("debug_send", { text: debugText }); setDebugText(""); }}
          >
            Send
          </button>
        </div>
      </div>
    </>
  );
}

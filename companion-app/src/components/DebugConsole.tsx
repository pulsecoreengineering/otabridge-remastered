import { useEffect, useRef, useState } from "react";
import type { LogLine } from "../types";

const BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 230400, 500000];

// The target device's own serial output — Serial1 on the ESP32's dedicated
// debug UART (GPIO32/33), relayed line-by-line. Separate component from
// ActivityLog on purpose: this is the Arduino's voice, not the app's.
export function DebugConsole({
  lines,
  active,
  disabled,
  onStart,
  onStop,
  onSend,
}: {
  lines: LogLine[];
  active: boolean;
  disabled: boolean;
  onStart: (baud: number) => void;
  onStop: () => void;
  onSend: (text: string) => void;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const [baud, setBaud] = useState(9600);
  const [text, setText] = useState("");

  useEffect(() => {
    ref.current?.scrollTo({ top: ref.current.scrollHeight });
  }, [lines]);

  function send() {
    if (!text.trim()) return;
    onSend(text);
    setText("");
  }

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="panel-title">Target debug console</span>
        <div className="row">
          <select
            value={baud}
            onChange={(e) => setBaud(Number(e.target.value))}
            style={{ width: "auto", padding: "3px 6px", fontSize: 10 }}
            disabled={active}
          >
            {BAUD_RATES.map((b) => <option key={b} value={b}>{b}</option>)}
          </select>
          {!active ? (
            <button disabled={disabled} onClick={() => onStart(baud)}>Start</button>
          ) : (
            <button className="danger" disabled={disabled} onClick={onStop}>Stop</button>
          )}
        </div>
      </div>
      <div className="console" ref={ref}>
        {lines.map((l, i) => (
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
          value={text}
          disabled={!active}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && send()}
        />
        <button disabled={!active || !text.trim()} onClick={send}>Send</button>
      </div>
    </div>
  );
}

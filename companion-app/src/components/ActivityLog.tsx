import { useEffect, useRef } from "react";
import type { LogLine } from "../types";

// Relay/control-plane activity — subscribe confirmations, command acks,
// errors. Deliberately separate from DebugConsole: this is "what the app
// and relay are doing," not the target device's own serial output. Mixing
// the two into one feed made it hard to tell "my command failed" from
// "the target printed something," which is exactly the distinction the
// original two-component design (see project history) was for.
export function ActivityLog({ lines }: { lines: LogLine[] }) {
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    ref.current?.scrollTo({ top: ref.current.scrollHeight });
  }, [lines]);

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="panel-title">Activity</span>
      </div>
      <div className="console" style={{ height: 90 }} ref={ref}>
        {lines.map((l, i) => (
          <div className="console-line" key={i}>
            <span className="console-time">{l.time}</span>
            <span className="console-msg">{l.text}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

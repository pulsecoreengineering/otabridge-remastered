import { useState } from "react";
import { relayApi, setToken, ApiError } from "../api/client";

export function LoginPage({ onAuthenticated }: { onAuthenticated: () => void }) {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function handle(action: "login" | "signup") {
    setError(null);
    setBusy(true);
    try {
      const result = action === "login"
        ? await relayApi.login(email, password)
        : await relayApi.signup(email, password);
      setToken(result.token);
      onAuthenticated();
    } catch (e) {
      setError(e instanceof ApiError ? e.message : "Something went wrong");
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="panel-title">Sign in</span>
      </div>
      <div className="panel-body">
        <div className="field">
          <label className="field-label">Email</label>
          <input
            type="email"
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            disabled={busy}
            autoFocus
          />
        </div>
        <div className="field">
          <label className="field-label">Password</label>
          <input
            type="password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            disabled={busy}
            onKeyDown={(e) => e.key === "Enter" && handle("login")}
          />
        </div>

        <div className="row">
          <button className="primary" style={{ flex: 1 }} disabled={busy} onClick={() => handle("login")}>
            Log in
          </button>
          <button disabled={busy} onClick={() => handle("signup")}>
            Sign up
          </button>
        </div>

        {error && <div className="msg err">{error}</div>}
      </div>
    </div>
  );
}

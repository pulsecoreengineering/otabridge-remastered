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
    <div className="card">
      <h1>Sign in</h1>
      <p className="sub">Your OTABridge devices, from any network.</p>

      <div className="field">
        <label>Email</label>
        <input
          type="email"
          value={email}
          onChange={(e) => setEmail(e.target.value)}
          disabled={busy}
        />
      </div>
      <div className="field">
        <label>Password</label>
        <input
          type="password"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          disabled={busy}
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
  );
}

import { useState } from "react";
import { getToken, type DeviceSummary } from "./api/client";
import { LoginPage } from "./pages/LoginPage";
import { DevicesPage } from "./pages/DevicesPage";
import { DeviceDetailPage } from "./pages/DeviceDetailPage";
import { FleetFlashPage } from "./pages/FleetFlashPage";

type View =
  | { name: "devices" }
  | { name: "device"; deviceId: string }
  | { name: "fleet"; devices: DeviceSummary[] };

function App() {
  const [authed, setAuthed] = useState(() => getToken() !== null);
  const [view, setView] = useState<View>({ name: "devices" });

  return (
    <>
      <header className="app-header">
        <div className="logo">
          <span className="logo-mark">OTABridge</span>
          <div className="logo-sep" />
          <span className="logo-sub">Companion</span>
        </div>
        {authed && <span className="badge online">connected</span>}
      </header>

      {!authed ? (
        <div className="center-shell">
          <div className="auth-card">
            <LoginPage onAuthenticated={() => setAuthed(true)} />
          </div>
        </div>
      ) : view.name === "devices" ? (
        <DevicesPage
          onLogout={() => setAuthed(false)}
          onSelectDevice={(deviceId) => setView({ name: "device", deviceId })}
          onFlashFleet={(devices) => setView({ name: "fleet", devices })}
        />
      ) : view.name === "device" ? (
        <DeviceDetailPage
          deviceId={view.deviceId}
          onBack={() => setView({ name: "devices" })}
        />
      ) : (
        <FleetFlashPage
          devices={view.devices}
          onBack={() => setView({ name: "devices" })}
        />
      )}
    </>
  );
}

export default App;

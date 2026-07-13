import { useState } from "react";
import { getToken } from "./api/client";
import { LoginPage } from "./pages/LoginPage";
import { DevicesPage } from "./pages/DevicesPage";
import { DeviceDetailPage } from "./pages/DeviceDetailPage";

type View = { name: "devices" } | { name: "device"; deviceId: string };

function App() {
  const [authed, setAuthed] = useState(() => getToken() !== null);
  const [view, setView] = useState<View>({ name: "devices" });

  return (
    <>
      <div className="logo">
        <div className="hex" />
        <div className="brand">Pulse<span>Core</span> — OTABridge</div>
      </div>

      {!authed ? (
        <LoginPage onAuthenticated={() => setAuthed(true)} />
      ) : view.name === "devices" ? (
        <DevicesPage
          onLogout={() => setAuthed(false)}
          onSelectDevice={(deviceId) => setView({ name: "device", deviceId })}
        />
      ) : (
        <DeviceDetailPage
          deviceId={view.deviceId}
          onBack={() => setView({ name: "devices" })}
        />
      )}
    </>
  );
}

export default App;

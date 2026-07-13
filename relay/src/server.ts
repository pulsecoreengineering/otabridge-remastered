import Fastify from "fastify";
import cors from "@fastify/cors";
import websocket from "@fastify/websocket";
import { env } from "./env.js";
import { authRoutes } from "./routes/auth.js";
import { deviceRoutes } from "./routes/devices.js";
import { deviceSocketRoute } from "./ws/deviceSocket.js";
import { appSocketRoute } from "./ws/appSocket.js";

const app = Fastify({ logger: true });

// Permissive by design: auth here is a Bearer JWT sent explicitly in the
// Authorization header, not a cookie, so there's no CSRF-via-CORS risk in
// reflecting any origin the way there would be with cookie-based auth.
// Revisit (lock to specific origins) once the companion app/PWA has a fixed
// production domain, if that's ever worth the added deploy friction.
await app.register(cors, { origin: true });

await app.register(websocket);
await app.register(authRoutes);
await app.register(deviceRoutes);
await app.register(deviceSocketRoute);
await app.register(appSocketRoute);

app.get("/health", async () => ({ ok: true }));

app.listen({ port: env.PORT, host: "0.0.0.0" }).catch((err) => {
  app.log.error(err);
  process.exit(1);
});

import Fastify from "fastify";
import cors from "@fastify/cors";
import rateLimit from "@fastify/rate-limit";
import websocket from "@fastify/websocket";
import { env } from "./env.js";
import { authRoutes } from "./routes/auth.js";
import { deviceRoutes } from "./routes/devices.js";
import { pushRoutes } from "./routes/push.js";
import { deviceSocketRoute } from "./ws/deviceSocket.js";
import { appSocketRoute } from "./ws/appSocket.js";

const app = Fastify({ logger: true });

// Permissive by design: auth here is a Bearer JWT sent explicitly in the
// Authorization header, not a cookie, so there's no CSRF-via-CORS risk in
// reflecting any origin the way there would be with cookie-based auth.
// Revisit (lock to specific origins) once the companion app/PWA has a fixed
// production domain, if that's ever worth the added deploy friction.
await app.register(cors, { origin: true });

// global: false — only routes that opt in via `config.rateLimit` are
// limited (currently just /devices/claim; see devices.ts). Left global-off
// rather than setting a blanket default so normal API traffic (companion
// app polling /devices, etc.) is never at risk of tripping a limit we
// didn't specifically reason about.
await app.register(rateLimit, { global: false });

await app.register(websocket);
await app.register(authRoutes);
await app.register(deviceRoutes);
await app.register(pushRoutes);
await app.register(deviceSocketRoute);
await app.register(appSocketRoute);

app.get("/health", async () => ({ ok: true }));

app.listen({ port: env.PORT, host: "0.0.0.0" }).catch((err) => {
  app.log.error(err);
  process.exit(1);
});

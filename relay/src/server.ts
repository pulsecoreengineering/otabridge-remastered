import Fastify from "fastify";
import websocket from "@fastify/websocket";
import { env } from "./env.js";
import { authRoutes } from "./routes/auth.js";
import { deviceRoutes } from "./routes/devices.js";
import { deviceSocketRoute } from "./ws/deviceSocket.js";
import { appSocketRoute } from "./ws/appSocket.js";

const app = Fastify({ logger: true });

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

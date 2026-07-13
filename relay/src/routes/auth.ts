import type { FastifyInstance } from "fastify";
import { z } from "zod";
import { prisma } from "../db.js";
import { hashPassword, verifyPassword, signAccountToken } from "../auth.js";

const credentialsSchema = z.object({
  email: z.string().email(),
  password: z.string().min(8),
});

export async function authRoutes(app: FastifyInstance): Promise<void> {
  app.post("/auth/signup", async (req, reply) => {
    const { email, password } = credentialsSchema.parse(req.body);

    const existing = await prisma.account.findUnique({ where: { email } });
    if (existing) return reply.code(409).send({ error: "email_in_use" });

    const passwordHash = await hashPassword(password);
    const account = await prisma.account.create({ data: { email, passwordHash } });
    return { token: signAccountToken(account.id) };
  });

  app.post("/auth/login", async (req, reply) => {
    const { email, password } = credentialsSchema.parse(req.body);

    const account = await prisma.account.findUnique({ where: { email } });
    if (!account || !(await verifyPassword(account.passwordHash, password))) {
      return reply.code(401).send({ error: "invalid_credentials" });
    }
    return { token: signAccountToken(account.id) };
  });
}

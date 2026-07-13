import { z } from "zod";

const envSchema = z.object({
  DATABASE_URL: z.string(),
  JWT_SECRET: z.string().min(32),
  PORT: z.coerce.number().default(8080),
});

export const env = envSchema.parse(process.env);

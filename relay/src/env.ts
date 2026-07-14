import { z } from "zod";

const envSchema = z.object({
  DATABASE_URL: z.string(),
  JWT_SECRET: z.string().min(32),
  PORT: z.coerce.number().default(8080),
  // Generate with: node -e "console.log(require('web-push').generateVAPIDKeys())"
  VAPID_PUBLIC_KEY: z.string(),
  VAPID_PRIVATE_KEY: z.string(),
  VAPID_SUBJECT: z.string().default("mailto:admin@example.com"),
});

export const env = envSchema.parse(process.env);

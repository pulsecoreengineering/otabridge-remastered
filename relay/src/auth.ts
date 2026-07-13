import argon2 from "argon2";
import jwt from "jsonwebtoken";
import { env } from "./env.js";

export function hashPassword(password: string): Promise<string> {
  return argon2.hash(password);
}

export function verifyPassword(hash: string, password: string): Promise<boolean> {
  return argon2.verify(hash, password);
}

export function signAccountToken(accountId: string): string {
  return jwt.sign({ sub: accountId }, env.JWT_SECRET, { expiresIn: "30d" });
}

export function verifyAccountToken(token: string): string {
  const payload = jwt.verify(token, env.JWT_SECRET) as { sub: string };
  return payload.sub;
}

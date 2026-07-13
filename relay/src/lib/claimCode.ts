import { randomBytes } from "node:crypto";

// Crockford-ish alphabet with 0/O/1/I/L excluded to avoid transcription errors
// when a user reads this off the device's setup page and types it into the app.
const ALPHABET = "23456789ABCDEFGHJKMNPQRSTVWXYZ";
const CODE_LENGTH = 8;

export const CLAIM_CODE_TTL_MS = 15 * 60 * 1000;

export function generateClaimCode(): string {
  const bytes = randomBytes(CODE_LENGTH);
  let code = "";
  for (let i = 0; i < CODE_LENGTH; i++) {
    code += ALPHABET[bytes[i] % ALPHABET.length];
  }
  return `${code.slice(0, 4)}-${code.slice(4)}`;
}

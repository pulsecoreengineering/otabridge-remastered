import * as ed from "@noble/ed25519";
import { randomBytes } from "node:crypto";

export function generateChallenge(): string {
  return randomBytes(32).toString("hex");
}

export async function verifySignature(
  publicKeyHex: string,
  message: string,
  signatureHex: string,
): Promise<boolean> {
  try {
    const publicKey = hexToBytes(publicKeyHex);
    const signature = hexToBytes(signatureHex);
    const msg = new TextEncoder().encode(message);
    return await ed.verifyAsync(signature, msg, publicKey);
  } catch {
    return false;
  }
}

function hexToBytes(hex: string): Uint8Array {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < bytes.length; i++) {
    bytes[i] = parseInt(hex.substr(i * 2, 2), 16);
  }
  return bytes;
}

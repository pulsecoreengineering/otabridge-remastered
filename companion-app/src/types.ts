export type LogLine = { time: string; text: string };

export function nowStamp(): string {
  return new Date().toTimeString().slice(0, 8);
}

import { prisma } from "../db.js";

export async function ownsDevice(accountId: string, deviceId: string): Promise<boolean> {
  const link = await prisma.accountDevice.findUnique({
    where: { accountId_deviceId: { accountId, deviceId } },
  });
  return !!link;
}

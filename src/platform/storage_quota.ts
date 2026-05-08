export type StorageStatus = {
  usage: number;
  quota: number;
  persisted: boolean;
};

export async function getStorageStatus(): Promise<StorageStatus> {
  const estimate = await navigator.storage?.estimate?.();
  const persisted = await navigator.storage?.persisted?.();
  return {
    usage: estimate?.usage ?? 0,
    quota: estimate?.quota ?? 0,
    persisted: persisted ?? false
  };
}

export async function requestPersistentStorage(): Promise<boolean> {
  return (await navigator.storage?.persist?.()) ?? false;
}

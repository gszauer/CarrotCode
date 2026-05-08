import type { Vfs } from "../vfs/types";
import { joinPath, normalizePath } from "../vfs/path";

export type ImportProgress = {
  files: number;
  dirs: number;
  bytes: number;
  currentPath: string;
};

type FileSystemEntryLike = {
  isFile: boolean;
  isDirectory: boolean;
  name: string;
  file?: (success: (file: File) => void, error?: (err: unknown) => void) => void;
  createReader?: () => { readEntries: (success: (entries: FileSystemEntryLike[]) => void, error?: (err: unknown) => void) => void };
};

type DataTransferItemWithEntries = DataTransferItem & {
  getAsFileSystemHandle?: () => Promise<FileSystemHandle>;
  webkitGetAsEntry?: () => FileSystemEntryLike | null;
};

export async function importDataTransfer(vfs: Vfs, items: DataTransferItemList, onProgress?: (progress: ImportProgress) => void): Promise<ImportProgress> {
  const progress: ImportProgress = { files: 0, dirs: 0, bytes: 0, currentPath: "/" };
  for (const item of Array.from(items) as DataTransferItemWithEntries[]) {
    if (item.kind !== "file") continue;
    if (item.getAsFileSystemHandle) {
      const handle = await item.getAsFileSystemHandle();
      await importHandle(vfs, handle, "/", progress, onProgress);
      continue;
    }
    const entry = item.webkitGetAsEntry?.();
    if (entry) {
      await importEntry(vfs, entry, "/", progress, onProgress);
      continue;
    }
    const file = item.getAsFile();
    if (file) await importFile(vfs, file, joinPath("/", file.name), progress, onProgress);
  }
  return progress;
}

export async function importFileList(vfs: Vfs, files: FileList | File[], onProgress?: (progress: ImportProgress) => void): Promise<ImportProgress> {
  const progress: ImportProgress = { files: 0, dirs: 0, bytes: 0, currentPath: "/" };
  for (const file of Array.from(files)) {
    const relative = (file as File & { webkitRelativePath?: string }).webkitRelativePath || file.name;
    await importFile(vfs, file, normalizePath(`/${relative}`), progress, onProgress);
  }
  return progress;
}

async function importHandle(vfs: Vfs, handle: FileSystemHandle, base: string, progress: ImportProgress, onProgress?: (progress: ImportProgress) => void): Promise<void> {
  const path = joinPath(base, handle.name);
  if (handle.kind === "directory") {
    await vfs.mkdir(path);
    progress.dirs++;
    progress.currentPath = path;
    onProgress?.({ ...progress });
    const directory = handle as FileSystemDirectoryHandle & { values: () => AsyncIterable<FileSystemHandle> };
    for await (const child of directory.values()) {
      await importHandle(vfs, child, path, progress, onProgress);
    }
  } else {
    const file = await (handle as FileSystemFileHandle).getFile();
    await importFile(vfs, file, path, progress, onProgress);
  }
}

async function importEntry(vfs: Vfs, entry: FileSystemEntryLike, base: string, progress: ImportProgress, onProgress?: (progress: ImportProgress) => void): Promise<void> {
  const path = joinPath(base, entry.name);
  if (entry.isDirectory && entry.createReader) {
    await vfs.mkdir(path);
    progress.dirs++;
    progress.currentPath = path;
    onProgress?.({ ...progress });
    const reader = entry.createReader();
    while (true) {
      const entries = await new Promise<FileSystemEntryLike[]>((resolve, reject) => reader.readEntries(resolve, reject));
      if (entries.length === 0) break;
      for (const child of entries) await importEntry(vfs, child, path, progress, onProgress);
    }
  } else if (entry.isFile && entry.file) {
    const file = await new Promise<File>((resolve, reject) => entry.file!(resolve, reject));
    await importFile(vfs, file, path, progress, onProgress);
  }
}

async function importFile(vfs: Vfs, file: File, path: string, progress: ImportProgress, onProgress?: (progress: ImportProgress) => void): Promise<void> {
  await vfs.writeFile(path, new Uint8Array(await file.arrayBuffer()), file.type || guessMime(path));
  progress.files++;
  progress.bytes += file.size;
  progress.currentPath = path;
  onProgress?.({ ...progress });
}

function guessMime(path: string): string {
  return path.match(/\.(ts|js|json|md|txt|css|html|lua|cpp|c|h|hpp)$/i) ? "text/plain" : "application/octet-stream";
}

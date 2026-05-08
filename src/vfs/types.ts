export type VfsNodeKind = "file" | "dir";

export type VfsNode = {
  id: string;
  workspaceId: string;
  path: string;
  parentPath: string;
  name: string;
  kind: VfsNodeKind;
  size: number;
  mtime: number;
  contentId?: string;
  mime?: string;
  encoding?: "utf-8" | "binary";
};

export type VfsEvent =
  | { type: "write"; path: string }
  | { type: "remove"; path: string }
  | { type: "rename"; oldPath: string; newPath: string }
  | { type: "mkdir"; path: string };

export interface Vfs {
  readonly workspaceId: string;
  listDir(path: string): Promise<VfsNode[]>;
  listAllFiles(): Promise<VfsNode[]>;
  stat(path: string): Promise<VfsNode | null>;
  readFile(path: string): Promise<Uint8Array>;
  readText(path: string): Promise<string>;
  writeFile(path: string, data: Uint8Array | string, mime?: string): Promise<void>;
  mkdir(path: string): Promise<void>;
  remove(path: string, opts?: { recursive?: boolean }): Promise<void>;
  rename(oldPath: string, newPath: string): Promise<void>;
  watch(listener: (event: VfsEvent) => void): () => void;
}

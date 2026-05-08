import { AppError, uid } from "../shared/types";
import { basename, comparePath, dirname, normalizePath } from "../vfs/path";
import type { Vfs, VfsEvent, VfsNode } from "../vfs/types";
import { ContentRecord, IndexedDbConnection, requestToPromise, WorkspaceRecord, cursorToArray } from "./indexed_db";

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder("utf-8", { fatal: false });
const DEFAULT_WORKSPACE_ID = "default";

export class IndexedVfs implements Vfs {
  readonly workspaceId: string;
  private readonly listeners = new Set<(event: VfsEvent) => void>();

  constructor(private readonly db: IndexedDbConnection, workspaceId: string) {
    this.workspaceId = workspaceId;
  }

  static async openDefault(db = new IndexedDbConnection()): Promise<IndexedVfs> {
    const workspaceId = await db.tx(["workspaces", "nodes", "contents"], "readwrite", async (tx) => {
      const workspaces = tx.objectStore("workspaces");
      const existing = await requestToPromise<WorkspaceRecord | undefined>(workspaces.get(DEFAULT_WORKSPACE_ID));
      if (existing) return existing.id;

      const now = Date.now();
      const workspace: WorkspaceRecord = {
        id: DEFAULT_WORKSPACE_ID,
        name: "Browser Workspace",
        createdAt: now,
        updatedAt: now,
        rootPath: "/",
        source: "empty"
      };
      workspaces.put(workspace);

      const nodes = tx.objectStore("nodes");
      const contents = tx.objectStore("contents");
      const root: VfsNode = {
        id: uid("node"),
        workspaceId: DEFAULT_WORKSPACE_ID,
        path: "/",
        parentPath: "/",
        name: "/",
        kind: "dir",
        size: 0,
        mtime: now
      };
      nodes.put(root);
      return workspace.id;
    });
    return new IndexedVfs(db, workspaceId);
  }

  watch(listener: (event: VfsEvent) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  async listDir(path: string): Promise<VfsNode[]> {
    const parent = normalizePath(path);
    return this.db.tx(["nodes"], "readonly", async (tx) => {
      const index = tx.objectStore("nodes").index("byParent");
      const rows = await cursorToArray<VfsNode>(index.openCursor(IDBKeyRange.only([this.workspaceId, parent])));
      return rows.sort(sortNodes);
    });
  }

  async listAllFiles(): Promise<VfsNode[]> {
    return this.db.tx(["nodes"], "readonly", async (tx) => {
      const index = tx.objectStore("nodes").index("byWorkspace");
      const rows = await cursorToArray<VfsNode>(index.openCursor(IDBKeyRange.only(this.workspaceId)));
      return rows.filter((node) => node.kind === "file").sort((a, b) => comparePath(a.path, b.path));
    });
  }

  async stat(path: string): Promise<VfsNode | null> {
    const p = normalizePath(path);
    return this.db.tx(["nodes"], "readonly", async (tx) => {
      const node = await requestToPromise<VfsNode | undefined>(tx.objectStore("nodes").get([this.workspaceId, p]));
      return node ?? null;
    });
  }

  async readFile(path: string): Promise<Uint8Array> {
    const p = normalizePath(path);
    return this.db.tx(["nodes", "contents"], "readonly", async (tx) => {
      const node = await requestToPromise<VfsNode | undefined>(tx.objectStore("nodes").get([this.workspaceId, p]));
      if (!node || node.kind !== "file" || !node.contentId) throw new AppError("not_found", `File not found: ${p}`);
      const content = await requestToPromise<ContentRecord | undefined>(tx.objectStore("contents").get(node.contentId));
      if (!content) throw new AppError("not_found", `Content missing for: ${p}`);
      return new Uint8Array(content.data.slice(0));
    });
  }

  async readText(path: string): Promise<string> {
    return textDecoder.decode(await this.readFile(path));
  }

  async writeFile(path: string, data: Uint8Array | string, mime = "text/plain"): Promise<void> {
    const p = normalizePath(path);
    await this.db.tx(["nodes", "contents"], "readwrite", async (tx) => {
      await ensureDirRecords(tx.objectStore("nodes"), this.workspaceId, dirname(p));
      await putFileRecords(tx.objectStore("nodes"), tx.objectStore("contents"), this.workspaceId, p, data, mime);
    });
    this.emit({ type: "write", path: p });
  }

  async mkdir(path: string): Promise<void> {
    const p = normalizePath(path);
    await this.db.tx(["nodes"], "readwrite", async (tx) => {
      await ensureDirRecords(tx.objectStore("nodes"), this.workspaceId, p);
    });
    this.emit({ type: "mkdir", path: p });
  }

  async remove(path: string, opts?: { recursive?: boolean }): Promise<void> {
    const p = normalizePath(path);
    await this.db.tx(["nodes", "contents"], "readwrite", async (tx) => {
      const nodes = tx.objectStore("nodes");
      const contents = tx.objectStore("contents");
      const node = await requestToPromise<VfsNode | undefined>(nodes.get([this.workspaceId, p]));
      if (!node) return;
      if (node.kind === "dir") {
        const descendants = await this.getDescendants(nodes, p);
        if (descendants.length > 0 && !opts?.recursive) {
          throw new AppError("not_empty", `Directory is not empty: ${p}`);
        }
        for (const child of descendants) {
          if (child.contentId) contents.delete(child.contentId);
          nodes.delete([this.workspaceId, child.path]);
        }
      }
      if (node.contentId) contents.delete(node.contentId);
      nodes.delete([this.workspaceId, p]);
    });
    this.emit({ type: "remove", path: p });
  }

  async resetToEmpty(): Promise<void> {
    await this.db.tx(["workspaces", "nodes", "contents"], "readwrite", async (tx) => {
      const workspaces = tx.objectStore("workspaces");
      const nodes = tx.objectStore("nodes");
      const contents = tx.objectStore("contents");
      const now = Date.now();
      const workspace = await requestToPromise<WorkspaceRecord | undefined>(workspaces.get(this.workspaceId));
      workspaces.put({
        id: this.workspaceId,
        name: workspace?.name ?? "Browser Workspace",
        createdAt: workspace?.createdAt ?? now,
        updatedAt: now,
        rootPath: "/",
        source: "empty"
      } satisfies WorkspaceRecord);

      const workspaceNodes = await cursorToArray<VfsNode>(nodes.index("byWorkspace").openCursor(IDBKeyRange.only(this.workspaceId)));
      for (const node of workspaceNodes) nodes.delete([this.workspaceId, node.path]);

      const workspaceContents = (await cursorToArray<ContentRecord>(contents.openCursor())).filter((content) => content.workspaceId === this.workspaceId);
      for (const content of workspaceContents) contents.delete(content.contentId);

      nodes.put({
        id: uid("node"),
        workspaceId: this.workspaceId,
        path: "/",
        parentPath: "/",
        name: "/",
        kind: "dir",
        size: 0,
        mtime: now
      } satisfies VfsNode);
    });
    this.emit({ type: "remove", path: "/" });
  }

  async rename(oldPath: string, newPath: string): Promise<void> {
    const oldP = normalizePath(oldPath);
    const newP = normalizePath(newPath);
    await this.db.tx(["nodes", "contents"], "readwrite", async (tx) => {
      const nodes = tx.objectStore("nodes");
      const contents = tx.objectStore("contents");
      const node = await requestToPromise<VfsNode | undefined>(nodes.get([this.workspaceId, oldP]));
      if (!node) throw new AppError("not_found", `Path not found: ${oldP}`);
      const existing = await requestToPromise<VfsNode | undefined>(nodes.get([this.workspaceId, newP]));
      if (existing) throw new AppError("exists", `Path already exists: ${newP}`);
      if (node.kind === "file") {
        if (node.contentId) {
          const content = await requestToPromise<ContentRecord | undefined>(contents.get(node.contentId));
          if (!content) throw new AppError("not_found", `Content missing for: ${oldP}`);
        }
        nodes.put({ ...node, path: newP, parentPath: dirname(newP), name: basename(newP), mtime: Date.now() } satisfies VfsNode);
        nodes.delete([this.workspaceId, oldP]);
        return;
      }
      const descendants = await this.getDescendants(nodes, oldP);
      const now = Date.now();
      for (const item of [node, ...descendants]) {
        const nextPath = item.path === oldP ? newP : normalizePath(`${newP}/${item.path.slice(oldP.length + 1)}`);
        nodes.put({ ...item, path: nextPath, parentPath: dirname(nextPath), name: basename(nextPath), mtime: now } satisfies VfsNode);
      }
      for (const item of [node, ...descendants]) nodes.delete([this.workspaceId, item.path]);
    });
    this.emit({ type: "rename", oldPath: oldP, newPath: newP });
  }

  private async getDescendants(nodes: IDBObjectStore, dir: string): Promise<VfsNode[]> {
    const all = await cursorToArray<VfsNode>(nodes.index("byWorkspace").openCursor(IDBKeyRange.only(this.workspaceId)));
    const prefix = dir === "/" ? "/" : `${dir}/`;
    return all.filter((node) => node.path !== dir && node.path.startsWith(prefix));
  }

  private emit(event: VfsEvent): void {
    for (const listener of this.listeners) listener(event);
  }
}

async function ensureDirRecords(nodes: IDBObjectStore, workspaceId: string, path: string): Promise<void> {
  const p = normalizePath(path);
  if (p === "/") {
    const root = await requestToPromise<VfsNode | undefined>(nodes.get([workspaceId, "/"]));
    if (!root) {
      nodes.put({ id: uid("node"), workspaceId, path: "/", parentPath: "/", name: "/", kind: "dir", size: 0, mtime: Date.now() } satisfies VfsNode);
    }
    return;
  }
  await ensureDirRecords(nodes, workspaceId, dirname(p));
  const existing = await requestToPromise<VfsNode | undefined>(nodes.get([workspaceId, p]));
  if (!existing) {
    nodes.put({ id: uid("node"), workspaceId, path: p, parentPath: dirname(p), name: basename(p), kind: "dir", size: 0, mtime: Date.now() } satisfies VfsNode);
  }
}

async function putFileRecords(nodes: IDBObjectStore, contents: IDBObjectStore, workspaceId: string, path: string, data: Uint8Array | string, mime: string): Promise<void> {
  const p = normalizePath(path);
  await ensureDirRecords(nodes, workspaceId, dirname(p));
  const bytes = typeof data === "string" ? textEncoder.encode(data) : data;
  const contentId = uid("content");
  const dataBuffer = new ArrayBuffer(bytes.byteLength);
  new Uint8Array(dataBuffer).set(bytes);
  const content: ContentRecord = {
    contentId,
    workspaceId,
    data: dataBuffer,
    size: bytes.byteLength
  };
  contents.put(content);
  const node: VfsNode = {
    id: uid("node"),
    workspaceId,
    path: p,
    parentPath: dirname(p),
    name: basename(p),
    kind: "file",
    size: bytes.byteLength,
    mtime: Date.now(),
    contentId,
    mime,
    encoding: mime.startsWith("text/") || p.match(/\.(ts|js|json|md|txt|css|html|lua|cpp|c|h|hpp)$/i) ? "utf-8" : "binary"
  };
  nodes.put(node);
}

function sortNodes(a: VfsNode, b: VfsNode): number {
  if (a.kind !== b.kind) return a.kind === "dir" ? -1 : 1;
  return comparePath(a.name, b.name);
}

import { AppError } from "../shared/types";

export const DB_NAME = "slug-editor";
export const DB_VERSION = 1;

export type WorkspaceRecord = {
  id: string;
  name: string;
  createdAt: number;
  updatedAt: number;
  rootPath: "/";
  source: "empty" | "sample" | "drag-drop" | "file-picker" | "restored";
};

export type ContentRecord = {
  contentId: string;
  workspaceId: string;
  data: ArrayBuffer;
  size: number;
};

export class IndexedDbConnection {
  private dbPromise: Promise<IDBDatabase> | null = null;

  constructor(private readonly dbName = DB_NAME) {}

  open(): Promise<IDBDatabase> {
    if (this.dbPromise) return this.dbPromise;
    this.dbPromise = new Promise((resolve, reject) => {
      const request = indexedDB.open(this.dbName, DB_VERSION);
      request.onupgradeneeded = () => {
        const db = request.result;
        if (!db.objectStoreNames.contains("meta")) {
          db.createObjectStore("meta");
        }
        if (!db.objectStoreNames.contains("workspaces")) {
          db.createObjectStore("workspaces", { keyPath: "id" });
        }
        if (!db.objectStoreNames.contains("nodes")) {
          const nodes = db.createObjectStore("nodes", { keyPath: ["workspaceId", "path"] });
          nodes.createIndex("byWorkspace", "workspaceId", { unique: false });
          nodes.createIndex("byParent", ["workspaceId", "parentPath"], { unique: false });
        }
        if (!db.objectStoreNames.contains("contents")) {
          db.createObjectStore("contents", { keyPath: "contentId" });
        }
        if (!db.objectStoreNames.contains("documents")) {
          db.createObjectStore("documents", { keyPath: "docId" });
        }
        if (!db.objectStoreNames.contains("layout")) {
          db.createObjectStore("layout", { keyPath: "workspaceId" });
        }
        if (!db.objectStoreNames.contains("chatThreads")) {
          db.createObjectStore("chatThreads", { keyPath: "threadId" });
        }
        if (!db.objectStoreNames.contains("chatItems")) {
          db.createObjectStore("chatItems", { keyPath: ["threadId", "index"] });
        }
      };
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(new AppError("indexed_db_open", request.error?.message ?? "Could not open IndexedDB"));
    });
    return this.dbPromise;
  }

  async tx<T>(stores: string[], mode: IDBTransactionMode, fn: (tx: IDBTransaction) => Promise<T> | T): Promise<T> {
    const db = await this.open();
    return new Promise<T>((resolve, reject) => {
      const tx = db.transaction(stores, mode);
      let settled = false;
      const finish = (value: T) => {
        settled = true;
        resolve(value);
      };
      tx.onerror = () => reject(new AppError("indexed_db_tx", tx.error?.message ?? "IndexedDB transaction failed"));
      tx.onabort = () => reject(new AppError("indexed_db_abort", tx.error?.message ?? "IndexedDB transaction aborted"));
      tx.oncomplete = () => {
        if (!settled) resolve(undefined as T);
      };
      Promise.resolve(fn(tx)).then(finish, reject);
    });
  }
}

export function requestToPromise<T>(request: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(new AppError("indexed_db_request", request.error?.message ?? "IndexedDB request failed"));
  });
}

export function cursorToArray<T>(request: IDBRequest<IDBCursorWithValue | null>): Promise<T[]> {
  return new Promise((resolve, reject) => {
    const result: T[] = [];
    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor) {
        resolve(result);
        return;
      }
      result.push(cursor.value as T);
      cursor.continue();
    };
    request.onerror = () => reject(new AppError("indexed_db_cursor", request.error?.message ?? "IndexedDB cursor failed"));
  });
}

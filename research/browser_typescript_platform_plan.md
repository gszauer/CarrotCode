# Browser TypeScript Platform Plan

Research date: 2026-04-29

This addendum locks the app platform: the editor is a TypeScript browser application rendered in a browser window. The browser is the host. WebGL2 renders the editor and UI; IndexedDB backs the virtual filesystem; drag/drop imports files and folders into that virtual filesystem.

For keyboard, clipboard, iOS virtual keyboard, IME, and high-DPI handling, see `input_clipboard_mobile_dpi_plan.md`. The important platform decision is that the visible editor remains WebGL2, but the browser host also owns a hidden native textarea used as an input bridge.

## Platform Decision

The v1 application is:

- TypeScript.
- Browser-window based.
- Single WebGL2 canvas for the main editor UI.
- No DOM editor widget, CodeMirror, Monaco, React, or canvas 2D editor surface.
- Browser APIs for file import/export and persistence.
- IndexedDB-backed virtual filesystem as the baseline storage layer.

Optional browser features can improve the experience, but the baseline must work with only:

- `File`, `Blob`, and `FileReader`/`Blob.text()` style reads.
- HTML drag/drop `DataTransfer` / `DataTransferItem`.
- `<input type="file" multiple>` and `<input type="file" webkitdirectory>`.
- IndexedDB.

MDN describes IndexedDB as a low-level browser API for storing significant amounts of structured client-side data, including files and blobs, with asynchronous transactions and same-origin storage (https://developer.mozilla.org/docs/Web/API/IndexedDB_API). This matches the editor's virtual workspace needs.

## Browser Host Modules

Recommended module tree:

```text
src/
  platform/
    browser/
      browser_host.ts
      input_bridge.ts
      drag_drop.ts
      file_import.ts
      file_export.ts
      indexed_db.ts
      indexed_vfs.ts
      storage_quota.ts
      viewport.ts
      file_system_access.ts
      download.ts
      mime.ts
  vfs/
    path.ts
    types.ts
    vfs.ts
    snapshots.ts
    encoding.ts
```

The rest of the editor should depend on `Vfs` and `BrowserHost`, not directly on IndexedDB or drag/drop.

## Virtual Filesystem Contract

Expose a small async filesystem interface:

```ts
type VfsNodeKind = "file" | "dir";

type VfsNode = {
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
  deleted?: boolean;
};

interface Vfs {
  listDir(path: string): Promise<VfsNode[]>;
  stat(path: string): Promise<VfsNode | null>;
  readFile(path: string): Promise<Uint8Array>;
  readText(path: string): Promise<string>;
  writeFile(path: string, data: Uint8Array | string): Promise<void>;
  mkdir(path: string): Promise<void>;
  remove(path: string, opts?: { recursive?: boolean }): Promise<void>;
  rename(oldPath: string, newPath: string): Promise<void>;
  watch?(listener: (event: VfsEvent) => void): () => void;
}
```

Use POSIX-like normalized paths internally:

- Always `/` separators.
- Workspace root is `/`.
- No `..` escape above root.
- Preserve display names but normalize lookup keys.
- Case sensitivity should be configurable later; v1 can be case-sensitive.

## IndexedDB Schema

Database: `slug-editor`

Versioned object stores:

```text
meta
  key: string
  value: unknown

workspaces
  id: string
  name: string
  createdAt: number
  updatedAt: number
  rootPath: "/"
  source: "drag-drop" | "file-picker" | "sample" | "restored"

nodes
  key: [workspaceId, path]
  indexes:
    byWorkspace
    byParent: [workspaceId, parentPath]
  value: VfsNode

contents
  key: contentId
  value:
    workspaceId: string
    data: Blob | ArrayBuffer
    size: number
    sha256?: string

documents
  key: docId
  value:
    workspaceId: string
    path?: string
    revision: number
    savedRevision: number
    dirty: boolean
    text: string
    selections: Selection[]
    scroll: {x: number, y: number}

layout
  key: workspaceId
  value:
    openTabs: ...
    splits: ...
    sidebarMode: "files" | "search" | "chat"
    sidebarWidth: number

chatThreads
  key: threadId
  value: ChatThread metadata

chatItems
  key: [threadId, index]
  value: TranscriptItem

fileHandles
  key: handleId
  value: FileSystemHandle metadata and handle, if supported
```

IndexedDB stores values through structured clone. MDN notes that IndexedDB can store structured-clone-supported objects (https://developer.mozilla.org/docs/Web/API/IndexedDB_API), and MDN's structured clone documentation lists `FileSystemFileHandle` as serializable where that API is supported (https://developer.mozilla.org/docs/Web/API/Web_Workers_API/Structured_clone_algorithm). Treat persisted handles as optional and permission-gated.

## Storage Quota And Persistence

Use the Storage API:

```ts
const estimate = await navigator.storage?.estimate?.();
const persisted = await navigator.storage?.persisted?.();
const granted = await navigator.storage?.persist?.();
```

MDN's quota guide says browser-managed storage covers IndexedDB, Cache API, and Origin Private File System, and writes can fail with `QuotaExceededError` when an origin exceeds quota (https://developer.mozilla.org/en-US/docs/Web/API/Storage_API/Storage_quotas_and_eviction_criteria). The app should:

- Show approximate workspace size.
- Request persistent storage after first real project import.
- Catch `QuotaExceededError` and show a recoverable error.
- Provide export/backup.
- Avoid caching generated renderer data in IndexedDB until the core VFS is stable.

## Drag And Drop Import

Support dropping:

- individual files
- multiple files
- folders where browser APIs expose directory structure
- zip files later, if we write a no-dependency zip reader

Event flow:

```ts
dropZone.addEventListener("dragover", (e) => {
  e.preventDefault();
  e.dataTransfer!.dropEffect = "copy";
});

dropZone.addEventListener("drop", async (e) => {
  e.preventDefault();
  const items = [...e.dataTransfer!.items];
  await importer.importDataTransferItems(items);
});
```

MDN describes drag data as `DataTransferItem` objects of kind `string` or `file`; file payloads can be retrieved with `getAsFile()`, and more complex file-system operations may use `getAsFileSystemHandle()` or `webkitGetAsEntry()` where available (https://developer.mozilla.org/docs/Web/API/HTML_Drag_and_Drop_API/Recommended_drag_types).

Import strategy:

1. Prefer `DataTransferItem.getAsFileSystemHandle()` if available.
2. Else use `webkitGetAsEntry()` for folder recursion where available.
3. Else use `DataTransferItem.getAsFile()` / `dataTransfer.files`.
4. For browsers without directory drop support, offer `<input type="file" webkitdirectory>`.

Important behavior:

- Drag/drop imports a copy into the virtual workspace.
- It does not automatically keep a live link to disk.
- For large projects, import in a cooperative job and update progress.
- Detect binary files and store as binary; do not open in text editor by default.
- Normalize paths and handle collisions with `overwrite`, `keep both`, or `skip`.

## Folder Import

Directory recursion adapters:

```ts
type ImportedEntry =
  | { kind: "file"; path: string; file: File }
  | { kind: "dir"; path: string };
```

Adapters:

- `fromDataTransferItems(items): AsyncGenerator<ImportedEntry>`
- `fromFileList(files): AsyncGenerator<ImportedEntry>`
- `fromFileSystemDirectoryHandle(handle): AsyncGenerator<ImportedEntry>`
- `fromWebkitEntry(entry): AsyncGenerator<ImportedEntry>`

The importer writes entries to `Vfs` in batches:

- Create directories first or lazily before each file write.
- Store file data as `Blob` or `ArrayBuffer`.
- For likely text files, also cache decoded UTF-8 text lazily when opened.
- Yield every N files or M milliseconds to keep the UI responsive.

## File System Access API

The File System Access API is optional. MDN describes `showOpenFilePicker()` and `showDirectoryPicker()` as ways to obtain user-granted handles, and notes the API requires secure contexts and explicit user permission (https://developer.mozilla.org/en-US/docs/Web/API/File_System_API).

Use it only as an enhancement:

- `Open Folder` can call `window.showDirectoryPicker()` when present.
- `Save to disk` can write through a `FileSystemFileHandle` when present.
- Handles stored in IndexedDB should be rechecked with `queryPermission()` / `requestPermission()`.
- If permission is denied or the API is absent, fall back to VFS import/export.

Do not make File System Access mandatory for v1. Firefox/Safari support and permission behavior can differ, and the virtual filesystem must be the reliable baseline.

## Origin Private File System

Origin Private File System is optional for later large-file performance. It can provide file-like storage scoped to the origin, but IndexedDB is simpler to inspect, migrate, and use for metadata plus content records. Start with IndexedDB.

Potential later split:

- IndexedDB for metadata, layout, chat, text documents.
- OPFS for large binary file contents and snapshots.

## File Export

Support export paths:

- Download a single file with `<a download>` and a `Blob` URL.
- Export selected folder/workspace as a zip. GabCode already uses a minimal no-compression zip writer in its web shell; we can write the same kind of STORED zip writer if needed.
- If File System Access is available and permission is granted, save changed files back to their handles.

V1 should support:

- Export current file.
- Export full virtual workspace as a zip.

## Editor Integration

Document open flow:

```text
FilesPanel click
  -> Vfs.readText(path)
  -> DocumentStore.open(path, text, revisionFromVfs)
  -> RootView.openDoc(doc)
```

Save flow:

```text
Command save
  -> DocumentStore.serialize(doc)
  -> Vfs.writeFile(doc.path, text)
  -> doc.savedRevision = doc.revision
  -> mark clean
```

Assistant edits:

```text
propose_file_edit
  -> PendingAction with base path + base document/VFS revision
  -> diff card
  -> user approves
  -> if open doc: apply to Document
  -> else: apply to VFS content
  -> save state remains explicit
```

For dirty open files, assistant proposals should target the in-memory `Document`, not stale IndexedDB content.

## TypeScript Interfaces

Use branded IDs to avoid mixing handles:

```ts
type WorkspaceId = string & { readonly __brand: "WorkspaceId" };
type DocId = string & { readonly __brand: "DocId" };
type ThreadId = string & { readonly __brand: "ThreadId" };
type ContentId = string & { readonly __brand: "ContentId" };
```

All platform APIs should return `Result<T>`-style objects or throw typed `PlatformError`s. Do not let raw DOMException names leak throughout editor code.

```ts
type PlatformErrorCode =
  | "not_found"
  | "permission_denied"
  | "quota_exceeded"
  | "encoding_error"
  | "unsupported"
  | "aborted"
  | "unknown";
```

## OpenAI Key Constraint In Browser

A pure browser app cannot keep a production OpenAI API key secret from the user or page runtime. For development, a user-supplied key can be stored locally if the user accepts the risk. For a distributed app, use one of:

- a local companion process/proxy
- a hosted backend that owns API keys
- user-provided API keys stored in browser storage with clear warnings

This does not change the editor architecture: `ChatHarness` should depend on `ModelTransport`, and `OpenAIResponsesTransport` can be implemented either in-browser or through a proxy.

## Implementation Order

1. `IndexedDbConnection` with schema migration.
2. `IndexedVfs` with `listDir`, `readText`, `writeFile`, `mkdir`, `remove`.
3. File import from `<input type=file multiple>`.
4. Drag/drop file import.
5. Directory import using `webkitdirectory` and `webkitRelativePath`.
6. Optional directory drag recursion through `webkitGetAsEntry()`.
7. Optional File System Access adapter.
8. Workspace export as single file, then zip.
9. Storage quota display and persistent-storage request.
10. Hook Files panel to VFS.
11. Hook Search panel to VFS.
12. Hook Chat tools to VFS and open documents.

## V1 Decisions

- Browser TypeScript is the target.
- IndexedDB VFS is the persistence baseline.
- Drag/drop imports into VFS.
- Folder import is required through the best available browser path.
- File System Access is optional enhancement, not a dependency.
- OpenAI transport must be abstracted so browser direct calls and proxy calls both fit.
- A local workspace export path is required so users can recover data if browser storage is cleared.

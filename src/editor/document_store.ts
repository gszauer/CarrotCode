import type { Vfs } from "../vfs/types";
import { normalizePath } from "../vfs/path";
import { syntaxFromPath, TextDocument } from "./document";
import { isUnsupportedFilePath, UNSUPPORTED_FILE_TEXT } from "./file_types";

export class DocumentStore {
  private readonly docsById = new Map<string, TextDocument>();
  private readonly docsByPath = new Map<string, TextDocument>();

  constructor(private readonly vfs: Vfs) {}

  all(): TextDocument[] {
    return [...this.docsById.values()];
  }

  clear(): void {
    this.docsById.clear();
    this.docsByPath.clear();
  }

  get(id: string): TextDocument | undefined {
    return this.docsById.get(id);
  }

  getByPath(path: string): TextDocument | undefined {
    return this.docsByPath.get(normalizePath(path));
  }

  async open(path: string): Promise<TextDocument> {
    const normalized = normalizePath(path);
    const existing = this.docsByPath.get(normalized);
    if (existing) return existing;
    const doc = isUnsupportedFilePath(normalized)
      ? new TextDocument(normalized, UNSUPPORTED_FILE_TEXT)
      : new TextDocument(normalized, await this.vfs.readText(normalized));
    doc.readOnly = isUnsupportedFilePath(normalized);
    doc.markSaved();
    this.docsById.set(doc.id, doc);
    this.docsByPath.set(normalized, doc);
    return doc;
  }

  createUntitled(text = ""): TextDocument {
    const doc = new TextDocument(undefined, text);
    this.docsById.set(doc.id, doc);
    return doc;
  }

  createVirtual(path: string, text: string): TextDocument {
    const normalized = normalizePath(path);
    const existing = this.docsByPath.get(normalized);
    if (existing) {
      existing.selectAll();
      existing.replaceSelection(text, "virtual");
      existing.path = normalized;
      existing.syntaxId = syntaxFromPath(normalized);
      existing.readOnly = false;
      existing.markSaved();
      return existing;
    }
    const doc = new TextDocument(normalized, text);
    doc.markSaved();
    this.docsById.set(doc.id, doc);
    this.docsByPath.set(normalized, doc);
    return doc;
  }

  async save(doc: TextDocument): Promise<void> {
    if (doc.readOnly) {
      doc.markSaved();
      return;
    }
    if (!doc.path) {
      doc.path = `/untitled-${Date.now().toString(36)}.txt`;
      this.docsByPath.set(doc.path, doc);
    }
    await this.vfs.writeFile(doc.path, doc.getText(), "text/plain");
    doc.markSaved();
  }

  async saveAs(doc: TextDocument, path: string): Promise<void> {
    const normalized = normalizePath(path);
    if (doc.path && doc.path !== normalized) this.docsByPath.delete(doc.path);
    doc.path = normalized;
    doc.syntaxId = syntaxFromPath(normalized);
    doc.readOnly = isUnsupportedFilePath(normalized);
    this.docsByPath.set(normalized, doc);
    if (!doc.readOnly) await this.vfs.writeFile(normalized, doc.getText(), "text/plain");
    doc.markSaved();
  }

  renamePath(oldPath: string, newPath: string): TextDocument | undefined {
    const oldNormalized = normalizePath(oldPath);
    const newNormalized = normalizePath(newPath);
    const doc = this.docsByPath.get(oldNormalized);
    if (!doc) return undefined;
    this.docsByPath.delete(oldNormalized);
    doc.path = newNormalized;
    doc.syntaxId = syntaxFromPath(newNormalized);
    doc.readOnly = isUnsupportedFilePath(newNormalized);
    this.docsByPath.set(newNormalized, doc);
    return doc;
  }

  removePath(path: string): TextDocument | undefined {
    const normalized = normalizePath(path);
    const doc = this.docsByPath.get(normalized);
    if (!doc) return undefined;
    this.docsByPath.delete(normalized);
    this.docsById.delete(doc.id);
    return doc;
  }

  remove(id: string): TextDocument | undefined {
    const doc = this.docsById.get(id);
    if (!doc) return undefined;
    this.docsById.delete(id);
    if (doc.path) this.docsByPath.delete(normalizePath(doc.path));
    return doc;
  }
}

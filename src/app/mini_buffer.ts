import type { CursorCommand } from "../editor/document";

type MiniBufferSnapshot = { text: string; cursor: number; anchor: number };
const MAX_MINI_BUFFER_UNDO = 200;

export class MiniBuffer {
  text = "";
  cursor = 0;
  anchor = 0;
  scrollX = 0;
  private readonly undoStack: MiniBufferSnapshot[] = [];
  private readonly redoStack: MiniBufferSnapshot[] = [];

  constructor(text = "") {
    this.text = text;
    this.cursor = text.length;
    this.anchor = this.cursor;
  }

  hasSelection(): boolean {
    return this.cursor !== this.anchor;
  }

  selectedText(): string {
    const [a, b] = this.ordered();
    return this.text.slice(a, b);
  }

  replaceSelection(text: string): void {
    const before = this.snapshot();
    const [a, b] = this.ordered();
    this.text = this.text.slice(0, a) + text + this.text.slice(b);
    this.cursor = a + text.length;
    this.anchor = this.cursor;
    this.recordEdit(before);
  }

  deleteBackward(): void {
    if (this.hasSelection()) {
      this.replaceSelection("");
      return;
    }
    if (this.cursor === 0) return;
    const before = this.snapshot();
    this.text = this.text.slice(0, this.cursor - 1) + this.text.slice(this.cursor);
    this.cursor--;
    this.anchor = this.cursor;
    this.recordEdit(before);
  }

  deleteForward(): void {
    if (this.hasSelection()) {
      this.replaceSelection("");
      return;
    }
    if (this.cursor >= this.text.length) return;
    const before = this.snapshot();
    this.text = this.text.slice(0, this.cursor) + this.text.slice(this.cursor + 1);
    this.recordEdit(before);
  }

  move(command: CursorCommand, extend = false): void {
    let next = this.cursor;
    if (command === "left") next = Math.max(0, this.cursor - 1);
    else if (command === "right") next = Math.min(this.text.length, this.cursor + 1);
    else if (command === "lineStart" || command === "docStart") next = 0;
    else if (command === "lineEnd" || command === "docEnd") next = this.text.length;
    else if (command === "wordLeft") next = wordLeft(this.text, this.cursor);
    else if (command === "wordRight") next = wordRight(this.text, this.cursor);
    this.cursor = next;
    if (!extend) this.anchor = this.cursor;
  }

  selectAll(): void {
    this.anchor = 0;
    this.cursor = this.text.length;
  }

  undo(): void {
    const previous = this.undoStack.pop();
    if (!previous) return;
    this.redoStack.push(this.snapshot());
    this.restore(previous);
  }

  redo(): void {
    const next = this.redoStack.pop();
    if (!next) return;
    this.undoStack.push(this.snapshot());
    this.restore(next);
  }

  canUndo(): boolean {
    return this.undoStack.length > 0;
  }

  canRedo(): boolean {
    return this.redoStack.length > 0;
  }

  clearUndoHistory(): void {
    this.undoStack.length = 0;
    this.redoStack.length = 0;
  }

  private ordered(): [number, number] {
    return this.anchor <= this.cursor ? [this.anchor, this.cursor] : [this.cursor, this.anchor];
  }

  private snapshot(): MiniBufferSnapshot {
    return { text: this.text, cursor: this.cursor, anchor: this.anchor };
  }

  private restore(snapshot: MiniBufferSnapshot): void {
    this.text = snapshot.text;
    this.cursor = Math.min(snapshot.cursor, this.text.length);
    this.anchor = Math.min(snapshot.anchor, this.text.length);
  }

  private recordEdit(before: MiniBufferSnapshot): void {
    if (before.text === this.text && before.cursor === this.cursor && before.anchor === this.anchor) return;
    this.undoStack.push(before);
    if (this.undoStack.length > MAX_MINI_BUFFER_UNDO) this.undoStack.shift();
    this.redoStack.length = 0;
  }
}

function wordLeft(text: string, cursor: number): number {
  let i = cursor;
  while (i > 0 && /\s/.test(text.charAt(i - 1))) i--;
  while (i > 0 && /\w/.test(text.charAt(i - 1))) i--;
  return i;
}

function wordRight(text: string, cursor: number): number {
  let i = cursor;
  while (i < text.length && /\s/.test(text.charAt(i))) i++;
  while (i < text.length && /\w/.test(text.charAt(i))) i++;
  return i;
}

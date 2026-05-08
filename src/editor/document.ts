import { clamp, uid } from "../shared/types";

export type Position = { line: number; col: number };
export type Selection = { anchor: Position; head: Position };
type EditKind = "word" | "space" | "delimiter" | "delete";

type UndoCommand =
  | { type: "selection"; time: number; group: number; selection: Selection }
  | { type: "insert"; time: number; group: number; pos: Position; text: string }
  | { type: "remove"; time: number; group: number; start: Position; end: Position };

const UNDO_MERGE_TIMEOUT_MS = 300;
const MAX_UNDO_COMMANDS = 10000;

export class TextDocument {
  readonly id: string;
  path: string | undefined;
  lines: string[];
  revision = 0;
  savedRevision = 0;
  syntaxId = "plain";
  readOnly = false;
  selection: Selection = { anchor: { line: 0, col: 0 }, head: { line: 0, col: 0 } };
  private readonly undoStack: UndoCommand[] = [];
  private readonly redoStack: UndoCommand[] = [];
  private undoGroup = 1;
  private lastEditKind: EditKind | null = null;

  constructor(path: string | undefined, text: string) {
    this.id = uid("doc");
    this.path = path;
    this.lines = splitText(text);
    this.syntaxId = syntaxFromPath(path);
  }

  get dirty(): boolean {
    return this.revision !== this.savedRevision;
  }

  getText(): string {
    return this.lines.join("\n");
  }

  markSaved(): void {
    this.savedRevision = this.revision;
  }

  setSelection(anchor: Position, head = anchor): void {
    this.selection = { anchor: this.clampPosition(anchor), head: this.clampPosition(head) };
  }

  getOrderedSelection(): { start: Position; end: Position } {
    return comparePosition(this.selection.anchor, this.selection.head) <= 0
      ? { start: this.selection.anchor, end: this.selection.head }
      : { start: this.selection.head, end: this.selection.anchor };
  }

  hasSelection(): boolean {
    return comparePosition(this.selection.anchor, this.selection.head) !== 0;
  }

  selectedText(): string {
    if (!this.hasSelection()) return "";
    const { start, end } = this.getOrderedSelection();
    if (start.line === end.line) {
      return this.lines[start.line]!.slice(start.col, end.col);
    }
    const parts = [this.lines[start.line]!.slice(start.col)];
    for (let line = start.line + 1; line < end.line; line++) parts.push(this.lines[line]!);
    parts.push(this.lines[end.line]!.slice(0, end.col));
    return parts.join("\n");
  }

  replaceSelection(text: string, _label = "insert"): void {
    const time = performance.now();
    const group = this.nextUndoGroup(editKindForInsert(text), time, this.undoStack);
    this.redoStack.length = 0;
    const { start, end } = this.getOrderedSelection();
    let pos = start;
    if (comparePosition(start, end) !== 0) {
      this.rawRemove(start, end, this.undoStack, time, group);
      pos = start;
    }
    if (text) pos = this.rawInsert(pos, text, this.undoStack, time, group);
    this.setSelection(pos);
  }

  deleteBackward(unit: "char" | "word" | "line" = "char"): void {
    if (this.hasSelection()) {
      this.replaceSelection("", "delete");
      return;
    }
    const pos = this.selection.head;
    if (pos.line === 0 && pos.col === 0) return;
    let start: Position;
    if (unit === "line") {
      start = { line: pos.line, col: 0 };
    } else if (unit === "word") {
      start = this.wordBoundaryBackward(pos);
    } else if (pos.col > 0) {
      start = { line: pos.line, col: previousCodePointCol(this.lines[pos.line]!, pos.col) };
    } else {
      start = { line: pos.line - 1, col: this.lines[pos.line - 1]!.length };
    }
    const time = performance.now();
    const group = this.nextUndoGroup("delete", time, this.undoStack);
    this.redoStack.length = 0;
    this.rawRemove(start, pos, this.undoStack, time, group);
    this.setSelection(start);
  }

  deleteForward(unit: "char" | "word" | "line" = "char"): void {
    if (this.hasSelection()) {
      this.replaceSelection("", "delete");
      return;
    }
    const pos = this.selection.head;
    const lastLine = this.lines.length - 1;
    if (pos.line === lastLine && pos.col === this.lines[lastLine]!.length) return;
    let end: Position;
    if (unit === "line") {
      end = { line: pos.line, col: this.lines[pos.line]!.length };
    } else if (unit === "word") {
      end = this.wordBoundaryForward(pos);
    } else if (pos.col < this.lines[pos.line]!.length) {
      end = { line: pos.line, col: nextCodePointCol(this.lines[pos.line]!, pos.col) };
    } else {
      end = { line: pos.line + 1, col: 0 };
    }
    const time = performance.now();
    const group = this.nextUndoGroup("delete", time, this.undoStack);
    this.redoStack.length = 0;
    this.rawRemove(pos, end, this.undoStack, time, group);
    this.setSelection(pos);
  }

  move(command: CursorCommand, extend = false): void {
    const current = this.selection.head;
    const next = this.resolveMove(current, command);
    this.selection = extend ? { anchor: this.selection.anchor, head: next } : { anchor: next, head: next };
  }

  selectAll(): void {
    const endLine = this.lines.length - 1;
    this.setSelection({ line: 0, col: 0 }, { line: endLine, col: this.lines[endLine]!.length });
  }

  indentSelectedLines(indent = "  "): void {
    if (!this.hasSelection()) {
      this.replaceSelection(indent, "indent");
      return;
    }
    this.redoStack.length = 0;
    const time = performance.now();
    const group = this.nextUndoGroup("delimiter", time, this.undoStack);
    const range = this.selectedLineRange();
    const selection = cloneSelection(this.selection);
    for (let line = range.start; line <= range.end; line++) this.rawInsert({ line, col: 0 }, indent, this.undoStack, time, group);
    this.selection = selection;
    this.selection = {
      anchor: adjustPositionByLinePrefix(this.selection.anchor, range.start, range.end, indent.length),
      head: adjustPositionByLinePrefix(this.selection.head, range.start, range.end, indent.length)
    };
  }

  unindentSelectedLines(indentWidth = 2): void {
    const range = this.hasSelection() ? this.selectedLineRange() : { start: this.selection.head.line, end: this.selection.head.line };
    const removals = new Map<number, number>();
    for (let line = range.start; line <= range.end; line++) {
      const text = this.lines[line]!;
      const count = text.startsWith("\t") ? 1 : Math.min(indentWidth, leadingSpaces(text));
      if (count > 0) removals.set(line, count);
    }
    if (removals.size === 0) return;
    this.redoStack.length = 0;
    const time = performance.now();
    const group = this.nextUndoGroup("delimiter", time, this.undoStack);
    const selection = cloneSelection(this.selection);
    for (const [line, count] of removals) this.rawRemove({ line, col: 0 }, { line, col: count }, this.undoStack, time, group);
    this.selection = selection;
    this.selection = {
      anchor: adjustPositionByLineRemovals(this.selection.anchor, removals),
      head: adjustPositionByLineRemovals(this.selection.head, removals)
    };
  }

  undo(): void {
    this.popUndo(this.undoStack, this.redoStack);
  }

  redo(): void {
    this.popUndo(this.redoStack, this.undoStack);
  }

  canUndo(): boolean {
    return this.undoStack.length > 0;
  }

  canRedo(): boolean {
    return this.redoStack.length > 0;
  }

  lineCount(): number {
    return this.lines.length;
  }

  clampPosition(pos: Position): Position {
    const line = clamp(Math.trunc(pos.line), 0, this.lines.length - 1);
    const col = clamp(Math.trunc(pos.col), 0, this.lines[line]!.length);
    return { line, col };
  }

  private resolveMove(pos: Position, command: CursorCommand): Position {
    switch (command) {
      case "left":
        return pos.col > 0 ? { line: pos.line, col: previousCodePointCol(this.lines[pos.line]!, pos.col) } : this.clampPosition({ line: pos.line - 1, col: Number.MAX_SAFE_INTEGER });
      case "right":
        return pos.col < this.lines[pos.line]!.length ? { line: pos.line, col: nextCodePointCol(this.lines[pos.line]!, pos.col) } : this.clampPosition({ line: pos.line + 1, col: 0 });
      case "up":
        return this.clampPosition({ line: pos.line - 1, col: pos.col });
      case "down":
        return this.clampPosition({ line: pos.line + 1, col: pos.col });
      case "lineStart":
        return { line: pos.line, col: 0 };
      case "lineEnd":
        return { line: pos.line, col: this.lines[pos.line]!.length };
      case "docStart":
        return { line: 0, col: 0 };
      case "docEnd": {
        const line = this.lines.length - 1;
        return { line, col: this.lines[line]!.length };
      }
      case "wordLeft":
        return this.wordBoundaryBackward(pos);
      case "wordRight":
        return this.wordBoundaryForward(pos);
    }
  }

  private selectedLineRange(): { start: number; end: number } {
    const ordered = this.getOrderedSelection();
    const end = ordered.end.col === 0 && ordered.end.line > ordered.start.line ? ordered.end.line - 1 : ordered.end.line;
    return { start: ordered.start.line, end };
  }

  private wordBoundaryBackward(pos: Position): Position {
    if (pos.col === 0) return this.clampPosition({ line: pos.line - 1, col: Number.MAX_SAFE_INTEGER });
    const line = this.lines[pos.line]!;
    let col = pos.col;
    while (col > 0 && /\s/.test(line.charAt(col - 1))) col--;
    while (col > 0 && /\w/.test(line.charAt(col - 1))) col--;
    return { line: pos.line, col };
  }

  private wordBoundaryForward(pos: Position): Position {
    const line = this.lines[pos.line]!;
    if (pos.col >= line.length) return this.clampPosition({ line: pos.line + 1, col: 0 });
    let col = pos.col;
    while (col < line.length && /\s/.test(line.charAt(col))) col++;
    while (col < line.length && /\w/.test(line.charAt(col))) col++;
    return { line: pos.line, col };
  }

  private rawInsert(pos: Position, text: string, undoStack: UndoCommand[] | null, time: number, group: number): Position {
    pos = this.clampPosition(pos);
    const before = this.lines[pos.line]!.slice(0, pos.col);
    const after = this.lines[pos.line]!.slice(pos.col);
    const insertLines = splitText(text);
    let end: Position;
    if (insertLines.length === 1) {
      this.lines.splice(pos.line, 1, before + insertLines[0]! + after);
      end = { line: pos.line, col: before.length + insertLines[0]!.length };
    } else {
      const first = before + insertLines[0]!;
      const last = insertLines[insertLines.length - 1]! + after;
      const middle = insertLines.slice(1, -1);
      this.lines.splice(pos.line, 1, first, ...middle, last);
      end = { line: pos.line + insertLines.length - 1, col: insertLines[insertLines.length - 1]!.length };
    }
    if (undoStack) {
      this.pushUndoCommand(undoStack, { type: "selection", time, group, selection: cloneSelection(this.selection) });
      this.pushUndoCommand(undoStack, { type: "remove", time, group, start: { ...pos }, end: { ...end } });
    }
    this.setSelection(end);
    this.bump();
    return end;
  }

  private rawRemove(start: Position, end: Position, undoStack: UndoCommand[] | null, time: number, group: number): Position {
    start = this.clampPosition(start);
    end = this.clampPosition(end);
    if (comparePosition(start, end) > 0) [start, end] = [end, start];
    if (comparePosition(start, end) === 0) return start;
    const text = this.textInRange(start, end);
    if (undoStack) {
      this.pushUndoCommand(undoStack, { type: "selection", time, group, selection: cloneSelection(this.selection) });
      this.pushUndoCommand(undoStack, { type: "insert", time, group, pos: { ...start }, text });
    }
    const before = this.lines[start.line]!.slice(0, start.col);
    const after = this.lines[end.line]!.slice(end.col);
    this.lines.splice(start.line, end.line - start.line + 1, before + after);
    if (this.lines.length === 0) this.lines.push("");
    this.setSelection(start);
    this.bump();
    return start;
  }

  private textInRange(start: Position, end: Position): string {
    if (start.line === end.line) return this.lines[start.line]!.slice(start.col, end.col);
    const parts = [this.lines[start.line]!.slice(start.col)];
    for (let line = start.line + 1; line < end.line; line++) parts.push(this.lines[line]!);
    parts.push(this.lines[end.line]!.slice(0, end.col));
    return parts.join("\n");
  }

  private nextUndoGroup(kind: EditKind, time: number, stack: UndoCommand[]): number {
    const previous = stack[stack.length - 1];
    const previousKind = this.lastEditKind;
    const merge = previous
      && previousKind === kind
      && kind !== "delimiter"
      && Math.abs(time - previous.time) < UNDO_MERGE_TIMEOUT_MS;
    if (!merge) this.undoGroup++;
    this.lastEditKind = kind;
    return this.undoGroup;
  }

  private popUndo(source: UndoCommand[], target: UndoCommand[]): void {
    let cmd = source.pop();
    if (!cmd) return;
    this.lastEditKind = null;
    while (cmd) {
      this.applyUndoCommand(cmd, target);
      const next = source[source.length - 1];
      if (!next || next.group !== cmd.group) break;
      cmd = source.pop();
    }
  }

  private applyUndoCommand(cmd: UndoCommand, target: UndoCommand[]): void {
    if (cmd.type === "selection") {
      this.selection = cloneSelection(cmd.selection);
      return;
    }
    if (cmd.type === "insert") {
      this.rawInsert(cmd.pos, cmd.text, target, cmd.time, cmd.group);
      return;
    }
    this.rawRemove(cmd.start, cmd.end, target, cmd.time, cmd.group);
  }

  private pushUndoCommand(stack: UndoCommand[], command: UndoCommand): void {
    stack.push(command);
    while (stack.length > MAX_UNDO_COMMANDS) stack.shift();
  }

  private bump(): void {
    this.revision++;
  }
}

export type CursorCommand =
  | "left"
  | "right"
  | "up"
  | "down"
  | "lineStart"
  | "lineEnd"
  | "docStart"
  | "docEnd"
  | "wordLeft"
  | "wordRight";

export function splitText(text: string): string[] {
  const normalized = text.replaceAll("\r\n", "\n").replaceAll("\r", "\n");
  return normalized.split("\n");
}

export function comparePosition(a: Position, b: Position): number {
  if (a.line !== b.line) return a.line - b.line;
  return a.col - b.col;
}

function cloneSelection(selection: Selection): Selection {
  return {
    anchor: { ...selection.anchor },
    head: { ...selection.head }
  };
}

function leadingSpaces(text: string): number {
  let count = 0;
  while (count < text.length && text.charAt(count) === " ") count++;
  return count;
}

function adjustPositionByLinePrefix(pos: Position, startLine: number, endLine: number, width: number): Position {
  if (pos.line < startLine || pos.line > endLine) return { ...pos };
  return { line: pos.line, col: pos.col + width };
}

function adjustPositionByLineRemovals(pos: Position, removals: Map<number, number>): Position {
  const count = removals.get(pos.line);
  if (!count) return { ...pos };
  return { line: pos.line, col: Math.max(0, pos.col - count) };
}

function editKindForInsert(text: string): EditKind {
  if (text === " ") return "space";
  if (text.length !== 1 || /\s/.test(text)) return "delimiter";
  return "word";
}

function previousCodePointCol(line: string, col: number): number {
  if (col <= 0) return 0;
  const code = line.charCodeAt(col - 1);
  return code >= 0xdc00 && code <= 0xdfff ? Math.max(0, col - 2) : col - 1;
}

function nextCodePointCol(line: string, col: number): number {
  if (col >= line.length) return line.length;
  const code = line.charCodeAt(col);
  return code >= 0xd800 && code <= 0xdbff ? Math.min(line.length, col + 2) : col + 1;
}

export function syntaxFromPath(path: string | undefined): string {
  if (!path) return "plain";
  if (path.match(/\.(ts|tsx|js|jsx|mjs)$/i)) return "javascript";
  if (path.match(/\.(c|cpp|cc|h|hpp)$/i)) return "cpp";
  if (path.match(/\.json$/i)) return "json";
  if (path.match(/\.md$/i)) return "markdown";
  if (path.match(/\.lua$/i)) return "lua";
  if (path.match(/\.py$/i)) return "python";
  return "plain";
}

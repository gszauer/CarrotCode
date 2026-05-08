import type { Rect } from "../shared/types";
import type { CursorCommand } from "../editor/document";

export type TextInputTarget = {
  kind: "editor" | "search" | "chat" | "command" | "projectReplace" | "find" | "findReplace" | "aiBaseUrl" | "aiApiKey" | "aiModel" | "aiMaxContextTokens";
  getSelectedText(): string;
  replaceSelection(text: string): void;
  deleteSelectionOrBackward(unit?: "char" | "word" | "line"): void;
  deleteForward(unit?: "char" | "word" | "line"): void;
  moveCursor(command: CursorCommand, extend?: boolean): void;
  runShortcut(command: string): boolean;
  onCompositionPreview(text: string): void;
  onCompositionCommit(text: string): void;
};

export class InputBridge {
  readonly textarea: HTMLTextAreaElement;
  activeTarget: TextInputTarget | null = null;
  composing = false;
  compositionText = "";

  constructor(private readonly root: HTMLElement) {
    this.textarea = document.createElement("textarea");
    this.textarea.className = "input-bridge";
    this.textarea.autocapitalize = "off";
    this.textarea.autocomplete = "off";
    this.textarea.spellcheck = false;
    this.textarea.inputMode = "text";
    this.textarea.setAttribute("autocorrect", "off");
    this.root.appendChild(this.textarea);
    this.resetTextareaSentinel();
    this.install();
  }

  focusEditor(target: TextInputTarget, caretRect?: Rect): void {
    this.activeTarget = target;
    if (caretRect) this.placeNearCaret(caretRect);
    if (!this.isFocused()) this.textarea.focus({ preventScroll: true });
    this.resetTextareaSentinel();
  }

  refocus(caretRect?: Rect): void {
    if (!this.activeTarget) return;
    if (caretRect) this.placeNearCaret(caretRect);
    if (!this.isFocused()) {
      this.textarea.focus({ preventScroll: true });
      this.resetTextareaSentinel();
    }
  }

  blur(): void {
    this.activeTarget = null;
    this.textarea.blur();
  }

  isFocused(): boolean {
    return document.activeElement === this.textarea;
  }

  syncSelectionForClipboard(text: string): void {
    if (this.composing) return;
    this.textarea.focus({ preventScroll: true });
    if (!text) {
      this.resetTextareaSentinel();
      return;
    }
    this.textarea.value = text;
    this.textarea.setSelectionRange(0, text.length);
  }

  resetTextareaSentinel(): void {
    this.textarea.value = "\n";
    this.textarea.setSelectionRange(1, 1);
  }

  private install(): void {
    this.textarea.addEventListener("contextmenu", (event) => event.preventDefault());
    this.textarea.addEventListener("selectstart", (event) => event.preventDefault());
    this.textarea.addEventListener("keydown", (event) => this.onKeyDown(event));
    this.textarea.addEventListener("beforeinput", (event) => this.onBeforeInput(event as InputEvent));
    this.textarea.addEventListener("input", () => this.onInput());
    this.textarea.addEventListener("copy", (event) => this.onCopy(event));
    this.textarea.addEventListener("cut", (event) => this.onCut(event));
    this.textarea.addEventListener("paste", (event) => this.onPaste(event));
    this.textarea.addEventListener("compositionstart", () => {
      this.composing = true;
      this.compositionText = "";
    });
    this.textarea.addEventListener("compositionupdate", (event) => {
      this.compositionText = event.data;
      this.activeTarget?.onCompositionPreview(event.data);
    });
    this.textarea.addEventListener("compositionend", (event) => {
      this.composing = false;
      this.compositionText = "";
      this.activeTarget?.onCompositionCommit(event.data);
      this.resetTextareaSentinel();
    });
  }

  private onKeyDown(event: KeyboardEvent): void {
    const target = this.activeTarget;
    if (!target || event.isComposing) return;
    const shortcut = shortcutFromEvent(event);
    if (target.runShortcut(shortcut)) {
      event.preventDefault();
      event.stopPropagation();
      return;
    }

    const shift = event.shiftKey;
    const mod = isCommandModifier(event);
    const alt = event.altKey;
    const motion = keyToMotion(event.key, mod, alt);
    if (motion) {
      target.moveCursor(motion, shift);
      event.preventDefault();
      event.stopPropagation();
    }
  }

  private onBeforeInput(event: InputEvent): void {
    const target = this.activeTarget;
    if (!target || this.composing) return;
    switch (event.inputType) {
      case "insertText":
        target.replaceSelection(event.data ?? "");
        break;
      case "insertLineBreak":
      case "insertParagraph":
        if (target.kind !== "editor") {
          target.runShortcut("Enter");
        } else {
          target.replaceSelection("\n");
        }
        break;
      case "deleteContentBackward":
        target.deleteSelectionOrBackward("char");
        break;
      case "deleteContentForward":
        target.deleteForward("char");
        break;
      case "deleteWordBackward":
        target.deleteSelectionOrBackward("word");
        break;
      case "deleteWordForward":
        target.deleteForward("word");
        break;
      case "historyUndo":
        target.runShortcut("Mod+Z");
        break;
      case "historyRedo":
        target.runShortcut("Mod+Shift+Z");
        break;
      case "insertFromPaste": {
        const text = normalizePastedText(event.dataTransfer?.getData("text/plain") ?? event.data ?? "");
        if (!text) return;
        target.replaceSelection(text);
        break;
      }
      default:
        return;
    }
    event.preventDefault();
    this.resetTextareaSentinel();
  }

  private onInput(): void {
    const target = this.activeTarget;
    if (!target || this.composing) {
      this.resetTextareaSentinel();
      return;
    }
    const text = normalizePastedText(textareaInsertedText(this.textarea.value));
    if (text) target.replaceSelection(text);
    this.resetTextareaSentinel();
  }

  private onCopy(event: ClipboardEvent): void {
    const text = this.activeTarget?.getSelectedText() ?? "";
    if (!text) return;
    if (event.clipboardData) {
      event.clipboardData.setData("text/plain", text);
      event.preventDefault();
    }
    this.syncSelectionForClipboard(text);
  }

  private onCut(event: ClipboardEvent): void {
    const text = this.activeTarget?.getSelectedText() ?? "";
    if (!text || !this.activeTarget) return;
    if (event.clipboardData) {
      event.clipboardData.setData("text/plain", text);
      event.preventDefault();
    }
    this.activeTarget.replaceSelection("");
    this.resetTextareaSentinel();
  }

  private onPaste(event: ClipboardEvent): void {
    const text = normalizePastedText(event.clipboardData?.getData("text/plain") ?? "");
    if (!text || !this.activeTarget) return;
    this.activeTarget.replaceSelection(text);
    event.preventDefault();
    this.resetTextareaSentinel();
  }

  private placeNearCaret(rect: Rect): void {
    const vv = window.visualViewport;
    const offsetLeft = vv?.offsetLeft ?? 0;
    const offsetTop = vv?.offsetTop ?? 0;
    this.textarea.style.left = `${Math.max(0, rect.x - offsetLeft)}px`;
    this.textarea.style.top = `${Math.max(0, rect.y - offsetTop)}px`;
  }
}

function normalizePastedText(text: string): string {
  return text.replaceAll("\r\n", "\n").replaceAll("\r", "\n");
}

function textareaInsertedText(value: string): string {
  if (value === "\n") return "";
  if (value.startsWith("\n")) return value.slice(1);
  return value;
}

export function shortcutFromEvent(event: KeyboardEvent): string {
  const parts: string[] = [];
  const mod = isCommandModifier(event);
  if (mod) parts.push("Mod");
  if (event.altKey) parts.push("Alt");
  if (event.shiftKey) parts.push("Shift");
  parts.push(normalizeKey(event.key));
  return parts.join("+");
}

function keyToMotion(key: string, mod: boolean, alt: boolean): CursorCommand | null {
  if (key === "ArrowLeft") return mod || alt ? "wordLeft" : "left";
  if (key === "ArrowRight") return mod || alt ? "wordRight" : "right";
  if (key === "ArrowUp") return "up";
  if (key === "ArrowDown") return "down";
  if (key === "Home") return "lineStart";
  if (key === "End") return "lineEnd";
  return null;
}

function normalizeKey(key: string): string {
  if (key === " ") return "Space";
  if (key.length === 1) return key.toUpperCase();
  return key;
}

function isCommandModifier(event: KeyboardEvent): boolean {
  return event.metaKey || event.ctrlKey;
}

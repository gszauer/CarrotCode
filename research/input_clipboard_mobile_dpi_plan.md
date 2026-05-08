# Input, Clipboard, Mobile Keyboard, and DPI Plan

Research date: 2026-04-29

This addendum covers the parts that are easy to miss in a WebGL-rendered editor: keyboard shortcuts, text input, clipboard, IME, iOS virtual keyboard behavior, and high-DPI canvas rendering.

The key rule: WebGL2 renders the editor, but a hidden native text control is still required as an input bridge. The app should never use a DOM text box as the visible editor, but it should use one as the browser integration point for keyboard, clipboard, IME, accessibility scaffolding, and mobile virtual keyboard.

## Sources

- MDN `KeyboardEvent`: keyboard events indicate low-level key interaction; MDN explicitly says to use the `input` event for text input and notes keyboard events may not fire for alternate input systems (https://developer.mozilla.org/en-US/docs/Web/API/KeyboardEvent/).
- MDN Clipboard `writeText`: secure-context API returning a promise after system clipboard update (https://developer.mozilla.org/en-US/docs/Web/API/Clipboard/writeText).
- MDN composition events: composition events report IME sessions and `compositionend` fires when composition completes or is cancelled (https://developer.mozilla.org/docs/Web/API/Element/compositionend_event).
- MDN VisualViewport: visual viewport emits `resize`, `scroll`, and `scrollend`, and mobile browsers commonly change the visual viewport rather than the layout viewport (https://developer.mozilla.org/en-US/docs/Web/API/VisualViewport).
- Khronos WebGL High DPI wiki: browsers upscale canvas by default on high-DPI screens; use `ResizeObserver` to determine displayed device-pixel size, and remember canvas positions are still reported in CSS pixels (https://www.khronos.org/webgl/wiki/HandlingHighDPI).
- MDN `inputmode`/input docs: use input attributes to hint virtual keyboard behavior (https://developer.mozilla.org/en-US/docs/Web/HTML/Element/input).
- iOS focus caveat reference: iOS requires focus that raises the keyboard to happen synchronously inside a user gesture; programmatic delayed focus may not show the keyboard (https://kairi.cc/en/blog/ios-input-scroll/).

## InputBridge

Add a browser platform service:

```ts
class InputBridge {
  textarea: HTMLTextAreaElement;
  activeTarget: TextInputTarget | null;
  composing: boolean;
  compositionText: string;

  attach(root: HTMLElement): void;
  focusEditor(target: TextInputTarget, reason: "pointer" | "command"): void;
  blur(): void;
  syncSelectionForClipboard(text: string): void;
}
```

`TextInputTarget` is implemented by `DocView`, `CommandView`, and the chat composer:

```ts
interface TextInputTarget {
  kind: "editor" | "command" | "chat";
  getSelectedText(): string;
  replaceSelection(text: string): void;
  deleteSelectionOrBackward(unit: "char" | "word" | "line"): void;
  deleteForward(unit: "char" | "word" | "line"): void;
  moveCursor(command: CursorCommand): void;
  runShortcut(command: string): boolean;
  onCompositionPreview(text: string): void;
  onCompositionCommit(text: string): void;
}
```

The hidden textarea should:

- Stay in the DOM and be focusable.
- Not be `display: none` or `visibility: hidden`, because unfocusable/hidden controls are unreliable for keyboard and clipboard integration.
- Use `position: fixed`.
- Use `opacity: 0`.
- Be very small but not zero in all browsers.
- Use `font-size: 16px` to avoid iOS focus zoom behavior.
- Use `autocapitalize="off"`, `autocomplete="off"`, `autocorrect="off"`, `spellcheck="false"`.
- Use `inputmode="text"` and `enterkeyhint` depending on target.

Recommended CSS:

```css
.input-bridge {
  position: fixed;
  left: 0;
  top: 0;
  width: 1px;
  height: 1px;
  opacity: 0;
  z-index: -1;
  font-size: 16px;
  line-height: 16px;
  transform: translate3d(0, 0, 0);
}
```

For iOS, consider positioning the textarea near the visual caret before focusing so Safari's focus scroll heuristics behave better:

```ts
function placeInputNearCaret(caretCss: Rect) {
  const vv = window.visualViewport;
  const offsetLeft = vv?.offsetLeft ?? 0;
  const offsetTop = vv?.offsetTop ?? 0;
  textarea.style.left = `${Math.max(0, caretCss.x - offsetLeft)}px`;
  textarea.style.top = `${Math.max(0, caretCss.y - offsetTop)}px`;
}
```

## Keyboard Shortcuts

Use `keydown` for shortcuts and navigation, not for text insertion. MDN notes `KeyboardEvent` is low-level and `input` should be used for text input (https://developer.mozilla.org/en-US/docs/Web/API/KeyboardEvent/).

Shortcut normalization:

```ts
function shortcutFromEvent(e: KeyboardEvent): string {
  const mod = isMacLike() ? e.metaKey : e.ctrlKey;
  const parts = [];
  if (mod) parts.push("Mod");
  if (e.altKey) parts.push("Alt");
  if (e.shiftKey) parts.push("Shift");
  const key = normalizeKey(e.key);
  parts.push(key);
  return parts.join("+");
}
```

Use `Mod` internally:

- macOS/iPad hardware keyboard: `Meta`
- Windows/Linux/ChromeOS: `Ctrl`
- iOS hardware keyboards often send `metaKey` for Command shortcuts

Core shortcuts:

```text
Mod+C       copy
Mod+X       cut
Mod+V       paste
Mod+A       select all
Mod+Z       undo
Mod+Shift+Z redo
Mod+Y       redo on Windows/Linux
Mod+S       save to VFS
Mod+F       find in document
Mod+Shift+F project search
Mod+P       command palette / quick open
Mod+B       toggle sidebar
Tab         indent or focus traversal depending target
Shift+Tab   outdent
Enter       insert newline or submit command/chat
Escape      close palette/search/chat focus
Arrow keys  cursor motion
PageUp/Down visible-page motion
Home/End    line motion
```

For handled shortcuts:

```ts
canvasRoot.addEventListener("keydown", (e) => {
  if (!activeTextTarget) return;
  if (e.isComposing) return;

  const shortcut = shortcutFromEvent(e);
  if (commandRegistry.runShortcut(shortcut, activeTextTarget)) {
    e.preventDefault();
    e.stopPropagation();
  }
});
```

Do not prevent default for unknown browser/system shortcuts.

## Ctrl+C / Copy

Because the editor is not a real visible text box, `Ctrl+C` / `Cmd+C` must be implemented explicitly.

Preferred path:

1. On `keydown`, detect `Mod+C`.
2. Ask active `TextInputTarget` for selected text.
3. If there is selection, call `navigator.clipboard.writeText(selectedText)`.
4. Prevent default only if the app handled the copy.
5. If Clipboard API fails, rely on the hidden textarea fallback.

```ts
async function copySelection(target: TextInputTarget): Promise<boolean> {
  const text = target.getSelectedText();
  if (!text) return false;
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    inputBridge.syncSelectionForClipboard(text);
    document.execCommand?.("copy");
    return true;
  }
}
```

Better fallback:

- Keep the hidden textarea's value set to selected text when the editor has a selection.
- Select the textarea content with `textarea.setSelectionRange(0, text.length)`.
- Listen for `copy` and set `event.clipboardData`.

```ts
textarea.addEventListener("copy", (e) => {
  const text = activeTarget?.getSelectedText() ?? "";
  if (!text) return;
  e.clipboardData?.setData("text/plain", text);
  e.preventDefault();
});
```

Reasoning:

- `navigator.clipboard.writeText()` requires secure context and user-agent permission rules (MDN: https://developer.mozilla.org/en-US/docs/Web/API/Clipboard/writeText).
- Native `copy` events are triggered by real keyboard/menu actions and work well as a fallback if the hidden textarea owns focus.

## Cut

`Mod+X`:

1. Copy selected text.
2. If copy succeeds, delete selection through the document model.
3. Push one undo group named `cut`.

Also handle `cut` event on the textarea:

```ts
textarea.addEventListener("cut", (e) => {
  const text = activeTarget?.getSelectedText() ?? "";
  if (!text) return;
  e.clipboardData?.setData("text/plain", text);
  activeTarget!.replaceSelection("");
  e.preventDefault();
});
```

## Paste

Use both paths:

- `paste` event on hidden textarea reads `event.clipboardData.getData("text/plain")`.
- `Mod+V` can optionally call `navigator.clipboard.readText()` if the paste event does not fire.

```ts
textarea.addEventListener("paste", (e) => {
  const text = e.clipboardData?.getData("text/plain") ?? "";
  if (!text) return;
  activeTarget?.replaceSelection(normalizePastedText(text));
  e.preventDefault();
});
```

Do not depend only on `navigator.clipboard.readText()`. Browser permission behavior is stricter for reads than writes.

## Text Input

Use `beforeinput`/`input` from the hidden textarea for actual text insertion and deletion. Use keyboard events only for navigation/commands.

Input types to handle:

```text
insertText
insertLineBreak
insertParagraph
insertFromPaste
deleteContentBackward
deleteContentForward
deleteWordBackward
deleteWordForward
deleteByCut
historyUndo
historyRedo
```

Implementation shape:

```ts
textarea.addEventListener("beforeinput", (e: InputEvent) => {
  if (!activeTarget || inputBridge.composing) return;

  switch (e.inputType) {
    case "insertText":
      activeTarget.replaceSelection(e.data ?? "");
      e.preventDefault();
      break;
    case "insertLineBreak":
    case "insertParagraph":
      activeTarget.replaceSelection("\n");
      e.preventDefault();
      break;
    case "deleteContentBackward":
      activeTarget.deleteSelectionOrBackward("char");
      e.preventDefault();
      break;
    case "deleteContentForward":
      activeTarget.deleteForward("char");
      e.preventDefault();
      break;
    case "historyUndo":
      activeTarget.runShortcut("undo");
      e.preventDefault();
      break;
  }
  resetTextareaSentinel();
});
```

Use a sentinel value in the textarea so uncontrolled native edits do not accumulate:

```ts
function resetTextareaSentinel() {
  textarea.value = "\n";
  textarea.setSelectionRange(1, 1);
}
```

## IME And Composition

Composition is mandatory for CJK, accents, emoji input, and many mobile keyboards.

Events:

- `compositionstart`: mark composing.
- `compositionupdate`: draw preview text at caret.
- `compositionend`: commit final text if non-empty.

```ts
textarea.addEventListener("compositionstart", () => {
  inputBridge.composing = true;
  inputBridge.compositionText = "";
});

textarea.addEventListener("compositionupdate", (e) => {
  inputBridge.compositionText = e.data;
  activeTarget?.onCompositionPreview(e.data);
});

textarea.addEventListener("compositionend", (e) => {
  inputBridge.composing = false;
  inputBridge.compositionText = "";
  activeTarget?.onCompositionCommit(e.data);
  resetTextareaSentinel();
});
```

Render composition preview in WebGL:

- Use the active syntax/text color or a dedicated composition color.
- Draw preview at caret without mutating document text.
- Draw underline beneath preview text.
- Hide normal caret or place it after preview text.

MDN says composition events are specifically tied to text composition systems such as IMEs (https://developer.mozilla.org/docs/Web/API/Element/compositionend_event).

## iOS Virtual Keyboard

There is no general browser API that simply says "show keyboard now" for a custom canvas editor. The practical web approach is to focus a real input/textarea in response to a user gesture.

Rules:

- On pointer/touch down inside editable editor/chat/command surfaces, synchronously call `textarea.focus({ preventScroll: true })`.
- Do not defer focus through `setTimeout`, `Promise`, or requestAnimationFrame if the goal is to open the iOS keyboard.
- Keep the textarea available and focusable.
- Use `font-size: 16px` to avoid iOS zooming on input focus.
- On blur, keep editor selection state but stop accepting text input.

```ts
function focusForPointerGesture(target: TextInputTarget, caretCss: Rect) {
  inputBridge.activeTarget = target;
  placeInputNearCaret(caretCss);
  textarea.focus({ preventScroll: true });
  resetTextareaSentinel();
}
```

The iOS user-gesture caveat is documented in practical WebKit/iOS references and matches real-world behavior: keyboard-raising focus must happen synchronously inside the tap/click call stack (https://kairi.cc/en/blog/ios-input-scroll/).

## Visual Viewport And Keyboard Insets

Use `window.visualViewport` when available to detect mobile keyboard-induced viewport changes. MDN notes mobile browsers often update visual viewport offsets/sizes rather than window scroll values (https://developer.mozilla.org/en-US/docs/Web/API/VisualViewport).

```ts
function getViewportState() {
  const vv = window.visualViewport;
  return {
    layoutWidth: window.innerWidth,
    layoutHeight: window.innerHeight,
    visualWidth: vv?.width ?? window.innerWidth,
    visualHeight: vv?.height ?? window.innerHeight,
    visualOffsetLeft: vv?.offsetLeft ?? 0,
    visualOffsetTop: vv?.offsetTop ?? 0,
    dpr: window.devicePixelRatio || 1,
  };
}
```

Listen to:

```ts
window.addEventListener("resize", scheduleResize);
window.visualViewport?.addEventListener("resize", scheduleResize);
window.visualViewport?.addEventListener("scroll", scheduleResize);
screen.orientation?.addEventListener?.("change", scheduleResize);
```

Use visual viewport dimensions for:

- visible canvas size
- chat composer placement
- command palette placement
- ensuring caret is above keyboard
- scroll-to-caret after keyboard opens

Keep internal editor layout in CSS pixels. Convert to device pixels only when setting canvas backing size and WebGL viewport.

## High-DPI WebGL2 Canvas

The canvas has two sizes:

- CSS size in CSS pixels: layout/input coordinates.
- backing size in device pixels: actual WebGL framebuffer.

Use a `ResizeObserver` and `devicePixelRatio`:

```ts
function resizeCanvasToDisplaySize(canvas: HTMLCanvasElement, gl: WebGL2RenderingContext) {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.round(rect.width * dpr));
  const height = Math.max(1, Math.round(rect.height * dpr));

  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
    gl.viewport(0, 0, width, height);
    renderer.setViewport({
      cssWidth: rect.width,
      cssHeight: rect.height,
      deviceWidth: width,
      deviceHeight: height,
      dpr,
    });
  }
}
```

Khronos recommends using `ResizeObserver` for high-DPI WebGL and warns that canvas positions are reported in CSS pixels even when rendering in device pixels (https://www.khronos.org/webgl/wiki/HandlingHighDPI).

Coordinate rules:

- Pointer events arrive in CSS pixels.
- View layout is CSS pixels.
- Text metrics for editor layout are CSS pixels.
- Canvas backing store is device pixels.
- WebGL viewport is device pixels.
- Orthographic projection maps CSS pixels to clip space, but shader dilation and framebuffer size must know the device-pixel viewport.

For Slug:

- `u_viewportPx` should be the device-pixel canvas size.
- Glyph/object positions can be in CSS pixels.
- Dynamic dilation should use device-pixel viewport dimensions.
- Pixel snapping should snap to device pixels:

```ts
function snapCssToDevicePixel(css: number, dpr: number): number {
  return Math.round(css * dpr) / dpr;
}
```

Use snapped baselines and caret rectangles for crisp editor text.

## Pointer And Touch Coordinates

Convert pointer coordinates to CSS-pixel local canvas coordinates:

```ts
function pointerToCanvasCss(e: PointerEvent, canvas: HTMLCanvasElement) {
  const rect = canvas.getBoundingClientRect();
  return {
    x: e.clientX - rect.left,
    y: e.clientY - rect.top,
  };
}
```

Do not multiply pointer coordinates by DPR before hit testing. The layout tree lives in CSS pixels.

Touch support:

- Use Pointer Events where available.
- Set `touch-action: none` on the canvas root only if implementing all gestures manually.
- Support one-finger caret placement/selection drag.
- Support two-finger pan later if needed.
- Avoid browser page scrolling by making the app root fixed and handling editor scroll internally.

## Selection UX On Mobile

V1 mobile selection can be simpler than native iOS text selection:

- Tap places caret and opens keyboard.
- Drag selects text.
- Long press can begin selection later.
- Provide editor toolbar buttons for Copy, Cut, Paste, Select All above the keyboard or in the sidebar/status area.

Because the visible text is WebGL, native iOS selection handles will not appear on the rendered editor text. If native selection handles become a requirement, the editor would need a much more complex DOM selection mirror. Do not make that a v1 dependency.

## Accessibility Note

A pure WebGL text editor is not naturally accessible to screen readers. The hidden textarea can provide partial text-input plumbing but not a complete accessible document model. Future work should add:

- ARIA role and labels around the app.
- An accessible text mirror for the active line or active document.
- Screen-reader announcements for cursor position, selection, and diagnostics.
- Keyboard-only navigation for all panels.

This is separate from the input bridge but should not be ignored.

## Implementation Order

1. Build `InputBridge` with hidden textarea and focus routing.
2. Add `keydown` shortcut normalization.
3. Implement `Mod+C`, `Mod+X`, `Mod+V`, `Mod+A`, undo/redo.
4. Add `beforeinput` insertion/deletion.
5. Add composition preview/commit.
6. Add iOS pointer-focus path and visual viewport resize handling.
7. Add high-DPI canvas resize and renderer viewport contract.
8. Add mobile toolbar buttons for copy/cut/paste/select all.
9. Test with hardware keyboard on desktop, iPad keyboard, iPhone software keyboard, Android software keyboard.

## Test Matrix

Desktop:

- Chrome, Firefox, Safari.
- Windows/Linux `Ctrl` shortcuts.
- macOS `Command` shortcuts.
- Clipboard with selected text, no selection, multiline selection.
- IME: Japanese/Chinese/Korean or macOS accent composition.
- DPR 1, 1.25, 1.5, 2.

iOS/iPadOS:

- Safari.
- iPhone software keyboard.
- iPad software keyboard.
- iPad hardware keyboard.
- Orientation change.
- Visual viewport shrink/offset when keyboard opens.
- Copy/paste through keyboard toolbar and app toolbar.

Android:

- Chrome software keyboard.
- Gboard composition/autocomplete.
- Paste from clipboard.
- DPR/high-res canvas.

Regression cases:

- Tap editor, keyboard appears, caret remains visible.
- Type text into WebGL editor.
- Compose CJK text and commit.
- Copy/cut/paste multiline selection.
- Device rotation preserves canvas sharpness and caret position.
- Browser zoom or DPR change resizes the framebuffer correctly.

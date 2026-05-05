# Chapter 6: Mouse Input and Selection

*Click to place the cursor, drag to select, double-click to select a word, shift-click to extend — and scroll with the wheel.*

---

## 6.1 — Coordinate Conversion

Before the editor can respond to a mouse click, it needs to answer a fundamental question: where in the document did the user click? The mouse event provides coordinates in CSS pixels relative to the browser viewport. The document is an array of lines, each addressed by a line index and a column index. Translating between these two worlds — screen space and document space — is the first thing every mouse handler does.

The translation happens in two steps. First, we convert screen coordinates to canvas coordinates by multiplying by the device pixel ratio:

```javascript
_toCanvas(e) {
  return { x: e.clientX * this.dpr, y: e.clientY * this.dpr };
}
```

A click at CSS pixel (200, 150) on a 2x Retina display becomes canvas pixel (400, 300). This puts us in the same coordinate system that the drawing code uses — device pixels on the canvas's backing store.

Second, we convert canvas coordinates to a document position — a `(line, col)` pair:

```javascript
_canvasToTextPos(cx, cy) {
  const line = Math.floor(
    (cy - this.textAreaY + this.scrollY) / this.lineH
  );
  const col = Math.round(
    (cx - this.gutterW - Config.textPaddingLeft * this.dpr
     + this.scrollX) / this.charW
  );
  return {
    line: Math.max(0, Math.min(line, this.doc.lineCount - 1)),
    col: Math.max(0, col)
  };
}
```

The line calculation subtracts the text area's top edge (below the menu bar), adds the vertical scroll offset, and divides by the line height. `Math.floor` gives us the line index that the click falls within — clicking anywhere on a line's vertical extent selects that line.

The column calculation subtracts the gutter width and the text padding to get the x-position relative to the start of the text. It adds the horizontal scroll offset to account for text that has scrolled off the left edge. Dividing by the character width gives the column index. Here we use `Math.round` instead of `Math.floor`, because we want the cursor to snap to the nearest character boundary. Clicking on the left half of a character places the cursor before it; clicking on the right half places the cursor after it. This snap-to-nearest behavior is what users expect — it makes cursor placement feel precise rather than biased.

Both values are clamped. The line is restricted to `[0, lineCount - 1]` so clicking below the last line of text does not produce an invalid index. The column is restricted to a minimum of 0 so clicking to the left of the text does not produce a negative column. There is a second clamping step at the call site, where the column is further restricted to the actual length of the computed line:

```javascript
pos.col = Math.min(pos.col, this.doc.getLine(pos.line).length);
```

This prevents the cursor from being placed past the end of a short line when the click is far to the right. If the user clicks at x-position 500 on a line that is only 20 characters long, the column is clamped to 20, not left at whatever large value the division produced.

These two conversion functions are called from every mouse handler in the editor. They are the lens through which the editor sees mouse input — transforming raw pixel positions into meaningful document locations.

Let us trace through a concrete example to make the arithmetic tangible. The user clicks at CSS pixel (350, 200) on a 2x Retina display. The menu bar is 28 CSS pixels tall, the gutter is 50 CSS pixels wide (for a three-digit line count), the text padding is 8 CSS pixels, and the font produces 18-device-pixel-wide characters and 45-device-pixel-tall lines. The document is scrolled down by 5 lines.

Step 1: `_toCanvas` converts (350, 200) to canvas coordinates (700, 400).

Step 2: `_canvasToTextPos(700, 400)` computes the line:
- `cy = 400`, `textAreaY = 28 * 2 = 56`, `scrollY = 5 * 45 = 225`, `lineH = 45`
- `(400 - 56 + 225) / 45 = 569 / 45 = 12.6`
- `Math.floor(12.6) = 12`

And the column:
- `cx = 700`, `gutterW = 50 * 2 = 100`, `textPaddingLeft * dpr = 8 * 2 = 16`, `scrollX = 0`, `charW = 18`
- `(700 - 100 - 16 + 0) / 18 = 584 / 18 = 32.4`
- `Math.round(32.4) = 32`

The click maps to line 12, column 32. If line 12 has only 25 characters, the call site clamps the column to 25. The cursor is placed at the end of the line, which is the closest valid position to where the user clicked.

This arithmetic runs on every mouse event — every click, every drag movement, every double-click. It must be fast, and it is: a few subtractions, a division, and a rounding operation. No loops, no searches, no data structure traversals. This is the payoff of using a monospace font with fixed-height lines — every conversion is O(1).


## 6.2 — Click to Place Cursor

The `mousedown` event is where mouse interaction begins. When the user clicks in the text area, we need to place the cursor at the clicked position, prepare for a potential drag selection, and reset the cursor blink timer so the cursor is immediately visible.

The `_onMouseDown` handler has to route the click to the correct target. A click might land on the menu bar, inside a dropdown menu, on the scrollbar, or in the text area. The handler checks each region in order:

```javascript
_onMouseDown(e) {
  const p = this._toCanvas(e);
  this.mouseX = p.x;
  this.mouseY = p.y;
  this.mouseDown = true;

  // Dropdown open? Route to dropdown handler.
  if (this.activeMenu >= 0) {
    if (this._handleDropdownClick(p.x, p.y)) return;
    if (p.y < this.menuBarH) {
      this._handleMenuBarClick(p.x);
      return;
    }
    this.activeMenu = -1;
    this.menuHoverItem = -1;
    this.needsRedraw = true;
    return;
  }

  // Menu bar?
  if (p.y < this.menuBarH) {
    this._handleMenuBarClick(p.x);
    return;
  }

  // Scrollbar?
  const sbRect = this._getScrollbarRect();
  if (sbRect && p.x >= this.canvas.width
      - Math.round(this.scrollbarWidth * this.dpr)) {
    // ... scrollbar handling ...
    return;
  }

  // Text area.
  if (p.y >= this.textAreaY
      && p.y < this.textAreaY + this.textAreaH) {
    // ... text area click handling ...
  }
}
```

The dropdown check comes first. If a dropdown menu is open, clicks are routed to the dropdown handler, to the menu bar (if the click is on a different menu label), or dismissed (if the click is outside both). This is the same modal interception pattern we saw for keyboard input in Chapter 5 — the dropdown takes exclusive focus when it is open.

The region checks form a strict hierarchy: dropdown first, then menu bar, then scrollbar, then text area. Each check tests the click position against the region's bounds and returns if the click is handled. A click in the gutter (the area showing line numbers) is not explicitly handled — it falls through to the text area handler because the gutter overlaps with the text area's y-range, and the `_canvasToTextPos` function will compute a column of 0 or negative (clamped to 0) for clicks in the gutter area. This means clicking on a line number places the cursor at the beginning of that line, which is reasonable behavior. A more sophisticated editor might treat gutter clicks as "select entire line" — and that would be a straightforward addition using the same coordinate conversion infrastructure.

When the click lands in the text area, the core logic runs:

```javascript
const pos = this._canvasToTextPos(p.x, p.y);
pos.col = Math.min(pos.col, this.doc.getLine(pos.line).length);
this.doc.setCursor(pos.line, pos.col);

if (e.shiftKey) {
  if (!this.doc.selectionActive) {
    this.doc.selectionActive = true;
    this.doc.selStartLine = this.doc.cursorLine;
    this.doc.selStartCol = this.doc.cursorCol;
  }
  this.doc.setCursor(pos.line, pos.col);
  this.doc.updateSelectionEnd();
} else {
  this.doc.clearSelection();
  this.doc.startSelection();
  this.isDraggingSelection = true;
}
this.cursorBlink = 0;
this.needsRedraw = true;
```

We convert the click position to a document position, clamp the column to the line length, and set the cursor. Then we check for the Shift modifier.

Without Shift, we clear any existing selection, start a new one anchored at the click position, and set `isDraggingSelection = true`. This flag tells the `mousemove` handler that subsequent mouse movement should extend the selection rather than being ignored. At this point, the selection has zero extent — its start and end are both at the click position. If the user releases the mouse without moving, the result is simply a cursor placement with no visible selection.

With Shift, we extend the existing selection (or start a new one from the current cursor position if none is active). The selection end is updated to the clicked position. This is the shift-click behavior that every editor supports: click somewhere, then shift-click somewhere else to select the range between the two positions. If you shift-click again at a different position, the selection end moves but the start stays anchored.

The `this.cursorBlink = 0` reset makes the cursor immediately visible after the click. Without this, the cursor might be in its "off" phase when the user clicks, making it look like the click did not register.

We store the mouse position in `this.mouseX` and `this.mouseY` at the top of the handler. These are used by the menu drawing code to detect hover states — the dropdown highlights the item under the mouse cursor, and it reads from these stored coordinates during the draw pass.


## 6.3 — Drag to Select

When the user presses the mouse button in the text area and then moves the mouse before releasing it, they are performing a drag selection. The selected region extends from the initial click position (the selection anchor) to the current mouse position. The selection updates in real time as the mouse moves, and it can span multiple lines.

The `mousemove` handler checks the `isDraggingSelection` flag:

```javascript
_onMouseMove(e) {
  const p = this._toCanvas(e);
  this.mouseX = p.x;
  this.mouseY = p.y;

  if (this.isDraggingScrollbar) {
    this._updateScrollFromDrag(p.y);
    this.needsRedraw = true;
    return;
  }

  if (this.isDraggingSelection) {
    const pos = this._canvasToTextPos(p.x, p.y);
    pos.col = Math.min(pos.col, this.doc.getLine(pos.line).length);
    this.doc.setCursor(pos.line, pos.col);
    this.doc.selEndLine = pos.line;
    this.doc.selEndCol = pos.col;
    this.doc.selectionActive = true;
    this.cursorBlink = 0;
    this.needsRedraw = true;

    // Auto-scroll
    if (p.y < this.textAreaY + this.lineH) {
      this.scrollY = Math.max(0, this.scrollY - this.lineH * 0.5);
    } else if (p.y > this.textAreaY + this.textAreaH - this.lineH) {
      const maxScroll = Math.max(0,
        this.doc.lineCount * this.lineH - this.textAreaH);
      this.scrollY = Math.min(maxScroll,
        this.scrollY + this.lineH * 0.5);
    }
    return;
  }

  if (this.activeMenu >= 0) {
    this.needsRedraw = true;
  }
}
```

On each mouse move during a drag, we convert the mouse position to a document position, update the cursor and the selection end, and trigger a redraw. The cursor follows the mouse, and the selection highlight updates to cover the range from the anchor (the original click point) to the current mouse position.

We set `this.doc.selEndLine` and `this.doc.selEndCol` directly rather than calling `updateSelectionEnd`, because we need to set the cursor position *and* the selection end to the same value, and `updateSelectionEnd` reads from the cursor (which we have already set). The direct assignment is more explicit about what is happening.

The auto-scroll behavior is what makes drag selection usable for ranges that extend beyond the visible viewport. If the mouse is near the top edge of the text area (within one line height of the top), we scroll up by half a line. If the mouse is near the bottom edge, we scroll down by half a line. The half-line increment is a tuning choice — it produces smooth scrolling that is fast enough to be practical but slow enough to be controllable. A full-line increment would be too jerky; a quarter-line increment would be too slow.

The auto-scrolling happens on every `mousemove` event, which fires many times per second while the mouse is moving. This means the scroll rate depends on how often the browser fires `mousemove` events, which is typically around 60Hz. At half a line per event, the auto-scroll speed is roughly 30 lines per second, which is fast enough to scroll through a long document in a few seconds.

Note that the auto-scroll adjusts `scrollY` directly. The next `mousemove` event will convert the mouse position using the new scroll offset, which means the cursor and selection will track correctly as the viewport scrolls. The document lines scroll past the mouse, and the selection extends to cover them.

Let us trace through a complete drag selection to see how all the pieces interact. The user clicks at line 10, column 5, then drags downward to line 15, column 20, then releases.

1. **mousedown at (10, 5):** `_canvasToTextPos` converts the click to (10, 5). The cursor is set to (10, 5). `clearSelection` removes any old selection. `startSelection` anchors the selection start at (10, 5). `isDraggingSelection` is set to `true`. Selection: start (10, 5), end (10, 5) — zero extent, no visible highlight.

2. **mousemove to (12, 8):** `isDraggingSelection` is true. `_canvasToTextPos` gives (12, 8). Cursor moves to (12, 8). Selection end is set to (12, 8). Selection: (10, 5) to (12, 8) — three lines highlighted. The renderer draws highlight rectangles: a partial rectangle on line 10 from column 5 to the end, full rectangles on line 11, and a partial rectangle on line 12 from column 0 to column 8.

3. **mousemove to (15, 20):** Same flow. Selection extends to (15, 20). Five and a half lines are now highlighted.

4. **mouseup:** `isDraggingSelection` is cleared. The selection remains active with start (10, 5) and end (15, 20). The user can now copy (Ctrl+C), cut (Ctrl+X), type over the selection, or press any key that operates on selected text.

This entire interaction requires no special coordination between the three handlers — they share state through the `isDraggingSelection` flag, the cursor position, and the selection coordinates. Each handler does its small part, and the result is a fluid, responsive selection gesture.

The `mouseup` handler is simple:

```javascript
_onMouseUp() {
  this.mouseDown = false;
  this.isDraggingSelection = false;
  this.isDraggingScrollbar = false;
}
```

It clears all the drag state flags. The selection remains — `selectionActive` is still true, and the start and end positions are preserved. The user can now copy, cut, type over, or otherwise act on the selection. The drag is complete, but its result persists.

We listen for `mouseup` on the `window` object rather than on the canvas:

```javascript
window.addEventListener("mouseup", () => this._onMouseUp());
```

This is important. If the user starts a drag on the canvas and then moves the mouse outside the canvas (or even outside the browser window) before releasing, the `mouseup` event would not fire on the canvas. By listening on `window`, we catch the release regardless of where it happens. Without this, the drag would never end, and the selection would keep updating as the mouse moved back into the canvas area.


## 6.4 — Shift-Click to Extend Selection

Shift-click is the mechanism for creating or extending a selection by clicking at a distant position while holding Shift. It is an alternative to click-and-drag for selecting large ranges of text, and it is essential for selecting text that does not fit on one screen — the user can click at the start, scroll to the end, and shift-click to select everything in between.

The shift-click handling is embedded in the `_onMouseDown` handler:

```javascript
if (e.shiftKey) {
  if (!this.doc.selectionActive) {
    this.doc.selectionActive = true;
    this.doc.selStartLine = this.doc.cursorLine;
    this.doc.selStartCol = this.doc.cursorCol;
  }
  this.doc.setCursor(pos.line, pos.col);
  this.doc.updateSelectionEnd();
}
```

There are two cases. If a selection is already active (the user previously clicked or drag-selected), shift-click moves the selection end to the new position while keeping the start anchored. The selection grows or shrinks to match. If no selection is active (the user just has a cursor with no highlight), shift-click creates a new selection from the current cursor position to the clicked position. The cursor position becomes the selection start, and the clicked position becomes the selection end.

This behavior means that shift-click always works, regardless of the prior state. If you click at position A, then shift-click at position B, the selection covers A to B. If you then shift-click at position C, the selection covers A to C (not B to C). The anchor is always the original click position, and shift-click moves the other end.

The shift-click model is the same as the shift-arrow model from Chapter 5, translated to mouse input. The selection has an anchor (the start) and a mobile end. The anchor is set on the initial click or when the first shift-modified action occurs. The mobile end follows the cursor, whether the cursor is moved by the mouse or the keyboard. This consistency is important — the user builds a mental model of how selection works, and that model should be the same regardless of input device.

One interaction to be aware of: if the user creates a selection with the keyboard (shift-arrow), then shift-clicks to extend it, the anchor is the original keyboard selection start, not the cursor position. This is because the shift-click code checks `this.doc.selectionActive` and preserves the existing `selStartLine` and `selStartCol` if a selection is already active. The keyboard and mouse share the same selection state, so they interoperate seamlessly.

There is a subtlety here: we do not set `isDraggingSelection = true` when Shift is held. This means the user cannot shift-click and then drag to further adjust the selection. This is a deliberate simplification. Most users expect shift-click to be a point-and-click operation, not a click-and-drag one. If the user wants to fine-tune the selection, they can shift-click again at a different position, or use shift-arrow keys for precise adjustment.


## 6.5 — Double-Click to Select Word

Double-clicking on a word should select the entire word. This is a universal convention in text editors, and it is the fastest way to select a single word for copying, replacing, or looking up.

The `dblclick` event fires when the user clicks twice in rapid succession on the same element. We do not need to detect the double-click ourselves — the browser handles the timing and provides the event:

```javascript
_onDoubleClick(e) {
  const p = this._toCanvas(e);
  if (p.y >= this.textAreaY
      && p.y < this.textAreaY + this.textAreaH) {
    const pos = this._canvasToTextPos(p.x, p.y);
    const line = this.doc.getLine(pos.line);
    pos.col = Math.min(pos.col, line.length);

    const left = this.doc.wordBoundaryLeft(pos.line, pos.col + 1);
    const right = this.doc.wordBoundaryRight(pos.line, pos.col);
    this.doc.selectionActive = true;
    this.doc.selStartLine = left.line;
    this.doc.selStartCol = left.col;
    this.doc.selEndLine = right.line;
    this.doc.selEndCol = right.col;
    this.doc.setCursor(right.line, right.col);
    this.needsRedraw = true;
  }
}
```

We convert the click position to a document position, then use the word boundary functions from Chapter 3 to find the start and end of the word at that position. The selection is set to cover the word — from the left boundary to the right boundary — and the cursor is placed at the right boundary (the end of the word).

There is a detail worth noting in the boundary function calls. We call `wordBoundaryLeft(pos.line, pos.col + 1)` with `col + 1`, not `col`. This is because `wordBoundaryLeft` scans backward from the position *before* the given column. If the cursor is on the first character of a word (column 5, and the word starts at column 5), calling `wordBoundaryLeft(line, 5)` would scan from column 4 — one character before the word — and would find the boundary of the *previous* word. By passing `col + 1`, we ensure the scan starts from within the clicked character, not before it.

For `wordBoundaryRight`, we pass `pos.col` directly. This function scans forward from the given column, which is the correct starting point — we want to find where the current word ends, starting from the click position.

The `dblclick` event fires after two `mousedown` events. The first `mousedown` sets up a drag selection (as described in section 6.2), and the subsequent `dblclick` overwrites it with a word selection. The first click's drag state (`isDraggingSelection = true`) is still set, but it is effectively superseded by the word selection. When the user releases the mouse after the double-click, the `mouseup` handler clears the drag flag, leaving the word selection in place.

One interaction to be aware of: the `mousedown` from the second click of the double-click sequence fires before the `dblclick` event. This means the cursor briefly moves to the clicked position (from the `mousedown` handler), and then the `dblclick` handler runs and sets up the word selection. The user never sees the intermediate state because both events are processed before the next frame is rendered.

If the user double-clicks on whitespace between words, the word boundary functions will select the whitespace run as a "word" of non-word characters. This is acceptable behavior — selecting whitespace by double-click is rarely useful, but it is not harmful, and it is consistent with how the word boundaries work everywhere else. If the user double-clicks on an operator like `===`, the word boundary functions will select the entire operator sequence, because consecutive non-word characters are treated as a unit. This is actually useful — selecting a multi-character operator with a double-click is faster than carefully dragging across three characters.

If the double-click lands at the very end of a line (past the last character), `wordBoundaryLeft` will scan leftward into the last word on the line, and `wordBoundaryRight` will find the end-of-line boundary. The result is a selection of the last word, which is the intuitive behavior.

The `dblclick` event is not the only way to handle double-click detection. An alternative approach is to detect double-clicks manually by tracking the time and position of each `mousedown` event and comparing consecutive clicks. This would give us more control — for example, we could implement triple-click to select an entire line. We use the browser's built-in `dblclick` event for simplicity, since it handles the timing and distance thresholds for us.


## 6.6 — Mouse Wheel Scrolling

The mouse wheel (or trackpad scroll gesture) is the primary way users scroll through a document. We listen for the `wheel` event on the canvas:

```javascript
this.canvas.addEventListener("wheel", (e) => this._onWheel(e),
  { passive: false });
```

The `{ passive: false }` option is required because we call `e.preventDefault()` inside the handler. By default, modern browsers treat wheel listeners as "passive" — they assume the handler will not call `preventDefault`, and they start scrolling the page immediately without waiting for the handler to finish. This improves scroll performance for regular web pages, but for our canvas editor, we need to prevent the default scroll behavior (which would scroll the entire page, not our editor content). Setting `passive: false` tells the browser to wait for our handler before deciding whether to scroll.

The handler:

```javascript
_onWheel(e) {
  e.preventDefault();

  // If a dropdown is open, scroll it instead
  if (this.activeMenu >= 0) {
    const mi = this.activeMenu;
    const children = this.menu[mi].children;
    const itemH = Math.round(26 * this.dpr);
    const sepH = Math.round(8 * this.dpr);
    let totalH = 0;
    for (let i = 0; i < children.length; i++) {
      totalH += children[i].type === "separator" ? sepH : itemH;
    }
    const maxH = this.canvas.height - this.menuBarH
                 - Math.round(8 * this.dpr);
    if (totalH > maxH) {
      if (!this._dropdownScrollY) this._dropdownScrollY = 0;
      this._dropdownScrollY += e.deltaY * this.dpr;
      this._dropdownScrollY = Math.max(0,
        Math.min(this._dropdownScrollY, totalH - maxH));
      this.needsRedraw = true;
      return;
    }
  }

  this.scrollY += e.deltaY * this.dpr;
  const maxScroll = Math.max(0,
    this.doc.lineCount * this.lineH - this.textAreaH);
  this.scrollY = Math.max(0, Math.min(this.scrollY, maxScroll));
  this.needsRedraw = true;
}
```

The first check is for an open dropdown menu. The View menu can contain many syntax entries and may overflow the screen. If a dropdown is open and it is taller than the viewport allows, the wheel scrolls the dropdown content instead of the document. This is a natural interaction — the user opens the View menu, sees that it extends beyond the screen, and scrolls within the menu to find the syntax they want. The dropdown scroll offset (`_dropdownScrollY`) is clamped to `[0, totalH - maxH]` to prevent scrolling past either end.

If no dropdown is open (the common case), the wheel scrolls the document. The `deltaY` property of the wheel event provides the scroll amount. We multiply by `this.dpr` to convert from CSS pixels to device pixels (since `scrollY` is in device pixels). The result is added to `scrollY`, which is then clamped to `[0, maxScroll]`.

The `maxScroll` calculation is `this.doc.lineCount * this.lineH - this.textAreaH` — the total height of the document minus the height of the viewport. When `scrollY` is 0, the top of the document is visible. When `scrollY` equals `maxScroll`, the bottom of the document is visible. Clamping to this range prevents scrolling past the beginning or end of the document.

The `deltaY` value varies between browsers and input devices. A typical mouse wheel notch produces a `deltaY` of around 100 CSS pixels in `deltaMode = 0` (pixel mode, the default in most browsers). A trackpad scroll gesture produces smaller values at higher frequency, resulting in smoother scrolling. Some mice use line-mode scrolling (`deltaMode = 1`), where `deltaY` is a line count. We do not check `deltaMode` — we treat all values as pixel amounts, which works well enough for the common cases. A more thorough implementation would check `deltaMode` and convert line-mode deltas to pixel amounts by multiplying by the line height.

We do not apply any momentum or smoothing to the scroll. The scroll position jumps immediately to the new value on each wheel event. Smooth scrolling (where the viewport decelerates after a scroll gesture) would add visual polish but also complexity — we would need to track scroll velocity, apply deceleration each frame, and handle the interaction between momentum scrolling and other scroll sources (keyboard Page Up/Down, scrollbar dragging, auto-scroll during selection). The immediate-jump approach is simple and responsive. On trackpads, the operating system's scroll acceleration already provides a smooth feel, so our immediate-jump approach inherits that smoothness for free.


## 6.7 — Scrollbar Interaction

The scrollbar is a thin vertical track on the right side of the text area with a draggable thumb that indicates the viewport's position within the document. It serves both as a visual indicator (showing where you are in the document) and as an interactive control (allowing direct scroll by dragging).

The scrollbar geometry is computed by `_getScrollbarRect`:

```javascript
_getScrollbarRect() {
  const totalH = this.doc.lineCount * this.lineH;
  if (totalH <= this.textAreaH) return null;
  const ratio = this.textAreaH / totalH;
  const sbH = Math.max(30 * this.dpr, ratio * this.textAreaH);
  const scrollRatio = this.scrollY / (totalH - this.textAreaH);
  const sbY = this.textAreaY
    + scrollRatio * (this.textAreaH - sbH);
  return {
    x: this.canvas.width
       - Math.round(this.scrollbarWidth * this.dpr),
    y: sbY,
    w: Math.round(this.scrollbarWidth * this.dpr),
    h: sbH,
  };
}
```

If the document is shorter than the viewport, no scrollbar is needed — the method returns `null`. Otherwise, the thumb height is proportional to the ratio of viewport height to document height, with a minimum of 30 device pixels so the thumb is always large enough to grab. The thumb position is proportional to the scroll offset within the scrollable range.

When the user clicks on the scrollbar area, the `_onMouseDown` handler detects it by checking whether the click's x-coordinate is in the rightmost `scrollbarWidth` pixels of the canvas:

```javascript
const sbRect = this._getScrollbarRect();
if (sbRect && p.x >= this.canvas.width
    - Math.round(this.scrollbarWidth * this.dpr)) {
  this.isDraggingScrollbar = true;
  if (p.y >= sbRect.y && p.y <= sbRect.y + sbRect.h) {
    this.scrollbarDragOffset = p.y - sbRect.y;
  } else {
    this.scrollbarDragOffset = sbRect.h / 2;
    this._updateScrollFromDrag(p.y);
  }
  return;
}
```

There are two sub-cases. If the click lands on the thumb itself (the `p.y` is within the thumb's vertical range), we record the offset between the mouse and the top of the thumb. This offset is preserved during dragging so the thumb does not "jump" — the point on the thumb that the user grabbed stays under the mouse cursor as they drag.

If the click lands on the scrollbar track but not on the thumb — above or below the thumb — we treat it as a "jump" click. We set the drag offset to half the thumb height (centering the thumb on the click position) and immediately update the scroll. This gives the user a fast way to jump to a different part of the document by clicking on the track.

The `_updateScrollFromDrag` method converts a mouse y-position to a scroll offset:

```javascript
_updateScrollFromDrag(mouseY) {
  const totalH = this.doc.lineCount * this.lineH;
  const sbHeight = Math.max(30 * this.dpr,
    (this.textAreaH / totalH) * this.textAreaH);
  const scrollableTrack = this.textAreaH - sbHeight;
  if (scrollableTrack <= 0) return;
  const ratio = Math.max(0, Math.min(1,
    (mouseY - this.textAreaY - this.scrollbarDragOffset)
    / scrollableTrack));
  this.scrollY = ratio * Math.max(0, totalH - this.textAreaH);
}
```

The "scrollable track" is the portion of the scrollbar track that the thumb can move within — the track height minus the thumb height. The ratio of the mouse position within this range (clamped to `[0, 1]`) maps linearly to the scroll offset. When the ratio is 0, `scrollY` is 0 (top of document). When the ratio is 1, `scrollY` equals the maximum scroll (bottom of document).

The subtraction of `this.scrollbarDragOffset` is what preserves the grab point. If the user grabbed the thumb 10 pixels from its top, the offset is 10, and we subtract 10 from the mouse position before computing the ratio. This means the thumb moves in lockstep with the mouse, with the grab point staying fixed under the cursor.

During dragging, the `_onMouseMove` handler delegates to `_updateScrollFromDrag`:

```javascript
if (this.isDraggingScrollbar) {
  this._updateScrollFromDrag(p.y);
  this.needsRedraw = true;
  return;
}
```

The scrollbar updates on every mouse move, producing smooth, responsive scrolling. When the user releases the mouse, `_onMouseUp` clears `isDraggingScrollbar`, and the scroll stops at its current position.

The scrollbar also provides visual feedback. It has two states: a default appearance and a highlighted appearance when the mouse is hovering over the scrollbar area or when the thumb is being dragged. The drawing code checks for hover:

```javascript
const isHover = this.mouseX >= x;
ctx.fillStyle = (isHover || this.isDraggingScrollbar)
  ? Theme.scrollbarHover : Theme.scrollbar;
```

When the mouse is to the right of the scrollbar's x-position — anywhere over the scrollbar track — the thumb brightens from `Theme.scrollbar` (a subtle gray) to `Theme.scrollbarHover` (a more visible gray). During a drag, the thumb stays bright regardless of mouse position. This feedback tells the user that the scrollbar is interactive and that they can grab it.

The scrollbar thumb is drawn as a rounded rectangle with a small inset from the track edges, giving it a pill shape that looks distinct from the track background. The `_drawRoundRect` helper draws the rounded corners using quadratic Bézier curves:

```javascript
_drawRoundRect(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h - r);
  ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  ctx.lineTo(x + r, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
  ctx.fill();
}
```

This is a standard technique for drawing rounded rectangles on a canvas, since the Canvas 2D API does not have a built-in rounded rectangle function in all browsers. The radius `r` is small — 3 device pixels — producing subtly rounded corners that give the thumb a polished appearance without being distracting.

The scrollbar is one of many details that collectively determine whether an editor feels professional or amateurish. A scrollbar that jumps when clicked, or that has no hover feedback, or that draws with sharp rectangular corners, would not be functionally broken — but it would feel wrong. Getting these small interactions right is what separates software that people enjoy using from software that people merely tolerate.


## 6.8 — Event Binding and Listener Placement

The placement of event listeners — which element they are attached to and with what options — is a design decision that affects correctness and behavior. Let us review where each mouse listener is attached and why.

```javascript
this.canvas.addEventListener("mousedown", (e) => this._onMouseDown(e));
this.canvas.addEventListener("mousemove", (e) => this._onMouseMove(e));
window.addEventListener("mouseup", () => this._onMouseUp());
this.canvas.addEventListener("wheel", (e) => this._onWheel(e),
  { passive: false });
this.canvas.addEventListener("dblclick", (e) => this._onDoubleClick(e));
```

`mousedown` is on the canvas because we only want to handle clicks that originate on our canvas element. A click on a browser UI element or a different part of the page should not affect our editor.

`mousemove` is on the canvas. Mouse movement outside the canvas is generally not relevant to us — we do not need to track hover states when the mouse is on the browser's address bar. However, during a drag selection, the user's mouse might leave the canvas. You might expect that we need `mousemove` on `window` to handle this case, but in practice the auto-scroll behavior handles it: when the mouse is near the edge of the text area, the viewport scrolls, and the selection extends. The mouse does not need to be precisely tracked outside the canvas for this to work.

`mouseup` is on `window`, as we discussed in section 6.3. This is critical for ending drags that finish outside the canvas.

`wheel` is on the canvas with `{ passive: false }`, as discussed in section 6.6.

`dblclick` is on the canvas because double-clicks should only trigger word selection when they happen on our editor.

The drag-and-drop listeners for file opening follow a similar pattern:

```javascript
this.canvas.addEventListener("dragover", (e) => {
  e.preventDefault();
  this.showDropOverlay = true;
  this.needsRedraw = true;
});
this.canvas.addEventListener("dragleave", () => {
  this.showDropOverlay = false;
  this.needsRedraw = true;
});
this.canvas.addEventListener("drop", (e) => {
  e.preventDefault();
  this.showDropOverlay = false;
  this.needsRedraw = true;
  if (e.dataTransfer.files.length > 0)
    this._openFile(e.dataTransfer.files[0]);
});
```

The `dragover` handler must call `e.preventDefault()` — this is a quirk of the HTML drag-and-drop API. Without it, the browser does not recognize the canvas as a valid drop target, and the `drop` event will never fire. The `dragover` handler also shows a visual overlay (a dashed border with a "Drop file to open" message), and `dragleave` hides it. The `drop` handler reads the first file from the `dataTransfer` object and opens it.

The overlay is a full-screen semi-transparent tint with a dashed border, drawn during the `_draw` call when `showDropOverlay` is true. It provides a clear visual signal that the editor is ready to receive a dropped file. The overlay disappears immediately when the user drops the file or drags it away.

The drag-and-drop flow is one of the simplest interactions in the editor because it does not involve any state management beyond the overlay flag. There is no drag state to track, no intermediate position to update, no timing to manage. The user drags a file from their operating system's file manager, the overlay appears, they drop the file, and the editor opens it. The `FileReader` API handles the asynchronous file reading, and the `Doc` constructor handles the rest — splitting the text into lines, detecting the syntax from the filename, tokenizing the content, and initializing the cursor at position (0, 0). It is a complete, self-contained interaction that demonstrates the editor's architecture at its best: a simple gesture triggers a chain of well-encapsulated operations, and the result appears on screen within a single frame.


## 6.9 — What We Have, and What Comes Next

We now have a complete mouse input system. The user can click to place the cursor, drag to create selections, double-click to select words, shift-click to extend selections, scroll with the mouse wheel, drag the scrollbar thumb to navigate, click the scrollbar track to jump, and drop files to open them. Every interaction is precise — the coordinate conversion handles DPR scaling, scroll offsets, gutter width, and character alignment correctly. Every interaction is robust — the `mouseup` listener on `window` prevents stuck drags, the auto-scroll during drag selection prevents viewport-boundary frustration, and the scrollbar drag offset prevents thumb jumping.

Combined with the keyboard input from Chapter 5, the editor now has a complete input layer. The user can interact with the editor using any combination of keyboard and mouse gestures, and the cursor, selection, text, and scroll state respond correctly.

In Chapter 7, we will turn our attention to the visual layer that makes code readable: syntax highlighting. We will build a regex-based tokenizer that splits each line into colored tokens, a syntax definition system that describes the patterns and keywords of ten programming languages, and an incremental highlighting cache that updates efficiently as the user edits. The tokenizer is the bridge between the raw text in the document and the colored runs that the font atlas draws — it is the system that transforms a wall of monochrome characters into a structured, readable display of code.

With the input layer (Chapters 5 and 6) complete, the editor is already fully functional — you can open files, edit them, navigate with keyboard and mouse, select and manipulate text, undo and redo, and save. What syntax highlighting adds is not functionality but readability. It turns text into code.

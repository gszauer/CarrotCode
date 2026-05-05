# Chapter 5: Keyboard Input and Navigation

*Handling every keypress — character insertion, arrow keys, Home/End, word jumping, page up/down, smart indent, and all the keyboard shortcuts.*

---

## 5.1 — The `keydown` Event Handler

The keyboard is the primary input device for a text editor. Every character the user types, every cursor movement, every shortcut — they all arrive as keyboard events. Getting this right means handling dozens of keys, each with up to three modifier combinations (Ctrl, Shift, Ctrl+Shift), each with different behavior depending on whether a selection is active. It is the most branching, most conditional code in the entire editor.

We listen for the `keydown` event on the `window` object:

```javascript
window.addEventListener("keydown", (e) => this._onKeyDown(e));
```

Why `keydown` and not `keypress` or `keyup`? The `keypress` event is deprecated and does not fire for non-printable keys like arrow keys, Backspace, Delete, Home, End, or function keys. The `keyup` event fires when the key is released, not when it is pressed, which introduces a perceptible delay and does not support auto-repeat (holding a key down). The `keydown` event fires immediately when the key is pressed, fires repeatedly when the key is held down, and fires for all keys including non-printable ones. It is the right event for a text editor.

From the event object, we extract four pieces of information:

```javascript
const ctrl = e.ctrlKey || e.metaKey;
const shift = e.shiftKey;
const key = e.key;
```

The `key` property is a string that identifies which key was pressed. For printable characters, it is the character itself: `"a"`, `"X"`, `"3"`, `"{"`. For special keys, it is a descriptive name: `"ArrowLeft"`, `"Backspace"`, `"Enter"`, `"Tab"`, `"Home"`, `"Escape"`. We use `e.ctrlKey || e.metaKey` for the control modifier so that Ctrl on Windows/Linux and Cmd on macOS both work. On macOS, the Command key sets `metaKey`, not `ctrlKey`, and users expect Cmd+Z, Cmd+S, Cmd+C, and so on to work the same as Ctrl+Z, Ctrl+S, Ctrl+C on other platforms.

Almost every branch in the key handler calls `e.preventDefault()`. This is essential. Without it, the browser would perform its default action for the key: Ctrl+S would open the Save dialog for the web page, Ctrl+N would open a new browser tab, Tab would move focus to the next DOM element, Backspace might navigate the browser back. By preventing the default action, we ensure that every keystroke is handled by our editor and only by our editor.

The handler is structured as a priority cascade. The first check is whether a dropdown menu is open — if so, keyboard input is routed to the menu navigation handler and the text area is ignored. After that, the cascade proceeds in order: keyboard shortcuts (Ctrl+key combinations), navigation keys (arrows, Home, End, Page Up/Down), special keys (Tab, Enter, Backspace, Delete, Escape), and finally printable characters. This ordering ensures that Ctrl+A is handled as "select all" before the `key.length === 1` check could treat it as the character "a".

```javascript
_onKeyDown(e) {
  if (this.activeMenu >= 0) {
    this._handleMenuKeyDown(e);
    return;
  }

  const ctrl = e.ctrlKey || e.metaKey;
  const shift = e.shiftKey;
  const key = e.key;

  // 1. Keyboard shortcuts (Ctrl held)
  if (ctrl) { /* ... */ }

  // 2. Navigation keys
  if (navKeys.indexOf(key) >= 0) { /* ... */ }

  // 3. Tab
  if (key === "Tab") { /* ... */ }

  // 4. Enter
  if (key === "Enter") { /* ... */ }

  // 5. Backspace
  if (key === "Backspace") { /* ... */ }

  // 6. Delete
  if (key === "Delete") { /* ... */ }

  // 7. Escape
  if (key === "Escape") { /* ... */ }

  // 8. Printable character
  if (key.length === 1 && !ctrl) { /* ... */ }
}
```

Each branch calls `e.preventDefault()`, performs the appropriate action, sets `this.needsRedraw = true`, resets the cursor blink timer if applicable, calls `_ensureCursorVisible` if the cursor moved, and returns. The early returns prevent falling through to later branches.

The very first check in the handler — whether a dropdown menu is open — deserves emphasis. When the user opens a dropdown menu (by clicking File, Edit, or View), the keyboard temporarily switches to "menu mode." Arrow keys navigate the menu items instead of the cursor. Enter selects the hovered item instead of inserting a newline. Escape closes the menu instead of clearing a selection. The `_handleMenuKeyDown` method handles all of these, and the rest of the key handler is skipped entirely. This is a clean separation: the menu has exclusive keyboard focus when it is open, and the text area has exclusive keyboard focus when it is not.

```javascript
if (this.activeMenu >= 0) {
  this._handleMenuKeyDown(e);
  return;
}
```

The `return` after `_handleMenuKeyDown` is critical. Without it, a key like "Enter" would both select the menu item and insert a newline in the text. The early return ensures that menu and text input are never both active at the same time.

This pattern — checking for modal state at the top of the handler and dispatching to a different handler — is a common solution to the modal input problem. Rather than threading "is a menu open?" checks through every branch of the key handler, we intercept all keys at the top and route them elsewhere. This keeps the main handler clean and focused on text editing.


## 5.2 — Keyboard Shortcuts

The first thing the handler checks, after ruling out menu navigation, is whether the Ctrl key (or Cmd on macOS) is held. If so, we enter the shortcut handling block:

```javascript
if (ctrl) {
  switch (key.toLowerCase()) {
    case "a": e.preventDefault(); this._selectAll(); return;
    case "z":
      e.preventDefault();
      if (shift) this.doc.redo(); else this.doc.undo();
      this.needsRedraw = true;
      this._ensureCursorVisible();
      return;
    case "y":
      e.preventDefault();
      this.doc.redo();
      this.needsRedraw = true;
      this._ensureCursorVisible();
      return;
    case "s": e.preventDefault(); this._saveFile(); return;
    case "n":
      e.preventDefault();
      this._executeMenuAction("new");
      return;
    case "o":
      e.preventDefault();
      document.getElementById("fileInput").click();
      return;
    case "d":
      e.preventDefault();
      this._duplicateLine();
      this.needsRedraw = true;
      return;
    case "=": case "+":
      e.preventDefault();
      this._executeMenuAction("zoomIn");
      return;
    case "-": case "_":
      e.preventDefault();
      this._executeMenuAction("zoomOut");
      return;
  }
}
```

We use `key.toLowerCase()` so that Ctrl+Z and Ctrl+Shift+Z both match the `"z"` case. The shift modifier is checked inside the case to distinguish undo (Ctrl+Z) from redo (Ctrl+Shift+Z). We also support Ctrl+Y as an alternative redo shortcut, since some users expect it from Windows editors.

Each shortcut is self-contained. Ctrl+A calls `_selectAll()`, which sets the selection from `(0, 0)` to the end of the last line. Ctrl+S calls `_saveFile()`, which creates a Blob from the document text and triggers a download. Ctrl+N creates a new empty document. Ctrl+O triggers the hidden file input's click event, which opens the browser's native file picker dialog. Ctrl+D calls `_duplicateLine()`, which inserts a copy of the current line below it.

The zoom shortcuts Ctrl+= and Ctrl+- adjust `Config.fontSize` and rebuild the font atlas. We match both `"="` and `"+"` for zoom in, because on most keyboards the plus sign requires Shift, and we want Ctrl+= (without Shift) to work as well. Similarly, we match both `"-"` and `"_"` for zoom out.

The `_duplicateLine` method is a small convenience that is surprisingly useful in practice:

```javascript
_duplicateLine() {
  const l = this.doc.cursorLine;
  const lineText = this.doc.getLine(l);
  this.doc.insertText(l, this.doc.getLine(l).length, "\n" + lineText);
  this.doc.setCursor(l + 1, this.doc.cursorCol);
  this._computeGutter();
}
```

It inserts a newline followed by the current line's text at the end of the current line. The cursor moves down one line, staying at the same column. This creates an exact copy of the line below it, which is useful for writing repetitive code. The insertion is recorded on the undo stack, so Ctrl+Z undoes the duplication.


## 5.3 — Arrow Key Navigation

Arrow keys are the most frequently used navigation keys. They move the cursor one character, one line, or one word at a time, with or without extending a selection. The implementation has to handle several interacting concerns: shift for selection, ctrl for word jumping, desired column for vertical movement, and the special behavior of collapsing a selection.

The navigation key handling begins with a preamble that is shared by all eight navigation keys:

```javascript
const navKeys = ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown",
                 "Home", "End", "PageUp", "PageDown"];
if (navKeys.indexOf(key) >= 0) {
  e.preventDefault();
  if (shift && !this.doc.selectionActive) {
    this.doc.startSelection();
  }

  // Handle non-shift arrow with active selection: jump to edge
  if (!shift && this.doc.hasSelection()) {
    const sel = this.doc.getNormalizedSelection();
    if (key === "ArrowLeft" || key === "Home" || key === "ArrowUp") {
      this.doc.setCursor(sel.fromLine, sel.fromCol);
    } else {
      this.doc.setCursor(sel.toLine, sel.toCol);
    }
    this.doc.clearSelection();
    this.cursorBlink = 0;
    this.needsRedraw = true;
    this._ensureCursorVisible();
    if (key === "ArrowLeft" || key === "ArrowRight") return;
  }

  this._handleNavigation(key, ctrl, shift);
  if (shift) {
    this.doc.updateSelectionEnd();
  } else {
    this.doc.clearSelection();
  }
  this.cursorBlink = 0;
  this.needsRedraw = true;
  this._ensureCursorVisible();
  return;
}
```

The preamble does three things. First, if Shift is held and no selection is active, it starts one — anchoring the selection start at the current cursor position. Second, if Shift is *not* held and a selection *is* active, it collapses the selection by jumping the cursor to the appropriate edge. Left-moving keys jump to the start of the selection; right-moving keys jump to the end. This is the standard behavior in every text editor: pressing Left with a selection active moves to the beginning of the selection, not one character left of the cursor. For Left and Right arrows, the handler returns after collapsing — the navigation itself is skipped because the collapse *is* the navigation. For Up, Down, Home, End, PageUp, and PageDown, the handler continues after collapsing, because those keys should both collapse the selection and perform their navigation.

After the preamble, `_handleNavigation` performs the actual cursor movement:

```javascript
_handleNavigation(key, ctrl) {
  const doc = this.doc;
  const line = doc.cursorLine;
  const col = doc.cursorCol;

  switch (key) {
    case "ArrowLeft":
      if (ctrl) {
        const wb = doc.wordBoundaryLeft(line, col);
        doc.setCursor(wb.line, wb.col);
      } else {
        if (col > 0) doc.setCursor(line, col - 1);
        else if (line > 0)
          doc.setCursor(line - 1, doc.getLine(line - 1).length);
      }
      break;
    case "ArrowRight":
      if (ctrl) {
        const wb = doc.wordBoundaryRight(line, col);
        doc.setCursor(wb.line, wb.col);
      } else {
        if (col < doc.getLine(line).length)
          doc.setCursor(line, col + 1);
        else if (line < doc.lineCount - 1)
          doc.setCursor(line + 1, 0);
      }
      break;
    case "ArrowUp":
      doc.setCursor(line - 1, doc.desiredCol, false);
      break;
    case "ArrowDown":
      doc.setCursor(line + 1, doc.desiredCol, false);
      break;
    // ... Home, End, PageUp, PageDown ...
  }
}
```

Left and Right without Ctrl move one character. If the cursor is at the beginning of a line and Left is pressed, the cursor wraps to the end of the previous line. If the cursor is at the end of a line and Right is pressed, the cursor wraps to the beginning of the next line. This wrapping behavior is what users expect — pressing Left at column 0 should not simply do nothing.

Left and Right with Ctrl use the word boundary functions from Chapter 3. Ctrl+Left jumps to the beginning of the previous word. Ctrl+Right jumps to the beginning of the next word. These functions handle whitespace skipping and line wrapping internally, so the navigation code just calls them and sets the cursor.

Up and Down move to the previous or next line at the desired column, passing `false` for `updateDesired` so the column intent is preserved. As we discussed in Chapter 3, this is what makes vertical navigation feel natural through lines of varying length.

After `_handleNavigation` returns, the preamble's postamble runs. If Shift was held, `updateSelectionEnd` extends the selection to the new cursor position. If Shift was not held, `clearSelection` ensures no selection is active. The cursor blink timer is reset to 0 (making the cursor immediately visible), and `_ensureCursorVisible` adjusts the scroll if the cursor has moved off-screen.

The interaction between selection and navigation is one of the places where a text editor's behavior has to be exactly right, or the user will feel constant friction. Let us trace through a specific scenario to see how the pieces fit together.

The user clicks at line 3, column 5 to place the cursor. Then they hold Shift and press Right three times, then Down once, then Right twice. Here is what happens at each step:

1. **Click at (3, 5):** `clearSelection` is called. Cursor is at (3, 5). No selection.
2. **Shift+Right:** `startSelection` is called (first Shift key press with no active selection), anchoring the start at (3, 5). `_handleNavigation` moves the cursor to (3, 6). `updateSelectionEnd` sets the selection end to (3, 6). Selection: (3, 5) to (3, 6).
3. **Shift+Right:** Selection is already active, so `startSelection` is not called again. Cursor moves to (3, 7). Selection end updates to (3, 7). Selection: (3, 5) to (3, 7).
4. **Shift+Right:** Same pattern. Cursor to (3, 8). Selection: (3, 5) to (3, 8).
5. **Shift+Down:** Cursor moves to (4, 8) (or clamped to line 4's length). Selection end updates. Selection: (3, 5) to (4, 8).
6. **Shift+Right:** Cursor to (4, 9). Selection: (3, 5) to (4, 9).
7. **Shift+Right:** Cursor to (4, 10). Selection: (3, 5) to (4, 10).

Now the user presses Left (without Shift). The selection collapse logic runs: there is a selection, Shift is not held, and the key is ArrowLeft (a left-moving key). The cursor jumps to the start of the normalized selection — (3, 5) — and the selection is cleared. The user is now back at the beginning of what was selected, with no selection active. If they had pressed Right instead, the cursor would have jumped to (4, 10), the end of the selection.

This collapsing behavior is essential. Without it, pressing Left with a selection active would move the cursor one character left from its current position (4, 10) to (4, 9), which feels wrong — the user expects the selection to disappear and the cursor to appear at the appropriate edge. Every major text editor implements this collapsing behavior, and users rely on it unconsciously.


## 5.4 — Home, End, Page Up, Page Down

The Home key has a particularly clever behavior called "smart Home." Instead of always jumping to column 0, it toggles between two positions: the first non-whitespace character on the line, and column 0.

```javascript
case "Home":
  if (ctrl) { doc.setCursor(0, 0); }
  else {
    const lineText = doc.getLine(line);
    let firstNonWs = 0;
    while (firstNonWs < lineText.length
           && (lineText[firstNonWs] === " "
               || lineText[firstNonWs] === "\t"))
      firstNonWs++;
    doc.setCursor(line, col === firstNonWs ? 0 : firstNonWs);
  }
  break;
```

If the cursor is not at the first non-whitespace character, Home jumps to it. If the cursor is already there, Home jumps to column 0. This means pressing Home twice always gets you to column 0, but the first press takes you to the meaningful start of the line — past the indentation. For code editing, this is far more useful than always jumping to column 0, because the indentation is rarely what you want to edit.

The implementation scans from the left to find the first non-whitespace character. If `col` is already at that position, it jumps to 0. Otherwise, it jumps to `firstNonWs`. If the line is entirely whitespace (or empty), `firstNonWs` equals the line length, and Home still works correctly — it jumps to the end of the whitespace, and a second press jumps to 0.

Ctrl+Home jumps to the very beginning of the document — line 0, column 0. This is unconditional and ignores the smart Home logic.

End is simpler — it jumps to the end of the current line:

```javascript
case "End":
  if (ctrl)
    doc.setCursor(doc.lineCount - 1,
      doc.getLine(doc.lineCount - 1).length);
  else
    doc.setCursor(line, doc.getLine(line).length);
  break;
```

Ctrl+End jumps to the very end of the document — the last column of the last line.

Page Up and Page Down move by a screenful of lines:

```javascript
case "PageUp": {
  const n = Math.floor(this.textAreaH / this.lineH);
  doc.setCursor(line - n, doc.desiredCol, false);
  this.scrollY = Math.max(0, this.scrollY - n * this.lineH);
  break;
}
case "PageDown": {
  const n = Math.floor(this.textAreaH / this.lineH);
  doc.setCursor(line + n, doc.desiredCol, false);
  const max = Math.max(0,
    doc.lineCount * this.lineH - this.textAreaH);
  this.scrollY = Math.min(max, this.scrollY + n * this.lineH);
  break;
}
```

The number of lines per page is `Math.floor(this.textAreaH / this.lineH)` — the number of full lines that fit in the text area. Both the cursor and the scroll position move by this amount. The cursor uses `desiredCol` with `updateDesired = false`, just like Up and Down. The scroll is clamped to `[0, maxScroll]`.

Note that Page Up and Page Down adjust `scrollY` directly, rather than relying on `_ensureCursorVisible`. This is because `_ensureCursorVisible` adjusts scroll minimally — it brings the cursor just inside the viewport. For paging, we want the entire viewport to shift by a full page, not just enough to show the cursor. The direct scroll adjustment gives the user the expected behavior: the screen "pages" up or down, and the cursor follows.

All four of these keys (Home, End, PageUp, PageDown) participate in the same selection logic as the arrow keys. Shift+Home selects from the cursor to the Home position. Shift+PageDown selects a full page of text. The preamble and postamble handle this uniformly for all navigation keys.


## 5.5 — Tab and Indentation

The Tab key has four behaviors, depending on the state of the selection and the Shift modifier:

```javascript
if (key === "Tab") {
  e.preventDefault();
  if (this.doc.hasSelection() && !shift) {
    this._indentSelection();
  } else if (this.doc.hasSelection() && shift) {
    this._outdentSelection();
  } else if (shift) {
    this._outdentCurrentLine();
  } else {
    this._insertTextAtCursor(" ".repeat(Config.tabSize));
  }
  this.needsRedraw = true;
  return;
}
```

Without a selection and without Shift, Tab inserts `Config.tabSize` spaces (defaulting to 4) at the cursor position. We insert spaces rather than a tab character because spaces render predictably in a monospace font — each space is one character width. Tab characters would require computing tab stops, which adds complexity for little benefit in a code editor.

With a selection and without Shift, Tab indents every selected line. The `_indentSelection` method inserts spaces at the beginning of each line:

```javascript
_indentSelection() {
  const sel = this.doc.getNormalizedSelection();
  const spaces = " ".repeat(Config.tabSize);
  for (let i = sel.toLine; i >= sel.fromLine; i--) {
    this.doc.insertText(i, 0, spaces);
  }
}
```

Notice that the loop iterates backwards, from `toLine` to `fromLine`. This is important. If we iterated forwards, inserting spaces at line `fromLine` would shift the line indices of all subsequent lines — but our selection coordinates refer to the original line indices. By going backwards, each insertion only affects lines that have already been processed.

Each `insertText` call creates its own undo entry. This means that indenting a ten-line selection creates ten undo entries. Pressing Ctrl+Z will undo one line of indentation at a time. A more polished implementation could group these into a single compound undo action, but the simple approach works and avoids adding complexity to the undo system.

With a selection and Shift held, Shift+Tab outdents every selected line:

```javascript
_outdentSelection() {
  const sel = this.doc.getNormalizedSelection();
  for (let i = sel.fromLine; i <= sel.toLine; i++) {
    const line = this.doc.getLine(i);
    let remove = 0;
    for (let j = 0; j < Config.tabSize && j < line.length; j++) {
      if (line[j] === " ") remove++;
      else break;
    }
    if (remove > 0) this.doc.deleteRange(i, 0, i, remove);
  }
}
```

For each line, we count how many leading spaces there are, up to `Config.tabSize`. If there are any, we delete them. A line with two leading spaces gets two removed, not four — we only remove up to the tab stop, and we stop at the first non-space character.

Without a selection and with Shift, Shift+Tab outdents the current line:

```javascript
_outdentCurrentLine() {
  const l = this.doc.cursorLine;
  const line = this.doc.getLine(l);
  let remove = 0;
  for (let j = 0; j < Config.tabSize && j < line.length; j++) {
    if (line[j] === " ") remove++;
    else break;
  }
  if (remove > 0) {
    this.doc.deleteRange(l, 0, l, remove);
    this.doc.setCursor(l, Math.max(0, this.doc.cursorCol - remove));
  }
}
```

This is the same logic as `_outdentSelection` but for a single line, with the additional step of adjusting the cursor column. If four spaces are removed from the beginning of the line, the cursor shifts left by four columns. The `Math.max(0, ...)` prevents the cursor from going negative if it was within the removed spaces.

The `e.preventDefault()` call for Tab is especially important. Without it, pressing Tab would move the browser's focus to the next focusable element on the page (the hidden file input), which would take keyboard input away from our editor.


## 5.6 — Enter and Auto-Indent

When the user presses Enter, we want to insert a new line and carry the current line's indentation to the new line. If the cursor is after an opening brace, colon, or parenthesis, we want to add an extra level of indentation. This is auto-indent — a simple heuristic that handles the most common cases correctly.

```javascript
if (key === "Enter") {
  e.preventDefault();
  const line = this.doc.getLine(this.doc.cursorLine);
  let indent = "";
  for (let i = 0; i < line.length; i++) {
    if (line[i] === " " || line[i] === "\t") indent += line[i];
    else break;
  }
  const beforeCursor =
    line.substring(0, this.doc.cursorCol).trimEnd();
  if (beforeCursor.endsWith("{")
      || beforeCursor.endsWith(":")
      || beforeCursor.endsWith("(")) {
    indent += " ".repeat(Config.tabSize);
  }
  this._insertTextAtCursor("\n" + indent);
  this.needsRedraw = true;
  this._ensureCursorVisible();
  return;
}
```

First, we scan the current line from the left to collect its leading whitespace. This produces a string like `"    "` (four spaces) or `"  "` (two spaces) or `""` (no indentation). This is the base indentation that will be carried to the new line.

Then we check whether the text to the left of the cursor (trimmed of trailing spaces) ends with `{`, `:`, or `(`. These are the characters that typically introduce a new block of indented code: `{` in C-like languages, `:` in Python, and `(` for multi-line function arguments. If so, we add one more level of indentation — `Config.tabSize` spaces.

Finally, we insert `"\n" + indent` at the cursor. The `_insertTextAtCursor` method handles deleting any active selection first, performing the insertion, and moving the cursor to the end of the inserted text. The cursor ends up at the end of the indentation on the new line, ready for the user to start typing the indented content.

This is a simple heuristic, not a full understanding of the language's syntax. It does not handle cases like inserting a closing brace and automatically outdenting, or handling `else` at the same indentation as the preceding `if`. But it covers the vast majority of cases correctly and does so with a few lines of code. More sophisticated auto-indent would require understanding the language's grammar, which is the domain of language servers and is beyond our scope.

The trimming of trailing whitespace before the cursor — `line.substring(0, this.doc.cursorCol).trimEnd()` — is a small but important detail. If the cursor is after `function hello() {   ` (with trailing spaces), we still want the extra indentation, because the significant character before the cursor is `{`. Without the trim, the trailing spaces would make `endsWith("{")` return false.

Let us trace through a concrete example. The user is editing JavaScript and the cursor is at the end of this line, which has four spaces of indentation:

```
    if (x > 0) {
```

The leading whitespace scan produces `indent = "    "` (four spaces). The text before the cursor is `"    if (x > 0) {"`, which trimmed is `"    if (x > 0) {"`. This ends with `{`, so we add four more spaces: `indent = "        "` (eight spaces). The inserted text is `"\n        "` — a newline followed by eight spaces. The cursor lands at column 8 of the new line, perfectly indented for the block body.

If the user then types `return x;` and presses Enter again, the leading whitespace of the current line is eight spaces, the text before the cursor (`"        return x;"`) does not end with `{`, `:`, or `(`, so the indent stays at eight spaces. The new line is indented at the same level as the return statement, which is correct.

Note that the entire insertion — the newline plus the indentation — is a single call to `_insertTextAtCursor`, which creates a single undo entry. Pressing Ctrl+Z removes the newline and the indentation together, returning the cursor to its position before the Enter was pressed.


## 5.7 — Backspace and Delete

Backspace and Delete are the two destructive keys. Backspace removes content to the left of the cursor; Delete removes content to the right. Both have multiple behaviors depending on the modifier keys and the editor state.

The `_handleBackspace` method:

```javascript
_handleBackspace(ctrl) {
  if (this.doc.hasSelection()) {
    this.doc.deleteSelection();
    return;
  }
  const l = this.doc.cursorLine;
  const c = this.doc.cursorCol;
  if (ctrl) {
    const wb = this.doc.wordBoundaryLeft(l, c);
    this.doc.deleteRange(wb.line, wb.col, l, c);
    this.doc.setCursor(wb.line, wb.col);
  } else if (c > 0) {
    const line = this.doc.getLine(l);
    const before = line.substring(0, c);
    let deleteCount = 1;
    if (before.trimEnd().length === 0 && before.length > 0) {
      deleteCount = ((c - 1) % Config.tabSize) + 1;
      deleteCount = Math.min(deleteCount, c);
    }
    this.doc.deleteRange(l, c - deleteCount, l, c);
    this.doc.setCursor(l, c - deleteCount);
  } else if (l > 0) {
    const prevLen = this.doc.getLine(l - 1).length;
    this.doc.deleteRange(l - 1, prevLen, l, 0);
    this.doc.setCursor(l - 1, prevLen);
  }
  this._computeGutter();
}
```

The first check is always for an active selection. If text is selected, Backspace deletes the selection and returns. This is the same pattern we saw in Chapter 4 — selection takes priority over character-level operations.

With Ctrl held, Backspace deletes from the cursor to the previous word boundary. This is word-level deletion, and it uses the `wordBoundaryLeft` function from Chapter 3. The range from the word boundary to the cursor is deleted, and the cursor moves to the word boundary.

Without Ctrl, and with the cursor in the middle or end of a line (`c > 0`), Backspace enters the smart deletion logic. The default behavior is to delete one character. But if the text before the cursor is entirely whitespace (spaces or tabs with nothing else), we compute a "smart" delete count that aligns to the previous tab stop.

The formula `((c - 1) % Config.tabSize) + 1` computes how many characters to delete to reach the previous tab stop. If `Config.tabSize` is 4 and the cursor is at column 6, `((6 - 1) % 4) + 1 = (5 % 4) + 1 = 1 + 1 = 2`. So we delete 2 spaces, bringing the cursor from column 6 to column 4 — the previous tab stop. If the cursor is at column 4, `((4 - 1) % 4) + 1 = (3 % 4) + 1 = 3 + 1 = 4`. So we delete 4 spaces, bringing the cursor from column 4 to column 0. This makes Backspace on indentation feel like the inverse of Tab — pressing Tab adds 4 spaces, and pressing Backspace on those spaces removes up to 4 at a time, snapping to tab stops.

Let us trace through the formula with `Config.tabSize = 4` at various cursor positions, assuming the line is all whitespace:

- Column 8: `((8-1) % 4) + 1 = (7 % 4) + 1 = 3 + 1 = 4`. Delete 4, move to column 4.
- Column 7: `((7-1) % 4) + 1 = (6 % 4) + 1 = 2 + 1 = 3`. Delete 3, move to column 4.
- Column 6: `((6-1) % 4) + 1 = (5 % 4) + 1 = 1 + 1 = 2`. Delete 2, move to column 4.
- Column 5: `((5-1) % 4) + 1 = (4 % 4) + 1 = 0 + 1 = 1`. Delete 1, move to column 4.
- Column 4: `((4-1) % 4) + 1 = (3 % 4) + 1 = 3 + 1 = 4`. Delete 4, move to column 0.
- Column 3: `((3-1) % 4) + 1 = (2 % 4) + 1 = 2 + 1 = 3`. Delete 3, move to column 0.
- Column 2: `((2-1) % 4) + 1 = (1 % 4) + 1 = 1 + 1 = 2`. Delete 2, move to column 0.
- Column 1: `((1-1) % 4) + 1 = (0 % 4) + 1 = 0 + 1 = 1`. Delete 1, move to column 0.

The pattern is clear: from any column on an all-whitespace line, Backspace jumps to the nearest tab stop to the left. Columns 1–4 all snap back to column 0. Columns 5–8 all snap back to column 4. This is exactly the behavior users expect when working with indentation.

The check `before.trimEnd().length === 0 && before.length > 0` is the guard that activates smart backspace only when the text before the cursor is entirely whitespace. If the cursor is at column 6 and the line is `"  hello "`, the text before column 6 is `"  hell"`, which has non-whitespace characters, so smart backspace does not activate — we delete one character as usual. Smart backspace is only for indentation, not for spaces that appear within or after code.

The `Math.min(deleteCount, c)` clamp prevents deleting more characters than exist before the cursor. If the cursor is at column 2, we cannot delete 4 characters. We delete 2.

If the cursor is at column 0 and not on the first line, Backspace joins the current line with the previous line. We delete the range from the end of the previous line to the beginning of the current line — this range contains exactly the newline character. The cursor moves to the end of the previous line (where the join happened).

The `_handleDelete` method is the forward-direction mirror:

```javascript
_handleDelete(ctrl) {
  if (this.doc.hasSelection()) {
    this.doc.deleteSelection();
    return;
  }
  const l = this.doc.cursorLine;
  const c = this.doc.cursorCol;
  if (ctrl) {
    const wb = this.doc.wordBoundaryRight(l, c);
    this.doc.deleteRange(l, c, wb.line, wb.col);
  } else {
    const lineLen = this.doc.getLine(l).length;
    if (c < lineLen) {
      this.doc.deleteRange(l, c, l, c + 1);
    } else if (l < this.doc.lineCount - 1) {
      this.doc.deleteRange(l, c, l + 1, 0);
    }
  }
  this._computeGutter();
}
```

With Ctrl, Delete removes from the cursor to the next word boundary. Without Ctrl, it deletes one character to the right, or — if the cursor is at the end of a line — it joins the current line with the next line by deleting the newline between them.

Note that Delete does not have a "smart" tab-stop-aligned behavior like Backspace does. This is a deliberate choice that matches most editors. Backspace is used far more frequently on indentation (to reduce indent level), while Delete at the end of a line is relatively rare. The asymmetry is pragmatic.

Both methods call `_computeGutter()` at the end, because deleting text might reduce the line count, which could change the number of digits needed in the gutter.


## 5.8 — Ensuring the Cursor Stays Visible

After every cursor movement and every edit, we call `_ensureCursorVisible` to make sure the cursor is within the visible viewport. Without this, the user could type or navigate off the bottom of the screen and lose sight of where they are.

```javascript
_ensureCursorVisible() {
  const cursorY = this.doc.cursorLine * this.lineH;
  const cursorX = this.doc.cursorCol * this.charW;

  // Vertical
  if (cursorY < this.scrollY) {
    this.scrollY = cursorY;
  } else if (cursorY + this.lineH > this.scrollY + this.textAreaH) {
    this.scrollY = cursorY + this.lineH - this.textAreaH;
  }

  // Horizontal
  const effectiveX = cursorX + Config.textPaddingLeft * this.dpr;
  if (effectiveX - this.scrollX < 0) {
    this.scrollX = Math.max(0, effectiveX - this.charW * 4);
  } else if (effectiveX - this.scrollX
             > this.textAreaW - this.charW * 2) {
    this.scrollX = effectiveX - this.textAreaW + this.charW * 8;
  }

  this.needsRedraw = true;
}
```

The vertical check is straightforward. If the cursor is above the viewport (`cursorY < this.scrollY`), we scroll up so the cursor line is at the top. If the cursor is below the viewport (`cursorY + this.lineH > this.scrollY + this.textAreaH`), we scroll down so the cursor line is at the bottom. This minimal adjustment keeps the viewport as stable as possible — it only scrolls as much as necessary to bring the cursor into view, rather than centering the cursor on screen.

The horizontal check includes padding. When the cursor moves past the right edge, we scroll right by enough to place the cursor eight characters from the right edge (`this.charW * 8`). This extra padding prevents the cursor from hugging the edge, giving the user visibility into the text ahead. When the cursor moves past the left edge, we scroll left by enough to place the cursor four characters from the left edge (`this.charW * 4`). The left padding is smaller because there is usually less text to the left of the cursor that the user needs to see.

The `effectiveX` variable accounts for the text padding — the gap between the gutter and the text. Without this, the calculation would be off by the padding amount, and the cursor could be partially hidden behind the gutter edge.

This method is called from almost everywhere: after handling arrow keys, after inserting text, after Backspace and Delete, after undo and redo, after Page Up and Page Down. The only navigation operations that do *not* call it are the ones that manage scroll directly (Page Up/Down, which set `scrollY` explicitly) and mouse clicks (which set the cursor to a position that is already visible, since the user clicked on it).

The method always sets `needsRedraw = true`, even if the scroll did not change. This is slightly wasteful in the case where the cursor is already visible, but it simplifies the calling code — callers do not need to check whether the scroll changed. The cost of an unnecessary redraw (setting a flag and checking it on the next frame) is negligible.


## 5.9 — Printable Characters

At the bottom of the key handler cascade, after all special keys have been checked, we handle printable characters:

```javascript
if (key.length === 1 && !ctrl) {
  e.preventDefault();
  this._insertTextAtCursor(key);
  this.needsRedraw = true;
  this._ensureCursorVisible();
}
```

The condition `key.length === 1` identifies printable characters. Non-printable keys like `"ArrowLeft"`, `"Backspace"`, and `"Shift"` have names longer than one character. The `!ctrl` check ensures that we do not treat Ctrl+key combinations as character insertions — Ctrl+A should select all, not insert the character "a."

The `_insertTextAtCursor` method handles the rest: deleting any active selection, inserting the character at the cursor position, and moving the cursor one column to the right. The insertion is recorded on the undo stack and will be merged with adjacent insertions by the `_pushUndo` logic from Chapter 4.

This catch-all handler covers all printable characters — letters, digits, punctuation, spaces, and even Unicode characters that the user might type via an input method. The `key` property of the event already contains the correct character, so we do not need to do any character code conversion.

One thing we do not handle here is composition input — the input method editors (IMEs) used for Chinese, Japanese, Korean, and other scripts that require multi-keystroke character composition. IME input generates `compositionstart`, `compositionupdate`, and `compositionend` events, which we do not listen for. This means our editor does not support IME input. For a code editor that will primarily be used with ASCII-centric programming languages, this is an acceptable limitation. Adding IME support would require managing a composition buffer, rendering the in-progress composition with a different visual treatment, and handling the interaction between composition and our existing selection and undo systems.


## 5.10 — What We Have, and What Comes Next

We now have a complete keyboard input system. Every key on the keyboard is handled: printable characters are inserted, arrow keys navigate with optional word jumping and selection extension, Home and End move to line boundaries with smart toggling, Page Up and Page Down scroll by screenfuls, Tab handles indentation with four different behaviors, Enter inserts new lines with auto-indent, Backspace and Delete remove text with word-level and tab-stop-aligned variants, Escape clears the selection and closes menus, and keyboard shortcuts provide access to undo, redo, save, open, select all, duplicate line, and zoom.

The keyboard is one half of the input story. In Chapter 6, we will build the other half: mouse input. Clicking to place the cursor, dragging to create selections, double-clicking to select words, shift-clicking to extend selections, scrolling with the mouse wheel, and dragging the scrollbar — these are the essential interactions that make the editor feel like a visual, interactive application rather than a command-line program.

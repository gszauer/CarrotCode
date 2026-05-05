# Chapter 3: The Document Model

*How text is stored, addressed, and manipulated — the `Doc` class that holds lines, cursor state, and selection state.*

---

## 3.1 — Lines as the Fundamental Unit

Every text editor needs a data structure to hold the text. This choice is one of the most consequential in the entire design, because it determines the performance characteristics of every operation — insertion, deletion, search, rendering — for the lifetime of the editor. The computer science literature offers several sophisticated options: ropes (balanced binary trees of strings), gap buffers (arrays with a movable gap at the cursor), piece tables (sequences of references into original and append-only buffers), and more. Each has trade-offs in asymptotic complexity, cache behavior, memory fragmentation, and implementation difficulty.

We are going to use an array of strings. Each element in the array is one line of text.

This is the same approach that *lite* takes. In *lite*, all loaded text files are stored in `Doc` objects, and each doc keeps a table of lines in `Doc.lines`. The table is a Lua array where each element is a string containing one line of text. Insert a line and you insert an element into the table. Delete a line and you remove an element. Modify a line and you replace the string at that index.

The simplicity of this approach is its strength. Accessing line `n` is an array index — `this.lines[n]` — which is an O(1) operation. Iterating over lines is a sequential scan through a contiguous array, which is cache-friendly. Inserting or deleting lines uses `Array.splice`, which is O(n) in the worst case because it shifts elements, but for the file sizes that a lightweight code editor typically handles (tens of thousands of lines), this is fast enough. JavaScript engines are heavily optimized for array operations, and the constant factors are small.

The alternatives have better asymptotic complexity for certain operations but come with significant implementation cost. A rope gives you O(log n) insertions and deletions, but you have to implement or import a balanced tree data structure, handle rebalancing, and convert back to strings for display. A piece table gives you efficient insertions without copying (you append to a separate buffer and create a new piece descriptor), but querying a specific line requires walking the piece list and tracking line breaks. A gap buffer gives you efficient edits near the cursor but requires moving the gap for random access. All of these are worth considering for an editor that needs to handle very large files — millions of lines, or single lines that are megabytes long — but they add complexity that is not justified for our use case.

The decision to use an array of strings also means we need to decide how text enters the array. The constructor takes a single string — the entire file contents — and splits it on newline characters:

```javascript
class Doc {
  constructor(text, filename) {
    this.filename = filename || "untitled";
    this.lines = text.split("\n");
    if (this.lines.length === 0) this.lines = [""];
    // ...
  }
}
```

The `split("\n")` call produces an array where each element is the text of one line, without the trailing newline. An empty file produces an array with a single empty string — `[""]` — because a text editor should always have at least one line. You cannot have a cursor in a document with zero lines. The guard `if (this.lines.length === 0) this.lines = [""]` handles this edge case, though in practice `"".split("\n")` already returns `[""]` in JavaScript, so the guard is defensive.

We provide bounds-checked access through `getLine`:

```javascript
getLine(idx) {
  if (idx < 0 || idx >= this.lines.length) return "";
  return this.lines[idx];
}
```

If the index is out of bounds, we return an empty string rather than throwing an error. This might seem unsafe — silently returning empty data could mask bugs — but in practice it makes the rest of the code simpler. Many operations compute line indices from scroll positions, mouse coordinates, or cursor arithmetic that might temporarily be out of range. Returning an empty string for out-of-bounds indices is a reasonable default that prevents crashes without requiring every caller to bounds-check.

The line count is exposed as a getter property:

```javascript
get lineCount() { return this.lines.length; }
```

This is used throughout the editor — for computing the gutter width, the scrollbar thumb size, the maximum scroll position, and the status bar display.

One thing we do not store in the line array is the newline character itself. Each string in `this.lines` contains only the content of the line, with no trailing `\n`. When we need to produce the full file contents (for saving), we join the array with newlines: `this.lines.join("\n")`. When we insert a newline character (the user presses Enter), we split the current line into two lines and splice the new line into the array. This representation — lines without their terminators — is the natural fit for how we render and manipulate text. We draw one line at a time, we select ranges defined by line and column indices, and we navigate with arrow keys that move by lines and columns. The newline is not a visible character; it is the boundary between elements in our array.


## 3.2 — The Cursor

The cursor — the blinking vertical bar that shows where the next keystroke will take effect — is the most fundamental piece of state in a text editor, after the text itself. Every operation flows through the cursor: typing inserts at the cursor, backspace deletes behind the cursor, arrow keys move the cursor, and clicking places the cursor. The cursor's position is the answer to the question "where am I in this document?"

We represent the cursor as two integers: `cursorLine` and `cursorCol`, both zero-indexed. Line 0 is the first line. Column 0 is before the first character. If line 5 contains the text `function hello()`, then `cursorCol = 0` is before the `f`, `cursorCol = 8` is between the space and the `h`, and `cursorCol = 16` is after the `)` — at the end of the line.

The cursor position must always be valid. It must point to a line that exists in the document, and to a column that is between 0 and the length of that line (inclusive — the cursor can be at the end of a line, after the last character). The `setCursor` method is the gatekeeper that enforces this:

```javascript
setCursor(line, col, updateDesired) {
  line = Math.max(0, Math.min(line, this.lines.length - 1));
  col = Math.max(0, Math.min(col, this.getLine(line).length));
  this.cursorLine = line;
  this.cursorCol = col;
  if (updateDesired !== false) {
    this.desiredCol = col;
  }
}
```

The clamping is straightforward. The line is clamped to `[0, lineCount - 1]`. The column is clamped to `[0, lineLength]`. Every part of the editor that modifies the cursor position calls `setCursor`, which means the cursor can never end up in an invalid state — no negative columns, no lines past the end of the file, no columns past the end of a line.

The third parameter, `updateDesired`, introduces a subtle but important concept: the desired column.

Consider what happens when you press the down arrow key. If you are at column 15 of a 40-character line and you press down, you expect to land at column 15 of the next line. If the next line is only 10 characters long, the cursor moves to column 10 (the end of the line). Now press down again, and the line after that is 50 characters long. Where should the cursor go? Column 10, because that is where the cursor currently is? Or column 15, because that is where you were before the short line forced you to the left?

The answer, in every text editor that feels right, is column 15. The editor remembers your intended column — the column you were at before vertical movement started forcing you sideways — and tries to return to it whenever the line is long enough.

This is the `desiredCol`. When the cursor moves horizontally (left, right, Home, End, clicking) or when text is inserted, `desiredCol` is updated to the new column. When the cursor moves vertically (up, down, Page Up, Page Down), `desiredCol` is *not* updated — the cursor moves to `min(desiredCol, lineLength)`, and the desired column is preserved for the next vertical movement.

This is why `setCursor` has the `updateDesired` parameter. The default behavior (`updateDesired !== false`, which is true when the parameter is omitted) updates `desiredCol` to the new column. When the caller passes `false` explicitly, it means "move the cursor, but keep the old desired column." The arrow up/down handlers pass `false`:

```javascript
case "ArrowUp":
  doc.setCursor(line - 1, doc.desiredCol, false);
  break;
case "ArrowDown":
  doc.setCursor(line + 1, doc.desiredCol, false);
  break;
```

Notice that we pass `doc.desiredCol` as the column, not `doc.cursorCol`. The clamping inside `setCursor` will bring it down to the line length if necessary, but the desired column stays at the original value for subsequent vertical movements.

This is one of those details that users never consciously notice but would immediately feel if it were missing. A text editor without `desiredCol` feels clumsy, like it is fighting you on every vertical movement through lines of unequal length. Getting it right makes the editor feel like it understands your intent.


## 3.3 — Selection

A selection in a text editor is a highlighted range of text. You create one by clicking and dragging, by shift-clicking, by shift-arrowing, or by pressing Ctrl+A. The selection determines what text will be affected by copy, cut, delete, and typing operations.

We represent a selection as four integers: `selStartLine`, `selStartCol`, `selEndLine`, and `selEndCol`. The start is where the selection began (where the user first clicked or where the cursor was when shift was pressed), and the end is where the selection currently extends to (where the user dragged to or where the cursor moved to). These are conceptual anchors, not ordered positions — the "start" can be after the "end" in the document if the user dragged upward or to the left.

A boolean flag, `selectionActive`, tracks whether a selection is currently in progress. This is distinct from whether the selection has nonzero extent. When the user clicks without shift, we set `selectionActive = true` and set both start and end to the cursor position. At this point, the selection is active but empty — its start and end are the same. If the user then drags, the end moves and the selection acquires extent. If the user just clicks without dragging, the selection remains active but empty.

The `hasSelection` method checks whether the selection has nonzero extent:

```javascript
hasSelection() {
  if (!this.selectionActive) return false;
  return !(this.selStartLine === this.selEndLine
           && this.selStartCol === this.selEndCol);
}
```

Both conditions must be true: the selection must be active, and the start and end must be at different positions.

The lifecycle of a selection involves three methods. `startSelection` records the current cursor position as the selection start and sets `selectionActive = true`:

```javascript
startSelection() {
  this.selectionActive = true;
  this.selStartLine = this.cursorLine;
  this.selStartCol = this.cursorCol;
}
```

`updateSelectionEnd` copies the current cursor position to the selection end:

```javascript
updateSelectionEnd() {
  if (!this.selectionActive) return;
  this.selEndLine = this.cursorLine;
  this.selEndCol = this.cursorCol;
}
```

And `clearSelection` deactivates the selection and resets all four coordinates to the cursor:

```javascript
clearSelection() {
  this.selectionActive = false;
  this.selStartLine = this.cursorLine;
  this.selStartCol = this.cursorCol;
  this.selEndLine = this.cursorLine;
  this.selEndCol = this.cursorCol;
}
```

The typical sequence for a mouse drag selection is: `mousedown` calls `startSelection()`, then repeated `mousemove` events update the cursor position and call `updateSelectionEnd()`, and finally `mouseup` stops the drag. For a shift-arrow selection: the handler checks if selection is active, if not calls `startSelection()`, moves the cursor, and calls `updateSelectionEnd()`. For clearing: any unshifted cursor movement or click calls `clearSelection()`.

Because the user can drag upward or to the left, the selection start can be later in the document than the selection end. But many operations — extracting selected text, deleting a selection, drawing the selection highlight — need the positions in document order: the earlier position first. The `getNormalizedSelection` method provides this:

```javascript
getNormalizedSelection() {
  if (this.selStartLine < this.selEndLine ||
      (this.selStartLine === this.selEndLine
       && this.selStartCol <= this.selEndCol)) {
    return { fromLine: this.selStartLine, fromCol: this.selStartCol,
             toLine: this.selEndLine, toCol: this.selEndCol };
  }
  return { fromLine: this.selEndLine, fromCol: this.selEndCol,
           toLine: this.selStartLine, toCol: this.selStartCol };
}
```

The comparison is two-level: if the start line is before the end line, the selection is already in order. If they are on the same line, we compare columns. If the start is after the end, we swap them.

Extracting the selected text is a matter of collecting the appropriate substrings:

```javascript
getSelectedText() {
  if (!this.hasSelection()) return "";
  const sel = this.getNormalizedSelection();
  if (sel.fromLine === sel.toLine) {
    return this.getLine(sel.fromLine).substring(sel.fromCol, sel.toCol);
  }
  const parts = [];
  parts.push(this.getLine(sel.fromLine).substring(sel.fromCol));
  for (let i = sel.fromLine + 1; i < sel.toLine; i++) {
    parts.push(this.getLine(i));
  }
  parts.push(this.getLine(sel.toLine).substring(0, sel.toCol));
  return parts.join("\n");
}
```

For a single-line selection, it is just a substring. For a multi-line selection, we take the tail of the first line (from `fromCol` to the end), all the complete lines in between, and the head of the last line (from the start to `toCol`), and join them with newlines. This reconstructs exactly the text that the user sees highlighted.

This method is used for the copy and cut operations. It is also used by `deleteSelection`, which extracts the text (for the undo record), deletes the range, moves the cursor to the start of the deleted range, and clears the selection. We will examine the deletion mechanics in Chapter 4.

One design decision worth noting is that we use a single selection, not multiple selections. Some modern editors — VS Code, Sublime Text — support multiple cursors and multiple selections. That is a powerful feature, but it adds significant complexity to every operation that touches the cursor or selection. With a single selection, the code is straightforward: there is one start, one end, one normalization. Adding multiple selections would mean managing an array of selection ranges, sorting them, merging overlapping ones, and ensuring that every edit operation correctly updates all of them. For our editor, a single selection is sufficient and keeps the code simple.

The selection also has important implications for how we draw the text area. When a selection exists, we need to render a highlight rectangle behind the selected text. For a single-line selection, this is one rectangle. For a multi-line selection, we need one rectangle per line: the tail of the first selected line, the full width of each middle line, and the head of the last selected line. The drawing code iterates over the visible selected lines and computes the rectangle for each one based on the normalized selection endpoints and the character width. We will build this rendering in Chapter 8, but the key takeaway is that the selection data — `fromLine`, `fromCol`, `toLine`, `toCol` — directly drives the geometry of the highlight rectangles. The normalized form makes this straightforward: we always iterate from `fromLine` to `toLine`, and the column values tell us where each rectangle starts and ends.

The relationship between the selection and the cursor is also worth clarifying. The cursor is always at one end of the selection — specifically, it is always at the selection *end* (not necessarily the end in document order, but the end that the user is actively moving). When the user drags downward, the cursor tracks the mouse and the selection end moves with it, while the selection start stays at the initial click position. When the user drags upward, the cursor moves above the start, and the selection end is before the start in document order. The cursor is the active end; the start is the anchor. This means that when we draw the cursor, it always appears at the edge of the selection that the user is controlling, which provides a clear visual indicator of which direction the selection will grow or shrink.


## 3.4 — Coordinate Systems

A text editor juggles three different coordinate systems, and converting between them correctly is essential for everything from placing the cursor on a mouse click to drawing the selection highlight to scrolling the viewport.

**Document coordinates** are `(line, col)` pairs. Line 0 is the first line of the file. Column 0 is before the first character. This is the coordinate system used by the `Doc` class for the cursor, the selection, and all edit operations. Document coordinates are independent of how the text is displayed — they do not change when the user scrolls, resizes the window, or changes the font size.

**Canvas coordinates** are `(x, y)` pixel positions on the canvas's backing store. The origin `(0, 0)` is the top-left corner of the canvas. These coordinates are in device pixels — on a 2x Retina display, a position at CSS pixel (100, 50) is canvas pixel (200, 100). The canvas coordinate system is what the drawing code works with.

**Screen coordinates** are `(clientX, clientY)` pixel positions relative to the browser viewport. These are what mouse events provide. They are in CSS pixels, not device pixels.

Converting from screen to canvas coordinates is a multiplication by the device pixel ratio:

```javascript
_toCanvas(e) {
  return { x: e.clientX * this.dpr, y: e.clientY * this.dpr };
}
```

Converting from canvas coordinates to document coordinates requires accounting for the layout geometry and the scroll position:

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

Let us trace through the line calculation. The canvas y-coordinate `cy` is the mouse position in device pixels. We subtract `this.textAreaY` (the top of the text area, below the menu bar) to get the y-position relative to the text area. We add `this.scrollY` (the vertical scroll offset, in device pixels) to account for lines that have scrolled off the top. We divide by `this.lineH` (the height of one line in device pixels) to get the line index. We use `Math.floor` because we want the line that the click is *inside* — clicking anywhere within a line's vertical extent should select that line.

The column calculation is similar. We subtract `this.gutterW` (the width of the gutter) and `Config.textPaddingLeft * this.dpr` (the padding between the gutter and the text) to get the x-position relative to the start of the text. We add `this.scrollX` (the horizontal scroll offset) to account for text that has scrolled to the left. We divide by `this.charW` (the width of one character in device pixels) to get the column index. We use `Math.round` rather than `Math.floor` here, because we want the cursor to snap to the nearest column boundary — clicking on the right half of a character should place the cursor after it, not before it.

Both results are clamped: the line to `[0, lineCount - 1]` and the column to a minimum of 0. The column is clamped to the actual line length later, at the call site, because `_canvasToTextPos` does not know which line was selected until the line is computed.

The reverse conversion — from document coordinates to canvas coordinates — is used when drawing the cursor, the selection, and other position-dependent elements:

```javascript
// Cursor drawing (in _drawTextArea):
const cx = textX + this.doc.cursorCol * this.charW - this.scrollX;
const cy = this.menuBarH + (this.doc.cursorLine * this.lineH - this.scrollY);
```

The x-position is the text area's left edge plus the cursor column times the character width minus the horizontal scroll. The y-position is the menu bar height plus the cursor line times the line height minus the vertical scroll. This is the inverse of the conversion we did above.

The key insight is that all of these conversions are simple arithmetic — multiplications, additions, and subtractions — because of two design decisions we made early on. First, we use a monospace font, so every character has the same width. Second, every line has the same height. These two invariants make document coordinates and canvas coordinates linearly related, which means conversion is O(1) in both directions. In an editor with variable-width fonts and word wrapping, these conversions would be far more complex — you would need to measure text widths, track wrapped line positions, and potentially search through a layout cache to find the line that contains a given y-coordinate.

There is one more coordinate-related concept that matters: the scroll position. The `scrollX` and `scrollY` properties on the editor represent how far the viewport has scrolled from the origin of the document. They are in device pixels. When `scrollY` is 0, the first line of the document is at the top of the text area. When `scrollY` is `5 * this.lineH`, the first five lines have scrolled off the top and the sixth line is at the top.

The scroll position creates the mapping between document space and screen space. A document line is visible if its y-position in canvas coordinates — `this.menuBarH + (lineIndex * this.lineH - this.scrollY)` — falls within the text area. The editor computes the first visible line as `Math.floor(this.scrollY / this.lineH)` and the last visible line as `startLine + Math.ceil(this.textAreaH / this.lineH) + 1`. Only these lines need to be drawn, so the rendering cost is proportional to the number of visible lines, not the total number of lines in the document.

The `+1` at the end of the visible line computation is a safety margin. A line that is only partially visible at the bottom of the viewport should still be drawn — the user can see part of it, so we must render it. The extra line ensures we always render any partially-visible line.

A related concept is *ensuring cursor visibility*. After every cursor movement and every edit operation, the editor checks whether the cursor is within the visible viewport. If the cursor has moved above the viewport, `scrollY` is adjusted downward to bring it on screen. If the cursor has moved below, `scrollY` is adjusted upward. The same logic applies horizontally for `scrollX`, though horizontal scrolling is less common because most code lines fit within the viewport width. This auto-scrolling behavior is implemented in `_ensureCursorVisible`, which we will examine in Chapter 5. The important point for now is that the scroll position is not directly controlled by the user — it is a derived value that the editor adjusts to keep the cursor visible, with the mouse wheel and scrollbar providing additional manual control.

All three coordinate systems — document, canvas, and screen — are connected through a chain of transformations. A mouse click arrives in screen coordinates, is scaled to canvas coordinates by the DPR, is translated to document coordinates by subtracting the layout offsets and dividing by character dimensions, and is clamped to valid document bounds. A cursor position starts as document coordinates, is multiplied by character dimensions, offset by the layout geometry, adjusted by the scroll position, and the result is the canvas pixel where the cursor bar is drawn. Understanding this chain is essential for debugging rendering issues — if the cursor appears in the wrong place, the bug is somewhere in this chain of arithmetic.


## 3.5 — Word Boundaries

Many editor operations work at the word level rather than the character level. Ctrl+Left moves the cursor to the beginning of the previous word. Ctrl+Right moves to the end of the next word. Ctrl+Backspace deletes the previous word. Double-click selects a word. All of these need to know where word boundaries are.

What constitutes a "word" is a design decision. We define a word character as anything matching the regex `[a-zA-Z0-9_$]` — letters, digits, underscores, and dollar signs. This covers identifiers in virtually every programming language. Everything else — spaces, operators, punctuation, brackets — is a non-word character. A word boundary is a position where the character type changes: where a word character is adjacent to a non-word character, or vice versa.

The `wordBoundaryLeft` method finds the boundary to the left of a given position:

```javascript
wordBoundaryLeft(line, col) {
  const text = this.getLine(line);
  if (col <= 0) {
    if (line > 0)
      return { line: line - 1, col: this.getLine(line - 1).length };
    return { line: 0, col: 0 };
  }
  let i = col - 1;
  const isWord = (c) => /[a-zA-Z0-9_$]/.test(c);
  while (i > 0 && text[i] === ' ') i--;
  const startType = isWord(text[i]);
  while (i > 0 && isWord(text[i - 1]) === startType) i--;
  return { line, col: i };
}
```

The algorithm works in three phases. First, if the cursor is at the beginning of the line, we wrap to the end of the previous line — this is the expected behavior when pressing Ctrl+Left at column 0. Second, we skip over any whitespace to the left of the cursor, because whitespace between words should not count as a word. Third, we determine the character type (word or non-word) at the current position and scan left as long as the character type matches. The result is the position where the character type changes — the beginning of the current word or symbol sequence.

Consider the text `  hello_world  +  foo`. If the cursor is at column 22 (after `foo`), `wordBoundaryLeft` skips the two spaces before `foo`, finds that `o` is a word character, scans left through `foo`, and returns column 18 (the `f`). From column 18, the next call skips the two spaces, finds `+` is a non-word character, scans left through `+` (just one character), and returns column 15 (the `+`). From column 15, the next call skips the two spaces, finds `d` is a word character, scans left through `hello_world`, and returns column 2 (the `h`).

The `wordBoundaryRight` method is the mirror image:

```javascript
wordBoundaryRight(line, col) {
  const text = this.getLine(line);
  if (col >= text.length) {
    if (line < this.lines.length - 1)
      return { line: line + 1, col: 0 };
    return { line, col: text.length };
  }
  let i = col;
  const isWord = (c) => /[a-zA-Z0-9_$]/.test(c);
  const startType = isWord(text[i]);
  while (i < text.length && isWord(text[i]) === startType) i++;
  while (i < text.length && text[i] === ' ') i++;
  return { line, col: i };
}
```

Here the whitespace skipping happens *after* the word scan rather than before. The idea is that when moving right, you jump to the end of the current word and past any trailing whitespace, landing at the start of the next word. This matches the behavior of Ctrl+Right in most editors: the cursor moves to the beginning of the next word, not the end of the current one.

The line-wrapping behavior is important. When `wordBoundaryLeft` is called at column 0, it returns the end of the previous line. When `wordBoundaryRight` is called at the end of a line, it returns the beginning of the next line. This means Ctrl+Left and Ctrl+Right can cross line boundaries, which is essential for fluid navigation — without it, the user would have to press Left to move to the previous line and then Ctrl+Left to start word-jumping again.

There is a subtlety in the two-phase scan that is easy to miss. After skipping whitespace, `wordBoundaryLeft` determines the character type at position `i` and scans left while the type matches. This means the scan treats a run of operators (like `===` or `->`) as a single "word" of non-word characters. Ctrl+Left on `x === y` jumps over `===` in one step, just as it jumps over `hello` in one step. This is consistent with how most editors behave — punctuation sequences are treated as units, not individual characters.

The word boundary functions are pure — they do not modify any state, they just compute a position. They are called by the navigation handlers (Ctrl+Left, Ctrl+Right), the deletion handlers (Ctrl+Backspace, Ctrl+Delete), and the double-click handler (which selects from the left boundary to the right boundary of the word under the click). We will see these callers in Chapters 5 and 6.


## 3.6 — File Identity and Syntax Detection

The `Doc` class carries two pieces of metadata about the file it represents: the filename and the dirty flag.

The `filename` is the name of the file that was opened, or `"untitled"` for a new document that has not been saved. It is displayed in the status bar and used as the default name when the user saves the file. It is also used for syntax detection — the most practically important thing the filename does.

```javascript
this.filename = filename || "untitled";
this.syntax = detectSyntax(this.filename);
this.tokenizer = new Tokenizer();
this.tokenizer.setSyntax(this.syntax);
```

The `detectSyntax` function matches the filename's extension against the extensions registered by each syntax definition:

```javascript
function detectSyntax(filename) {
  if (!filename) return Syntaxes[Syntaxes.length - 1];
  const lower = filename.toLowerCase();
  for (let i = 0; i < Syntaxes.length; i++) {
    for (let e = 0; e < Syntaxes[i].extensions.length; e++) {
      if (lower.endsWith(Syntaxes[i].extensions[e])) return Syntaxes[i];
    }
  }
  return Syntaxes[Syntaxes.length - 1];
}
```

The function iterates through the syntax definitions in order, checking each one's extensions array. If the filename ends with `.js`, `.jsx`, `.mjs`, `.cjs`, `.ts`, or `.tsx`, the JavaScript syntax is selected. If no extension matches, the last syntax in the array — "Plain," which has no patterns — is used as a fallback. The case-insensitive comparison (`toLowerCase()`) ensures that `README.MD` and `readme.md` both match the Markdown syntax.

When the user opens a file by dragging it onto the editor or using File > Open, a new `Doc` is created with the file's name, and the syntax is detected automatically. The user can also override the syntax manually through the View menu, which calls `_setSyntax` on the editor:

```javascript
_setSyntax(syntaxIdx) {
  if (syntaxIdx < 0 || syntaxIdx >= Syntaxes.length) return;
  this.doc.syntax = Syntaxes[syntaxIdx];
  this.doc.tokenizer.setSyntax(this.doc.syntax);
  this.doc.hlCache = [];
  this.doc._highlightAll();
  this.needsRedraw = true;
}
```

Changing the syntax clears the entire highlight cache and re-tokenizes the document from scratch. This is necessary because different syntaxes have different pattern sets, so every line's tokens may change.

The `dirty` flag tracks whether the document has been modified since it was last saved (or since it was opened). It is set to `true` by every edit operation and cleared when the user saves. The status bar displays `[modified]` next to the filename when `dirty` is true, giving the user a visual indicator of unsaved changes.

The dirty flag is a simple boolean, not a counter or a hash. This means it does not track *which* changes were saved — if you make a change, save, then undo, the flag becomes `true` again even though the document is back to its saved state. A more sophisticated implementation could track the undo stack depth at the point of the last save and compare, but the simple boolean is sufficient for our purposes and matches the behavior of most lightweight editors.


## 3.7 — The Highlight Cache

The `Doc` class owns the highlight cache — an array of tokenization results, one per line — even though the tokenization logic lives in the `Tokenizer` class and the syntax definitions live in the `SyntaxDef` objects. We will examine the tokenizer in detail in Chapter 7. For now, what matters is the cache's interface and how the `Doc` keeps it synchronized with the line array.

The cache is an array:

```javascript
this.hlCache = [];
```

Each element has the form `{ tokens, state }`, where `tokens` is an array of `{ text, type }` objects representing the tokenized content of that line, and `state` is the tokenizer's state at the end of the line (which multi-line range, if any, is active). The state is what connects one line's highlighting to the next — if a line ends inside a block comment, the next line must know to continue the comment.

The cache is populated in two ways. At document load time, `_highlightAll` tokenizes every line sequentially:

```javascript
_highlightAll() {
  this.hlCache = [];
  let state = null;
  for (let i = 0; i < this.lines.length; i++) {
    const result = this.tokenizer.tokenize(this.lines[i], state);
    this.hlCache[i] = result;
    state = result.state;
  }
  this.hlDirtyFrom = this.lines.length;
}
```

Each line is tokenized using the state from the previous line. The first line uses `null` as its initial state, meaning "not inside any multi-line construct."

After an edit, `_rehighlightFrom` re-tokenizes starting from the changed line:

```javascript
_rehighlightFrom(line) {
  let state = null;
  if (line > 0 && this.hlCache[line - 1]) {
    state = this.hlCache[line - 1].state;
  }
  for (let i = line; i < this.lines.length; i++) {
    const result = this.tokenizer.tokenize(this.lines[i], state);
    const old = this.hlCache[i];
    this.hlCache[i] = result;
    state = result.state;
    if (old && i > line
        && JSON.stringify(old.state) === JSON.stringify(result.state)) {
      break;
    }
  }
}
```

This method starts from the state at the end of the line before the edit and re-tokenizes forward. After each line, it compares the new state with the old cached state. If they match — meaning the edit did not change how the tokenizer transitions between lines — it stops. This is the incremental optimization: most edits only affect the highlighting of a few lines around the change. If you type a character in the middle of a line, only that line's tokens change; the state at the end of the line is the same, so no other lines need to be re-tokenized. If you delete the closing `*/` of a block comment, the state changes (from "outside comment" to "inside comment"), and re-tokenization continues until it finds a new `*/` or reaches the end of the file.

The `JSON.stringify` comparison is a pragmatic choice. The state objects are small — they just contain `{ rangeIdx: null }` or `{ rangeIdx: 3 }` — so serializing and comparing them is fast. A more efficient approach would be to implement a custom comparison, but for objects this small, `JSON.stringify` is simple and correct.

When lines are inserted or deleted, the edit methods splice the highlight cache in parallel with the line array:

```javascript
// In _rawInsert, when inserting new lines:
this.hlCache.splice(line + 1, 0,
  ...new Array(newLines.length).fill(null));

// In _rawDelete, when removing lines:
this.hlCache.splice(fromLine + 1, removeCount);
```

The `null` entries for newly inserted lines signal that they have not been tokenized yet. When the renderer requests tokens for a line via `getTokensForLine`, it checks for a missing cache entry and triggers re-tokenization on demand:

```javascript
getTokensForLine(lineIdx) {
  if (lineIdx < 0 || lineIdx >= this.lines.length)
    return [{ text: "", type: "normal" }];
  if (!this.hlCache[lineIdx]) {
    this._rehighlightFrom(lineIdx);
  }
  return this.hlCache[lineIdx].tokens;
}
```

This lazy approach means we never tokenize lines that are not visible. If you insert a thousand lines in the middle of a document, only the lines that are on screen get tokenized immediately. The rest are tokenized if and when the user scrolls to them. In *lite*, this kind of deferred work is managed through a cooperative threading system — background coroutines tokenize lines incrementally, yielding control between chunks to keep the UI responsive. Our approach is simpler but effective: we tokenize on demand, and the per-line tokenization is fast enough that it never causes a perceptible delay.


## 3.8 — What We Have, and What Comes Next

The `Doc` class is the heart of the editor. It holds the text as an array of strings, the cursor as a `(line, col)` pair with a desired column for vertical movement, and the selection as a pair of endpoints with normalization. It knows its filename, tracks whether it has been modified, and owns the syntax highlighting cache. It provides methods for querying text (`getLine`, `getSelectedText`), for computing positions (`wordBoundaryLeft`, `wordBoundaryRight`), and for managing cursor and selection state (`setCursor`, `startSelection`, `updateSelectionEnd`, `clearSelection`, `getNormalizedSelection`, `hasSelection`).

What the `Doc` class does not yet have — and what we will build in the next chapter — is the ability to modify its own text. The edit operations (`insertText`, `deleteRange`) and the undo/redo system (`undo`, `redo`) are the subject of Chapter 4. These methods are closely tied to the data structures we have built here — they modify the `lines` array, update the highlight cache, and record actions on the undo stack. But they are complex enough to deserve their own chapter, because the undo system introduces its own design challenges: how to record actions so they can be reversed, how to merge rapid keystrokes into single undoable units, and how to handle the interplay between the redo stack and new edits.

The `Doc` class as we have built it so far is a passive data structure — it holds state and answers questions about that state. In Chapter 4, we will give it the ability to act: to change its own text, to remember what it changed, and to take it back.

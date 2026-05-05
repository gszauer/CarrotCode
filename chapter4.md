# Chapter 4: Editing Operations and Undo/Redo

*Inserting text, deleting ranges, and building a full undo/redo system with intelligent action merging.*

---

## 4.1 — The Two Primitive Operations

Every change to a text document, no matter how complex it appears to the user, can be decomposed into two primitive operations: inserting text at a position and deleting text from a range. Typing a character is an insertion. Pressing Backspace is a deletion. Pasting a paragraph is an insertion. Cutting a selection is a deletion. Even replacing text — selecting a word and typing over it — is a deletion followed by an insertion. These two operations are the atoms of text editing. Everything else is built on top of them.

In our `Doc` class, these primitives are implemented as `_rawInsert` and `_rawDelete`. The underscore prefix and the word "raw" signal that these methods operate directly on the line array without recording anything to the undo stack. This distinction is important, and we will see why in a moment.

Let us start with `_rawInsert`. It takes a line index, a column index, and a string of text to insert at that position:

```javascript
_rawInsert(line, col, text) {
  const lineText = this.getLine(line);
  const before = lineText.substring(0, col);
  const after = lineText.substring(col);
  const inserted = text.split("\n");

  if (inserted.length === 1) {
    this.lines[line] = before + inserted[0] + after;
    this._rehighlightFrom(line);
  } else {
    this.lines[line] = before + inserted[0];
    const newLines = [];
    for (let i = 1; i < inserted.length - 1; i++) {
      newLines.push(inserted[i]);
    }
    newLines.push(inserted[inserted.length - 1] + after);
    this.lines.splice(line + 1, 0, ...newLines);
    this.hlCache.splice(line + 1, 0,
      ...new Array(newLines.length).fill(null));
    this._rehighlightFrom(line);
  }
  this.dirty = true;
}
```

The method handles two cases: inserting text that does not contain newlines (the common case when typing a character) and inserting text that does contain newlines (pasting multiline text or pressing Enter).

For the single-line case, the existing line is split at the insertion column into `before` and `after`. The new text is sandwiched between them: `before + inserted[0] + after`. The line is replaced in the array, and the highlight cache is updated.

For the multi-line case, the logic is more involved. The first line of the insertion is appended to the `before` fragment — this becomes the new content of the existing line. The last line of the insertion is prepended to the `after` fragment — this becomes a new line at the end. Any lines in between are inserted as-is. The new lines are spliced into the line array at `line + 1`, and the corresponding entries are added to the highlight cache (as `null`, indicating they need tokenization).

Let us trace through a concrete example. Suppose line 3 contains `"hello world"` and we insert `"brave\nnew "` at column 6 (between the space and `w`). The `before` is `"hello "` and the `after` is `"world"`. The inserted text splits into `["brave", "new "]`. The first line becomes `"hello brave"`. The last fragment becomes `"new world"`. The result is that line 3 is now `"hello brave"` and a new line 4 is `"new world"`. The original line 4 and beyond shift down by one.

Now `_rawDelete`. It takes a range defined by start and end positions:

```javascript
_rawDelete(fromLine, fromCol, toLine, toCol) {
  if (fromLine === toLine) {
    const l = this.getLine(fromLine);
    this.lines[fromLine] = l.substring(0, fromCol) + l.substring(toCol);
    this._rehighlightFrom(fromLine);
  } else {
    const first = this.getLine(fromLine).substring(0, fromCol);
    const last = this.getLine(toLine).substring(toCol);
    this.lines[fromLine] = first + last;
    const removeCount = toLine - fromLine;
    this.lines.splice(fromLine + 1, removeCount);
    this.hlCache.splice(fromLine + 1, removeCount);
    this._rehighlightFrom(fromLine);
  }
  this.dirty = true;
}
```

Again, two cases. For a single-line deletion, we remove the characters between `fromCol` and `toCol` by concatenating the part before the range with the part after it. For a multi-line deletion, we keep the part before the range on the first line, keep the part after the range on the last line, join them into one line, and remove all the lines in between using `splice`. The highlight cache is spliced in parallel.

These two methods are inverses of each other. If you insert the text `"brave\nnew "` at line 3, column 6, and then delete from line 3, column 6, to line 4, column 4, you get back the original document. This invertibility is what makes undo possible — to undo an insertion, we delete the same text from the same position; to undo a deletion, we insert the deleted text back.

Both methods call `this._rehighlightFrom(line)` after modifying the line array. As we discussed in Chapter 3, this re-tokenizes from the changed line forward, stopping when the tokenizer state matches the cached state. This keeps the syntax highlighting in sync with the text without re-tokenizing the entire document.

Both methods also set `this.dirty = true`, marking the document as modified. This is the flag that causes `[modified]` to appear in the status bar.

It is worth pausing to appreciate how these two methods handle the complexity of multi-line edits. Consider deleting a selection that spans three lines — from line 5, column 10, to line 7, column 3. Before the deletion, the document has lines like:

```
line 5: "The quick brown fox jumps"
line 6: "over the lazy"
line 7: "dog and cat"
```

If we delete from (5, 10) to (7, 3), the `before` fragment is `"The quick "` (line 5 up to column 10), and the `after` fragment is `" and cat"` (line 7 from column 3 onward). The resulting line is `"The quick  and cat"`. Lines 6 and 7 are removed from the array entirely using `splice`. The highlight cache entries for those lines are also removed, and `_rehighlightFrom` is called on line 5 to update the syntax highlighting for the modified line and any lines whose tokenizer state might have changed.

The `splice` calls on both the line array and the highlight cache are paired throughout the code. Every time `this.lines.splice(...)` is called, a corresponding `this.hlCache.splice(...)` follows immediately. This pairing is essential — if the two arrays fall out of sync, the wrong highlighting would be shown for the wrong lines, producing garbled colors that would be very difficult to debug. By keeping the splices together, we ensure the arrays always have the same length and the same correspondence: `hlCache[i]` always holds the tokenization result for `lines[i]`.

There is an important asymmetry between insert and delete in how they relate to the undo system, which we will see in the next section. When we insert, we know the text being inserted (it is a parameter). When we delete, we need to extract the text being deleted *before* we delete it, because once it is gone from the line array, we cannot recover it. This is why `deleteRange` calls `_getTextRange` before calling `_rawDelete` — it reads the text that is about to be destroyed and stores it in the action record for later restoration.


## 4.2 — The Undo Stack

The raw operations modify the text, but they do not remember what they did. If we called `_rawInsert` directly and the user pressed Ctrl+Z, nothing would happen — there would be no record of the change to reverse. The undo system provides that record.

Our undo system is based on action records. Each action record describes one edit operation — what type it was (insert or delete), where it happened, what text was involved, and where the cursor was before the edit:

```javascript
{
  type: "insert",       // or "delete"
  line: 3,              // line where the edit started
  col: 6,               // column where the edit started
  text: "hello",        // the text that was inserted or deleted
  cursorLine: 3,        // cursor line BEFORE the edit
  cursorCol: 6          // cursor column BEFORE the edit
}
```

The cursor position stored in the action record is the cursor's position *before* the edit, not after it. This is a deliberate choice. When we undo, we want to restore the cursor to where it was before the user performed the action. If the user typed "hello" at position (3, 6) and the cursor moved to (3, 11), undoing should move the cursor back to (3, 6). Storing the pre-edit cursor position makes this straightforward.

The `insertText` and `deleteRange` methods are the public interface for editing. They wrap the raw operations with undo recording:

```javascript
insertText(line, col, text) {
  this._pushUndo({
    type: "insert", line, col, text,
    cursorLine: this.cursorLine, cursorCol: this.cursorCol
  });
  this._rawInsert(line, col, text);
}

deleteRange(fromLine, fromCol, toLine, toCol) {
  const text = this._getTextRange(fromLine, fromCol, toLine, toCol);
  this._pushUndo({
    type: "delete", line: fromLine, col: fromCol, text,
    cursorLine: this.cursorLine, cursorCol: this.cursorCol
  });
  this._rawDelete(fromLine, fromCol, toLine, toCol);
  return text;
}
```

For insertions, the action record stores the text being inserted. For deletions, we first extract the text that is about to be deleted using `_getTextRange`, then store it in the action record, then perform the deletion. The extracted text is what we will need to re-insert if the user undoes the deletion.

The `_getTextRange` method reconstructs the text within a range, using the same logic as `getSelectedText` from Chapter 3:

```javascript
_getTextRange(fromLine, fromCol, toLine, toCol) {
  if (fromLine === toLine)
    return this.getLine(fromLine).substring(fromCol, toCol);
  const parts = [];
  parts.push(this.getLine(fromLine).substring(fromCol));
  for (let i = fromLine + 1; i < toLine; i++)
    parts.push(this.getLine(i));
  parts.push(this.getLine(toLine).substring(0, toCol));
  return parts.join("\n");
}
```

For a single-line range, it returns a substring. For a multi-line range, it collects the tail of the first line, all middle lines, and the head of the last line, joined with newlines.

The undo stack itself is a simple array: `this.undoStack = []`. New actions are pushed onto the end. When the user presses Ctrl+Z, the most recent action is popped from the end. This gives us last-in, first-out behavior — the most recent edit is undone first.

There is also a redo stack: `this.redoStack = []`. When we undo an action, we move it from the undo stack to the redo stack. When the user presses Ctrl+Shift+Z (or Ctrl+Y), we pop from the redo stack and re-apply the action. The critical rule is that the redo stack is *cleared* whenever a new edit is performed:

```javascript
this.undoStack.push(action);
this.lastUndoTime = now;
this.redoStack = [];
this.dirty = true;
```

This matches the behavior of every standard text editor. If you type "hello", undo, and then type "world", you cannot redo the "hello" — the redo history was discarded when you typed "world". The redo stack is a strictly linear branch. The moment you diverge from the undone history by making a new edit, the old future is gone.


## 4.3 — Undo and Redo Execution

The `undo` method pops the most recent action from the undo stack and reverses it:

```javascript
undo() {
  if (this.undoStack.length === 0) return;
  const action = this.undoStack.pop();
  if (action.type === "insert") {
    const lines = action.text.split("\n");
    const endLine = action.line + lines.length - 1;
    const endCol = lines.length === 1
      ? action.col + action.text.length
      : lines[lines.length - 1].length;
    this._rawDelete(action.line, action.col, endLine, endCol);
  } else {
    this._rawInsert(action.line, action.col, action.text);
  }
  this.setCursor(action.cursorLine, action.cursorCol);
  this.redoStack.push(action);
  this.clearSelection();
}
```

The logic is symmetric. To undo an insertion, we need to delete the text that was inserted. The action record tells us where the insertion started (`action.line`, `action.col`) and what was inserted (`action.text`). We compute where the insertion ended by counting the lines and characters in the inserted text: if the text contains no newlines, the end is on the same line at `action.col + action.text.length`. If it contains newlines, we split the text and compute the end line and column from the last fragment. Then we call `_rawDelete` to remove the inserted text.

To undo a deletion, we re-insert the deleted text at the position where it was deleted. The action record has both: `action.line` and `action.col` are the start of the range, and `action.text` is the text that was removed. We call `_rawInsert` to put it back.

After the reversal, we restore the cursor to the position stored in the action record — this is the position *before* the original edit was made. We push the action onto the redo stack so it can be re-applied later. And we clear any active selection, because the undo may have changed the text in a way that makes the current selection invalid.

The critical detail is that we call `_rawInsert` and `_rawDelete`, not `insertText` and `deleteRange`. The raw methods do not push to the undo stack. If we used the non-raw methods, undoing would create a new undo entry, and the user would end up in an infinite loop of undoing undos. The raw methods modify the text silently — they are the private implementation that the undo system uses to manipulate the document without creating new records.

The `redo` method is the mirror image:

```javascript
redo() {
  if (this.redoStack.length === 0) return;
  const action = this.redoStack.pop();
  if (action.type === "insert") {
    this._rawInsert(action.line, action.col, action.text);
    const lines = action.text.split("\n");
    const endLine = action.line + lines.length - 1;
    const endCol = lines.length === 1
      ? action.col + action.text.length
      : lines[lines.length - 1].length;
    this.setCursor(endLine, endCol);
  } else {
    const lines = action.text.split("\n");
    const endLine = action.line + lines.length - 1;
    const endCol = lines.length === 1
      ? action.col + action.text.length
      : lines[lines.length - 1].length;
    this._rawDelete(action.line, action.col, endLine, endCol);
    this.setCursor(action.line, action.col);
  }
  this.undoStack.push(action);
  this.clearSelection();
}
```

To redo an insertion, we re-insert the text and move the cursor to the end of the insertion — where it would have been after the original edit. To redo a deletion, we re-delete the range and move the cursor to the start of the range. The action is moved back from the redo stack to the undo stack.

Notice the asymmetry in cursor handling between undo and redo. When undoing, the cursor goes to `action.cursorLine` and `action.cursorCol` — the position *before* the edit. When redoing, the cursor goes to the position *after* the edit, which we compute from the action's position and text. This is because redo is replaying the original edit, and the user expects the cursor to end up where it would have been had they just performed the edit.

Together, `undo` and `redo` form a complete, correct undo system. You can undo any number of edits back to the beginning of the editing session, then redo any number of them forward. The text, cursor, and selection are restored correctly at each step. And the entire system is built on the two raw primitives — `_rawInsert` and `_rawDelete` — which we established in the previous section.

The end-position computation that appears in both `undo` and `redo` deserves a closer look, because it is the most fiddly part of the undo system. Given an action that describes an insert at position `(line, col)` with text `text`, we need to find the position after the last character of the inserted text. If the text is `"hello"` (no newlines), the end position is `(line, col + 5)`. If the text is `"hello\nworld"` (one newline), the end position is `(line + 1, 5)` — one line down, and five characters into the second fragment. If the text is `"a\nb\nc"` (two newlines), the end position is `(line + 2, 1)`.

The general formula is:

```javascript
const lines = action.text.split("\n");
const endLine = action.line + lines.length - 1;
const endCol = lines.length === 1
  ? action.col + action.text.length
  : lines[lines.length - 1].length;
```

For a single-line text, the end column is the start column plus the text length. For a multi-line text, the end line is the start line plus the number of newlines, and the end column is the length of the last fragment (which starts at column 0 on its line). This computation appears in four places — undo of an insert, redo of an insert, undo of a delete (computing the range to delete the re-inserted text), and redo of a delete. It could be factored into a helper function, but we keep it inline for clarity — each occurrence is three lines, and the context makes the intent obvious.

An alternative design would be to store the end position in the action record alongside the start position, avoiding the recomputation. This would be slightly more efficient but would add two more fields to every action record and require keeping them in sync with the text. Since the computation is fast (one `split` and a few comparisons), we recompute it as needed.


## 4.4 — Action Merging

If every keystroke created a separate undo entry, typing the word "hello" would require pressing Ctrl+Z five times to undo it. This would be maddening. Users expect that typing a continuous stream of characters creates a single undoable action — pressing Ctrl+Z should undo the whole word, or at least a substantial chunk of it, in one step.

This is action merging, and it is implemented in the `_pushUndo` method. Instead of blindly pushing every action onto the undo stack, `_pushUndo` checks whether the new action can be merged with the most recent one on the stack:

```javascript
_pushUndo(action) {
  const now = Date.now();
  const last = this.undoStack.length > 0
    ? this.undoStack[this.undoStack.length - 1]
    : null;

  if (last && action.type === "insert" && last.type === "insert"
      && (now - this.lastUndoTime) < Config.undoMergeTimeout
      && action.text.length === 1 && !/\n/.test(action.text)
      && last.line === action.line
      && last.col + last.text.length === action.col) {
    last.text += action.text;
    this.lastUndoTime = now;
    return;
  }

  // ... delete merging ...

  this.undoStack.push(action);
  this.lastUndoTime = now;
  this.redoStack = [];
  this.dirty = true;
}
```

The insert merging condition has five requirements, all of which must be true for the new action to be merged with the last one:

1. **Both actions are inserts.** You cannot merge an insert with a delete.
2. **The time since the last action is less than `Config.undoMergeTimeout` (300ms).** If the user pauses for more than 300 milliseconds between keystrokes, the next keystroke starts a new undo entry. This is the boundary between "continuous typing" and "a new edit."
3. **The new insertion is a single character and is not a newline.** Multi-character insertions (like pastes) and newlines always start new undo entries.
4. **Both insertions are on the same line.** If the cursor moved to a different line between the two keystrokes (which should not happen during continuous typing, but could happen if the user uses a shortcut), the entries are not merged.
5. **The new insertion is immediately adjacent to the end of the last insertion.** That is, the last insertion placed text at columns `[col, col + text.length)`, and the new insertion is at column `col + text.length`. This ensures we are appending to a continuous sequence.

When all five conditions are met, the merge is simple: we append the new character to the last action's `text` field. The action record grows from, say, `{ text: "hell" }` to `{ text: "hello" }`. When this merged action is later undone, the entire string "hello" is deleted in one step.

Delete merging is more complex because deletions can go in two directions — backward (Backspace) or forward (Delete key):

```javascript
if (last && action.type === "delete" && last.type === "delete"
    && (now - this.lastUndoTime) < Config.undoMergeTimeout
    && !action.text.includes("\n") && !last.text.includes("\n")
    && action.line === last.line) {
  if (action.col + action.text.length === last.col) {
    last.text = action.text + last.text;
    last.col = action.col;
    this.lastUndoTime = now;
    return;
  }
  if (action.col === last.col) {
    last.text = last.text + action.text;
    this.lastUndoTime = now;
    return;
  }
}
```

The first condition — `action.col + action.text.length === last.col` — handles Backspace merging. When you press Backspace repeatedly, each deletion removes one character to the left of the cursor. The new deletion's column plus its text length equals the old deletion's column, because the cursor moved left. We prepend the new character to the last action's text and update the column to reflect the leftward shift.

The second condition — `action.col === last.col` — handles Delete key merging. When you press Delete repeatedly, each deletion removes one character to the right of the cursor. The cursor does not move, so the column stays the same. We append the new character to the last action's text.

Both delete merging paths require that neither the old nor the new action crosses a line boundary (no newlines in either text) and that both are on the same line. This prevents merging a character delete with a line-join operation, which would be confusing to undo.

If none of the merging conditions are met, the action is pushed as a new entry on the stack, the redo stack is cleared, and `lastUndoTime` is updated.

Let us trace through a complete example. The user types "hello world" at position (0, 0), pauses for two seconds, then types "!".

- Type 'h': New action `{ type: "insert", col: 0, text: "h" }` pushed.
- Type 'e': Merge check passes (insert, <300ms, single char, same line, adjacent). Action becomes `{ text: "he" }`.
- Type 'l': Merged. `{ text: "hel" }`.
- Type 'l': Merged. `{ text: "hell" }`.
- Type 'o': Merged. `{ text: "hello" }`.
- Type ' ': Merged. `{ text: "hello " }`.
- Type 'w': Merged. `{ text: "hello w" }`.
- Type 'o': Merged. `{ text: "hello wo" }`.
- Type 'r': Merged. `{ text: "hello wor" }`.
- Type 'l': Merged. `{ text: "hello worl" }`.
- Type 'd': Merged. `{ text: "hello world" }`.
- Two-second pause.
- Type '!': Merge check fails (>300ms since last). New action `{ type: "insert", col: 11, text: "!" }` pushed.

The undo stack now has two entries. Pressing Ctrl+Z once deletes "!" and restores the cursor. Pressing Ctrl+Z again deletes "hello world" and restores the cursor to (0, 0). This is exactly the behavior the user expects — the continuous typing is one undo step, and the character after the pause is a separate step.

The merge timeout of 300 milliseconds is a tuning parameter. A shorter timeout would create more undo entries (finer granularity). A longer timeout would create fewer (coarser granularity). The value of 300ms is a common choice that matches the natural rhythm of typing — most people type fast enough that consecutive characters are less than 300ms apart, but most pauses (for thought, reading, or repositioning) are longer than 300ms.

Let us also trace through a Backspace merging example. The user has typed "hello" on line 0 and now presses Backspace three times rapidly.

- Backspace 1: Deletes 'o' at column 4. Action: `{ type: "delete", line: 0, col: 4, text: "o" }`. Pushed as new entry (no previous delete to merge with).
- Backspace 2: Deletes 'l' at column 3. Merge check: both are deletes, within 300ms, same line, no newlines in either text, and `action.col + action.text.length === last.col` (3 + 1 === 4). Merge succeeds. Action becomes `{ col: 3, text: "lo" }`.
- Backspace 3: Deletes 'l' at column 2. Merge check: 2 + 1 === 3. Merge succeeds. Action becomes `{ col: 2, text: "llo" }`.

Pressing Ctrl+Z once will now re-insert "llo" at column 2, restoring "hello". The three Backspace presses are undone in a single step.

For Delete key merging, the pattern is different. The user positions the cursor at column 0 and presses Delete three times on "hello":

- Delete 1: Deletes 'h' at column 0. Action: `{ type: "delete", line: 0, col: 0, text: "h" }`. Pushed as new entry.
- Delete 2: Deletes 'e' at column 0. Merge check: `action.col === last.col` (0 === 0). Merge succeeds. Action becomes `{ col: 0, text: "he" }`.
- Delete 3: Deletes 'l' at column 0. Merge check: 0 === 0. Merge succeeds. Action becomes `{ col: 0, text: "hel" }`.

Notice the difference: with Backspace, the text is prepended and the column shifts left. With Delete, the text is appended and the column stays the same. This is because Backspace removes characters to the left (the cursor moves left with each press), while Delete removes characters to the right (the cursor stays put).

There are several conditions that break merging and force a new undo entry:

- **A pause longer than 300ms.** The user stops to think, then continues typing.
- **A newline.** Pressing Enter always starts a new undo entry, even if the user is typing quickly. This feels right — undoing a line break is a distinct action from undoing character typing.
- **A multi-character operation.** Pasting "hello world" from the clipboard is not merged with the previous keystroke, because the inserted text is longer than one character.
- **A different line.** If the cursor moves to a different line between edits (via mouse click, arrow key, or any other navigation), the next edit starts a new entry.
- **A type change.** An insert following a delete, or vice versa, always starts a new entry.
- **Non-adjacent position.** For inserts, the new column must be immediately after the last insert's text. For deletes, the positions must be adjacent in the appropriate direction.


## 4.5 — Selection-Aware Editing

The editing operations we have built so far — `insertText` and `deleteRange` — operate on explicit positions. But many editor actions are implicitly defined by the cursor and selection. When the user types a character, it should be inserted at the cursor. When the user presses Backspace with a selection active, the selection should be deleted, not the character before the cursor. These behaviors are implemented in the editor's event handlers, not in the `Doc` class, but they follow a consistent pattern that is worth examining.

The `deleteSelection` method on the `Doc` is the bridge between selection state and the edit primitives:

```javascript
deleteSelection() {
  if (!this.hasSelection()) return "";
  const sel = this.getNormalizedSelection();
  const text = this.deleteRange(
    sel.fromLine, sel.fromCol, sel.toLine, sel.toCol
  );
  this.setCursor(sel.fromLine, sel.fromCol);
  this.clearSelection();
  return text;
}
```

It normalizes the selection to get `from` before `to`, calls `deleteRange` to remove the text (which records the action on the undo stack), moves the cursor to the start of the deleted range, and clears the selection. The deleted text is returned for use by callers like the cut operation.

The pattern for selection-aware editing appears in every event handler that modifies text. Here is the general shape:

```javascript
// In _insertTextAtCursor:
if (this.doc.hasSelection()) this.doc.deleteSelection();
const l = this.doc.cursorLine;
const c = this.doc.cursorCol;
this.doc.insertText(l, c, text);
// ... move cursor to end of insertion ...
```

The first thing we do is check for an active selection. If one exists, we delete it. This handles the common case where the user selects a word and types over it — the selection is deleted, and the new text is inserted at the position where the selection started. Because `deleteSelection` moves the cursor to the selection start, the subsequent insertion happens at the right place.

The same pattern appears in `_handleBackspace`:

```javascript
_handleBackspace(ctrl) {
  if (this.doc.hasSelection()) {
    this.doc.deleteSelection();
    return;
  }
  // ... normal backspace logic ...
}
```

If there is a selection, Backspace deletes it and returns immediately — no character-level deletion is performed. The same is true for `_handleDelete`. This means that Backspace and Delete always do the "right thing" regardless of whether a selection is active.

For clipboard operations, the pattern is slightly different:

```javascript
// Copy (window event handler):
if (this.doc.hasSelection()) {
  e.clipboardData.setData("text/plain", this.doc.getSelectedText());
  e.preventDefault();
}

// Cut (window event handler):
if (this.doc.hasSelection()) {
  e.clipboardData.setData("text/plain", this.doc.getSelectedText());
  this.doc.deleteSelection();
  e.preventDefault();
}
```

Copy extracts the selected text and puts it on the clipboard without modifying the document. Cut does the same, then deletes the selection. Both call `e.preventDefault()` to stop the browser from trying to handle the copy/cut itself (which it cannot do meaningfully, since our text is on a canvas, not in a DOM element).

Paste checks for an active selection first:

```javascript
// In _insertTextAtCursor, called from paste handler:
if (this.doc.hasSelection()) this.doc.deleteSelection();
this.doc.insertText(l, c, text);
```

If text is selected when the user pastes, the selection is replaced by the pasted text. This is standard editor behavior.

The `deleteSelection` method creates a single undo entry for the deletion. If it is followed by an insertion (as in typing over a selection), the insertion creates a second undo entry. This means undoing a "type over selection" requires two Ctrl+Z presses: one to undo the insertion, one to undo the deletion (which restores the selected text). Some editors merge these into a single undo entry, but the two-step approach is simpler and still correct — the text is fully restored after both undos.


## 4.6 — Clipboard Integration

The clipboard is the mechanism that connects our editor to the rest of the operating system. When the user copies text in our editor, they should be able to paste it in another application. When they copy in another application, they should be able to paste in our editor. This requires us to integrate with the browser's clipboard API.

The browser provides three clipboard-related events: `copy`, `cut`, and `paste`. These fire when the user presses Ctrl+C, Ctrl+X, and Ctrl+V, respectively (or the corresponding menu items or keyboard shortcuts on their platform). We listen for these events on the `window` object:

```javascript
window.addEventListener("paste", (e) => {
  const text = e.clipboardData.getData("text/plain");
  if (text) {
    this._insertTextAtCursor(text);
    this.needsRedraw = true;
  }
  e.preventDefault();
});

window.addEventListener("copy", (e) => {
  if (this.doc.hasSelection()) {
    e.clipboardData.setData("text/plain", this.doc.getSelectedText());
    e.preventDefault();
  }
});

window.addEventListener("cut", (e) => {
  if (this.doc.hasSelection()) {
    e.clipboardData.setData("text/plain", this.doc.getSelectedText());
    this.doc.deleteSelection();
    this.needsRedraw = true;
    e.preventDefault();
  }
});
```

Why `window` and not the canvas? Because the canvas element is not a text input, it does not normally receive clipboard events. By listening on `window`, we catch the events regardless of which element has focus. We call `e.preventDefault()` to suppress the browser's default clipboard behavior, which would try to interact with the DOM in ways that do not apply to our canvas-based editor.

The `paste` event handler reads text from the clipboard with `e.clipboardData.getData("text/plain")`. This returns a string, or an empty string if the clipboard does not contain text. We insert the text at the cursor position using `_insertTextAtCursor`, which handles selection deletion, insertion, and cursor movement. We always call `preventDefault` on paste, because without it the browser might try to paste into a focused element somewhere else in the page.

The `copy` event handler writes text to the clipboard with `e.clipboardData.setData("text/plain", text)`. We only do this if there is an active selection — copying when nothing is selected should do nothing. The `preventDefault` call is necessary because without it, the browser would also try to copy the content of the focused element, which is not what we want.

The `cut` event handler combines copy and delete. It writes the selected text to the clipboard, then calls `deleteSelection` to remove it from the document.

There is one situation where the synchronous clipboard API is not sufficient: when the user triggers a paste through a menu item rather than a keyboard shortcut. The `paste` event only fires in response to a user-initiated paste gesture (Ctrl+V or right-click paste). If we trigger a paste from our File > Edit > Paste menu, there is no `paste` event. In this case, we use the asynchronous `navigator.clipboard` API:

```javascript
case "paste":
  navigator.clipboard.readText().then(text => {
    if (text) {
      this._insertTextAtCursor(text);
      this.needsRedraw = true;
    }
  });
  break;
```

The `navigator.clipboard.readText()` method returns a promise that resolves with the clipboard text. This API requires that the page is focused and that the user has granted clipboard permission (which browsers typically grant implicitly for user-initiated actions). The asynchronous nature means we cannot insert the text immediately — we insert it when the promise resolves and trigger a redraw.

For copy and cut via the menu, we use the write side of the async API:

```javascript
case "cut":
  if (this.doc.hasSelection()) {
    navigator.clipboard.writeText(this.doc.getSelectedText());
    this.doc.deleteSelection();
  }
  break;
case "copy":
  if (this.doc.hasSelection()) {
    navigator.clipboard.writeText(this.doc.getSelectedText());
  }
  break;
```

The dual approach — synchronous `e.clipboardData` for keyboard-driven operations, asynchronous `navigator.clipboard` for menu-driven operations — covers both use cases. The keyboard path is preferred because it is synchronous and does not require permission prompts, but the async fallback ensures that menu items work as expected.

One subtlety worth noting is that the synchronous clipboard API (`e.clipboardData`) is only available during the handling of a clipboard event. You cannot call `e.clipboardData.setData` at an arbitrary time — it must be within a `copy`, `cut`, or `paste` event handler. Outside of these events, the data transfer object is no longer writable. This is a browser security restriction designed to prevent scripts from silently reading or modifying the clipboard without user intent. The asynchronous `navigator.clipboard` API has its own restrictions — it requires the page to be focused, and some browsers require a secure context (HTTPS) — but it can be called at any time as long as the permission conditions are met.

For our editor, the practical upshot is simple. Keyboard shortcuts (Ctrl+C, Ctrl+X, Ctrl+V) fire clipboard events, so we use the synchronous API in those handlers. Menu items do not fire clipboard events, so we use the async API in the menu action handlers. The same text is read and written in both cases; only the API differs.


## 4.7 — The Complete Edit Flow

Let us trace through a complete editing sequence to see how all the pieces fit together. The user types the letter 'x' at position (5, 10) with no selection active.

1. The browser fires a `keydown` event with `key = "x"`.
2. The editor's `_onKeyDown` handler identifies this as a printable character (length 1, no Ctrl held) and calls `this._insertTextAtCursor("x")`.
3. `_insertTextAtCursor` checks `this.doc.hasSelection()` — no selection, so it continues.
4. It reads the cursor position: line 5, col 10.
5. It calls `this.doc.insertText(5, 10, "x")`.
6. `insertText` calls `_pushUndo({ type: "insert", line: 5, col: 10, text: "x", cursorLine: 5, cursorCol: 10 })`.
7. `_pushUndo` checks for merging. If the last action was also an insert, on the same line, at the adjacent column, within the 300ms timeout, and was also a single character — it merges by appending "x" to the last action's text. Otherwise, it pushes a new action and clears the redo stack.
8. `insertText` calls `_rawInsert(5, 10, "x")`.
9. `_rawInsert` splits line 5 at column 10, inserts "x" in the middle, and calls `_rehighlightFrom(5)` to update the syntax highlighting cache.
10. Back in `_insertTextAtCursor`, the cursor is moved to (5, 11) — one column to the right of the insertion point.
11. `_computeGutter` is called in case the line count changed (it did not, in this case).
12. `this.needsRedraw = true` is set, and `_ensureCursorVisible` is called.
13. On the next frame, the render loop sees `needsRedraw` is true, calls `_draw`, and the updated text appears on screen with the cursor at its new position.

Now the user presses Ctrl+Z:

1. The `keydown` handler detects Ctrl+Z and calls `this.doc.undo()`.
2. `undo` pops the most recent action from the undo stack. Let us say the merged action is `{ type: "insert", line: 5, col: 10, text: "x", cursorLine: 5, cursorCol: 10 }`.
3. Because it is an insert, `undo` computes the end position: line 5, col 11 (one character after the start).
4. It calls `_rawDelete(5, 10, 5, 11)`, which removes the "x" from line 5.
5. The cursor is restored to (5, 10) — the pre-edit position from the action record.
6. The action is pushed onto the redo stack.
7. The selection is cleared.
8. `this.needsRedraw = true` is set.
9. On the next frame, the original text is rendered.

This sequence demonstrates the separation of concerns. The event handler (`_onKeyDown`) knows about keyboard shortcuts and input routing. The editor method (`_insertTextAtCursor`) knows about cursor positioning and selection. The document method (`insertText`) knows about undo recording. The raw method (`_rawInsert`) knows about modifying the line array. And the undo method (`undo`) knows about reversing recorded actions. Each layer has a single responsibility, and they compose cleanly.


## 4.8 — What We Have, and What Comes Next

We now have a complete editing system. The `Doc` class can insert text at any position and delete text from any range, with both operations automatically recorded on an undo stack. The undo system supports unlimited undo and redo, with intelligent merging that combines rapid keystrokes into single undoable actions. The redo stack is correctly invalidated when new edits are made. Selection-aware editing ensures that all operations — typing, backspace, delete, cut, paste — correctly handle the case where text is selected. And clipboard integration connects the editor to the operating system through both the synchronous and asynchronous clipboard APIs.

The editing system is the engine of the editor. In Chapter 5, we will build the controls: the keyboard input handling that turns keystrokes into cursor movements, text insertions, deletions, and navigations. Every key on the keyboard — arrows, Home, End, Page Up, Page Down, Tab, Enter, Backspace, Delete, and every printable character — needs to be handled correctly, including with Ctrl and Shift modifiers. This is where the editor starts to feel like a real tool rather than a data structure with a rendering layer.

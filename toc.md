# Building a Text Editor from Scratch: A Canvas Rendering Approach

### Inspired by rxi's *lite* — Implemented in JavaScript

---

## Table of Contents

---

### Chapter 1: The Blank Canvas

*Why build a text editor from scratch? Why canvas? Setting up the project, understanding device pixel ratio, and drawing your first rectangle.*

- 1.1 — Why Build a Text Editor?
  - What existing editors get right and wrong
  - The philosophy behind rxi's *lite*: simplicity as a feature
  - What "from scratch" means in a browser context
  - What the reader will build by the end of this book
- 1.2 — Why Canvas Over the DOM?
  - The DOM text editing trap: contentEditable, textareas, and their limits
  - How canvas gives you pixel-level control
  - Trade-offs: accessibility, text input, clipboard — and how to solve each
  - How *lite* uses SDL and a software renderer; our canvas equivalent
- 1.3 — Project Setup
  - A single HTML file: why inline is fine for now
  - The HTML skeleton: a canvas element, a hidden file input, and nothing else
  - CSS reset: removing margins, setting dimensions, killing user-select
- 1.4 — Device Pixel Ratio: The First Thing You Must Get Right
  - What `window.devicePixelRatio` is and why it matters
  - The canvas sizing pattern: CSS size vs. backing store size
  - Drawing a test rectangle at native resolution
  - Handling resize and DPR changes (external monitors, zoom)
- 1.5 — The Render Loop
  - `requestAnimationFrame` vs. `setInterval`
  - The `needsRedraw` flag: why we don't repaint every frame
  - Delta time and frame timing
  - *lite*'s approach: redraw everything, but only when something changed
- 1.6 — Configuration and Theming
  - A `Config` object for fonts, sizes, spacing, and behavior
  - A `Theme` object for every color in the editor
  - Why separating configuration from code matters for extensibility
  - The *lite*-inspired dark color palette

---

### Chapter 2: The Font Atlas

*Rendering text on canvas the fast way — building a glyph atlas, understanding monospace metrics, and the tinted-blit technique for colored text.*

- 2.1 — The Problem with `fillText`
  - How the Canvas 2D API renders text internally
  - Why calling `fillText` per character is slow at scale
  - How game engines and GPU renderers solve this: texture atlases
  - What *lite* does: `stb_truetype` and glyph caching in C
- 2.2 — Designing the Font Atlas
  - Choosing a character range: printable ASCII (32–126) and why that's enough to start
  - Atlas layout: a 16-column grid of glyph cells
  - Measuring characters: `measureText`, `charWidth`, and `charHeight`
  - Scaling for device pixel ratio: the atlas lives in device pixels
  - The atlas canvas: an offscreen `<canvas>` element
- 2.3 — Rendering Glyphs to the Atlas
  - Setting up the offscreen context: font string, `textBaseline`, fill color
  - Rasterizing white glyphs onto a transparent background
  - Building the glyph lookup table: char code → `{ x, y, w, h }`
  - Handling line height: centering glyphs vertically in their cell
- 2.4 — The Tinted-Blit Technique
  - The problem: we need colored text, but the atlas is white
  - The solution: `globalCompositeOperation = "source-in"`
  - Step by step: clear → stamp glyphs → tint → blit to main canvas
  - The run canvas: a reusable offscreen buffer for each text run
  - Why this is fast: one `drawImage` call per color run, not per character
- 2.5 — Drawing Colored Text Runs
  - The `drawColoredText` method: an array of `{ text, color }` runs
  - Advancing the x position by `text.length * charWidth`
  - Handling non-ASCII fallback: direct `fillText` for characters outside the atlas
  - Memory management: reusing the run canvas, growing it only when needed
- 2.6 — Rebuilding the Atlas
  - When the font size changes (zoom in/out), the atlas must be rebuilt
  - Tearing down and recreating the `FontAtlas` instance
  - Re-deriving all layout metrics: `charWidth`, `charHeight`, gutter width, etc.

---

### Chapter 3: The Document Model

*How text is stored, addressed, and manipulated — the `Doc` class that holds lines, cursor state, and selection state.*

- 3.1 — Lines as the Fundamental Unit
  - Why an array of strings, not a rope, gap buffer, or piece table
  - *lite*'s `Doc.lines` table: the same approach, and why it works
  - Splitting input text on `"\n"` and the empty-document edge case
  - `getLine(idx)`: bounds-checked access
- 3.2 — The Cursor
  - Representing position: `(line, col)`, zero-indexed
  - Clamping: preventing the cursor from leaving the document
  - The `desiredCol` concept: remembering column intent during vertical movement
  - `setCursor(line, col, updateDesired)`: the gatekeeper method
- 3.3 — Selection
  - Selection as two endpoints: `(startLine, startCol)` and `(endLine, endCol)`
  - The `selectionActive` flag: distinguishing "no selection" from "zero-width selection"
  - Forward and backward selections: why the user drags in both directions
  - `getNormalizedSelection()`: always returning `from` before `to`
  - `getSelectedText()`: extracting text across multiple lines
- 3.4 — Coordinate Systems
  - Document coordinates: `(line, col)` — a position in the text
  - Canvas coordinates: `(x, y)` — a pixel on the backing store
  - Screen coordinates: `(clientX, clientY)` — where the mouse is
  - Converting between them: scroll offsets, gutter width, text padding, DPR
- 3.5 — Word Boundaries
  - What constitutes a "word": `[a-zA-Z0-9_$]` vs. everything else
  - `wordBoundaryLeft` and `wordBoundaryRight`: scanning from the cursor
  - Skipping whitespace before scanning: matching expected editor behavior
  - Crossing line boundaries: wrapping to the previous/next line
- 3.6 — File Identity
  - `filename`: tracking what file is open
  - `dirty`: has the document been modified since last save?
  - Syntax detection by file extension: connecting the document to the highlighter

---

### Chapter 4: Editing Operations and Undo/Redo

*Inserting text, deleting ranges, and building a full undo/redo system with intelligent action merging.*

- 4.1 — The Two Primitive Operations
  - Every edit is either an insert or a delete
  - `_rawInsert(line, col, text)`: splitting the line, splicing new lines in
  - `_rawDelete(fromLine, fromCol, toLine, toCol)`: joining lines, removing middles
  - Why "raw" — these methods don't touch the undo stack
  - Keeping the highlight cache in sync: `splice` on `hlCache`, then `_rehighlightFrom`
- 4.2 — The Undo Stack
  - Action records: `{ type, line, col, text, cursorLine, cursorCol }`
  - Why we store the cursor position *before* the edit
  - `insertText` and `deleteRange`: wrapping raw operations with `_pushUndo`
  - The redo stack: cleared on every new edit, populated on undo
- 4.3 — Undo and Redo Execution
  - Undoing an insert: compute the end position, call `_rawDelete`, restore cursor
  - Undoing a delete: call `_rawInsert` at the original position, restore cursor
  - Redo: the mirror image — replay the original action
  - Why `_raw` operations are critical: undo/redo must not push to the undo stack
- 4.4 — Action Merging
  - The problem: typing "hello" shouldn't create five undo entries
  - Time-based merging: `undoMergeTimeout` (300ms between keystrokes)
  - Character insert merging: same line, adjacent column, single character, no newline
  - Backspace merging: consecutive deletes from the same line
  - Delete key merging: consecutive forward-deletes at the same position
  - When merging stops: newlines, pauses, line changes, or a different operation type
- 4.5 — Selection-Aware Editing
  - `deleteSelection()`: normalize, delete range, set cursor to start, clear selection
  - The pattern: every insert/backspace/delete checks `hasSelection()` first
  - Cut: copy selected text to clipboard, then `deleteSelection()`
  - Paste: `deleteSelection()` first, then insert
- 4.6 — Clipboard Integration
  - The browser clipboard API: `copy`, `cut`, `paste` events
  - `e.clipboardData.setData` and `e.clipboardData.getData`
  - Why we intercept these events on `window`, not on a DOM element
  - The async clipboard API fallback for menu-driven paste

---

### Chapter 5: Keyboard Input and Navigation

*Handling every keypress — character insertion, arrow keys, Home/End, word jumping, page up/down, smart indent, and all the keyboard shortcuts.*

- 5.1 — The `keydown` Event Handler
  - Why `keydown`, not `keypress` or `keyup`
  - Extracting `key`, `ctrlKey`, `shiftKey`, `metaKey`
  - `e.preventDefault()`: when and why to suppress default browser behavior
  - The routing structure: shortcuts first, then navigation, then special keys, then printable characters
- 5.2 — Keyboard Shortcuts
  - Ctrl+Z / Ctrl+Shift+Z: undo / redo
  - Ctrl+S: save (triggering a download)
  - Ctrl+N: new document
  - Ctrl+O: open file (triggering the hidden file input)
  - Ctrl+A: select all
  - Ctrl+D: duplicate current line
  - Ctrl+=/Ctrl+-: zoom in/out
  - The Cmd key on macOS: why `e.metaKey` matters
- 5.3 — Arrow Key Navigation
  - Left/Right: move one column, wrapping across lines
  - Up/Down: move one line, using `desiredCol` to maintain horizontal position
  - Ctrl+Left/Right: word boundary jumping
  - The selection pattern: if Shift is held, start/update selection; if not, clear it
  - Collapsing selection on arrow key: jumping to the appropriate edge
- 5.4 — Home, End, Page Up, Page Down
  - Smart Home: toggle between first non-whitespace and column 0
  - End: jump to end of line
  - Ctrl+Home / Ctrl+End: jump to document start/end
  - Page Up/Down: move by a screenful of lines, adjusting scroll simultaneously
- 5.5 — Tab and Indentation
  - Tab: insert `tabSize` spaces at cursor
  - Shift+Tab: remove up to `tabSize` leading spaces from current line
  - Tab with selection: indent every selected line
  - Shift+Tab with selection: outdent every selected line
- 5.6 — Enter and Auto-Indent
  - Detecting the current line's leading whitespace
  - Carrying indentation to the new line
  - Extra indent after `{`, `:`, or `(`: a simple heuristic that covers most cases
  - Inserting `"\n" + indent` as a single operation
- 5.7 — Backspace and Delete
  - Simple backspace: delete one character left, or join with previous line
  - Smart backspace: when the line is all whitespace, delete to the previous tab stop
  - Ctrl+Backspace: delete to the previous word boundary
  - Delete key: mirror of backspace, forward direction
  - Ctrl+Delete: delete to the next word boundary
- 5.8 — Ensuring the Cursor Stays Visible
  - `_ensureCursorVisible()`: adjusting `scrollX` and `scrollY`
  - Vertical: if the cursor is above or below the viewport, scroll to reveal it
  - Horizontal: if the cursor is past the right edge, scroll right with padding
  - Calling this after every cursor movement and every edit

---

### Chapter 6: Mouse Input and Selection

*Click to place the cursor, drag to select, double-click to select a word, shift-click to extend — and scroll with the wheel.*

- 6.1 — Coordinate Conversion
  - `_toCanvas(e)`: multiplying `clientX`/`clientY` by DPR
  - `_canvasToTextPos(cx, cy)`: converting canvas pixels to `(line, col)`
  - Accounting for scroll offset, gutter width, and text padding
  - Clamping: `line` within `[0, lineCount-1]`, `col` within `[0, lineLength]`
- 6.2 — Click to Place Cursor
  - `mousedown` in the text area: compute position, set cursor, clear selection
  - Starting a potential drag selection: `startSelection()` + `isDraggingSelection = true`
  - Resetting the cursor blink timer on click
- 6.3 — Drag to Select
  - `mousemove` while `isDraggingSelection`: update cursor and selection end
  - Auto-scrolling: when the mouse is near the top or bottom edge, scroll the viewport
  - `mouseup`: stop dragging
- 6.4 — Shift-Click to Extend Selection
  - If Shift is held on `mousedown`, extend the existing selection
  - If no selection is active, start one from the current cursor position
  - Setting the selection end to the clicked position
- 6.5 — Double-Click to Select Word
  - The `dblclick` event: computing word boundaries around the click position
  - Setting selection start to the left boundary, end to the right
  - Moving the cursor to the right boundary
- 6.6 — Mouse Wheel Scrolling
  - `wheel` event with `{ passive: false }` to allow `preventDefault`
  - Applying `deltaY` (scaled by DPR) to `scrollY`
  - Clamping scroll to `[0, maxScroll]`
  - Dropdown menu scrolling: when a dropdown is open, scroll it instead
- 6.7 — Scrollbar Interaction
  - Hit-testing the scrollbar track and thumb
  - Dragging the scrollbar thumb: tracking the drag offset
  - Click on scrollbar track: jump the thumb to the click position
  - Converting scrollbar thumb position to `scrollY`

---

### Chapter 7: Syntax Highlighting

*A regex-based tokenizer with multi-line range support, incremental re-highlighting, and language definitions for ten syntaxes.*

- 7.1 — Architecture Overview
  - Three components, mirroring *lite*: `SyntaxDef`, `Tokenizer`, and the highlight cache
  - *lite*'s approach: `core.syntax`, `core.tokenizer`, `core.doc.highlighter`
  - Our approach: the same separation, adapted for JavaScript and regex
- 7.2 — Syntax Definitions
  - The `SyntaxDef` class: `name`, `extensions`, `patterns`
  - Two pattern types: single-line (`regex`) and multi-line ranges (`start`/`end`)
  - Token types: `comment`, `string`, `number`, `keyword`, `keyword2`, `literal`, `func`, `symbol`, `operator`, `normal`
  - Pattern precedence: first match wins, patterns higher in the list take priority
- 7.3 — The Tokenizer
  - `tokenize(lineText, state)`: the core function
  - The `state` object: `{ rangeIdx }` — tracking which multi-line range we're inside
  - Resuming a multi-line range from the previous line: scanning for the end pattern
  - The main loop: try each pattern against the remaining text at `pos`
  - Accumulating unmatched characters into `"normal"` tokens
  - Return value: `{ tokens, state }` — the tokens for this line and the state for the next
- 7.4 — Multi-Line Ranges
  - The problem: `/* ... */` comments and template strings can span many lines
  - How *lite* solves it: storing the pattern table index of the current range
  - Our implementation: `state.rangeIdx` points to the active range pattern
  - Start of a range: if no end is found on this line, return early with the range index
  - Continuation: next line begins by scanning for the end pattern
  - End of a range: consume up to the end match, reset `rangeIdx` to `null`
- 7.5 — The Highlight Cache
  - `hlCache`: an array of `{ tokens, state }` per line
  - `_highlightAll()`: full sequential tokenization at document load
  - `_rehighlightFrom(line)`: incremental re-tokenization after an edit
  - The early-exit optimization: if the new state matches the old cached state, stop
  - Cache invalidation on insert/delete: splicing `hlCache` to match `lines`
- 7.6 — Language Definitions
  - JavaScript: comments, strings (single, double, template), numbers, keywords, builtins, functions, operators
  - HTML: comments, tags, attributes, strings, entities
  - CSS: comments, strings, colors, units, at-rules, selectors, properties
  - Python: comments, triple-quoted strings, numbers, keywords, builtins
  - Lua: block comments, strings, long strings, keywords
  - C/C++: comments, preprocessor directives, strings, numbers, keywords, standard library
  - Rust: comments, strings, typed numbers, keywords, standard types, macros
  - JSON: keys, strings, literals, numbers
  - Markdown: headings, bold, italic, inline code, lists, links, blockquotes, fenced code
  - Plain: the empty fallback
- 7.7 — Syntax Detection and Switching
  - `detectSyntax(filename)`: matching file extensions to syntax definitions
  - `_setSyntax(idx)`: changing the active syntax, clearing the cache, re-highlighting
  - The View menu: toggling highlighting on/off, selecting a syntax manually
  - The status bar indicator: showing the active syntax name

---

### Chapter 8: The User Interface — Layout, Gutter, and Status Bar

*Laying out the editor's visual regions, drawing line numbers, the active-line highlight, the status bar, and the scrollbar.*

- 8.1 — Layout Computation
  - The four regions: menu bar (top), gutter (left), text area (center), status bar (bottom)
  - All measurements in device pixels: `menuBarH`, `statusBarH`, `gutterW`
  - `textAreaX`, `textAreaY`, `textAreaW`, `textAreaH`: derived from the others
  - Gutter width: dynamic, based on the number of digits in the line count
  - `_resize()`: recalculating everything when the window or DPR changes
- 8.2 — The Gutter
  - Drawing the background and the separator line
  - Rendering line numbers right-aligned with the font atlas
  - Highlighting the active line number in a brighter color
  - Visible line range: `startLine` from `scrollY / lineH`, `endLine` clamped to `lineCount`
  - Skipping off-screen lines for performance
- 8.3 — The Text Area
  - Clipping: `ctx.save()`, `ctx.rect()`, `ctx.clip()`, `ctx.restore()`
  - Drawing the active line highlight behind the text
  - Drawing selections: computing rectangles per line from the normalized selection
  - Drawing text: iterating visible lines, building color runs from tokens, calling `drawColoredText`
  - Drawing the cursor: a thin vertical rectangle, blink driven by `cursorBlink` timer
  - Scroll offset: shifting all drawing by `-scrollX` and `-scrollY`
- 8.4 — The Status Bar
  - Drawing the background at the bottom of the canvas
  - Left side: filename and `[modified]` indicator
  - Right side: syntax name (with off indicator), line/column, total line count
  - Rendering text with the font atlas at the status bar's vertical center
- 8.5 — The Scrollbar
  - When to show: only when `totalHeight > textAreaH`
  - Thumb size: proportional to `textAreaH / totalHeight`, with a minimum size
  - Thumb position: proportional to `scrollY / maxScroll`
  - Drawing: rounded rectangle with hover/drag highlight
  - The scrollbar track: a subtle background behind the thumb
- 8.6 — The Redraw Cycle
  - *lite*'s approach: redraw everything, use a hash grid to detect changed regions
  - Our simplified approach: `needsRedraw` flag, full repaint when set
  - When `needsRedraw` is set: any input event, cursor blink toggle, scroll, edit, resize
  - The render order: background → gutter → text area (clipped) → scrollbar → menu bar → status bar → dropdown → overlay
  - Why the menu bar draws last: it must cover the gutter and text area

---

### Chapter 9: The Menu System

*Building a top menu bar with dropdown menus, hover states, keyboard navigation, scrollable dropdowns, and check indicators.*

- 9.1 — Menu Data Structure
  - Top-level items: `{ label, children }` — File, Edit, View
  - Child items: `{ label, action, shortcut }` for commands
  - Separators: `{ type: "separator" }` for visual grouping
  - Checked items: `{ checked: () => boolean }` for toggle and radio states
  - Dynamic menus: `_buildSyntaxMenuItems()` — generating items from the syntax list
- 9.2 — Drawing the Menu Bar
  - Background, border line, and item layout
  - Computing item widths from label length and padding
  - Hover detection: comparing `mouseX`/`mouseY` against each item's bounds
  - Active state: the currently open dropdown's label is highlighted
- 9.3 — Drawing the Dropdown
  - Positioning: below the parent menu item, aligned to its left edge
  - Computing total height from item count (items + separators)
  - Viewport clamping: limiting dropdown height to fit on screen
  - Shadow, background, border, and clipping
  - Drawing items: hover highlight, label, shortcut text, check indicator
  - The check indicator: a bullet character drawn in the caret color for checked items
  - Scroll indicators: arrows at top/bottom when the dropdown is scrollable
- 9.4 — Click Handling
  - Menu bar click: toggle the dropdown for the clicked item
  - Dropdown click: execute the action, close the dropdown
  - Click outside: close the dropdown
  - Converting mouse position to dropdown item index, accounting for scroll offset
- 9.5 — Keyboard Navigation
  - Arrow Down/Up: move the hover index, skipping separators
  - Enter: execute the hovered item's action
  - Arrow Left/Right: switch to the adjacent top-level menu
  - Escape: close the dropdown
  - Auto-scrolling: when the keyboard selection moves off-screen in a tall dropdown
- 9.6 — Menu Actions
  - The `_executeMenuAction(action)` dispatcher
  - String-based action matching: `"new"`, `"open"`, `"save"`, `"undo"`, etc.
  - Prefix matching for dynamic actions: `"syntax_0"`, `"syntax_1"`, etc.
  - Actions that change editor state vs. actions that trigger async behavior (paste, open)

---

### Chapter 10: File I/O, Drag-and-Drop, and Putting It All Together

*Loading and saving files, the drag-and-drop overlay, architecture review, performance considerations, and where to go from here.*

- 10.1 — Opening Files
  - The hidden `<input type="file">` element
  - Triggering it from the File > Open menu or Ctrl+O
  - `FileReader.readAsText()`: loading the file content asynchronously
  - Creating a new `Doc` from the loaded text, resetting scroll and gutter
  - Syntax detection from the file's name
- 10.2 — Saving Files
  - `Blob` and `URL.createObjectURL`: creating a downloadable text file
  - Programmatic `<a>` click to trigger the download
  - Clearing the dirty flag on save
  - The filename from the document: defaulting to `"untitled"`
- 10.3 — Drag and Drop
  - `dragover`: preventing default, showing the overlay
  - `dragleave`: hiding the overlay
  - `drop`: preventing default, reading the dropped file
  - The drop overlay: a semi-transparent blue tint, dashed border, centered text
  - Why `dragover` must call `preventDefault` for `drop` to fire
- 10.4 — Architecture Review
  - The class hierarchy: `FontAtlas`, `SyntaxDef`, `Tokenizer`, `Doc`, `Editor`
  - Data flow: input events → state changes → `needsRedraw` → render
  - *lite*'s philosophy applied: polling over callbacks, simplicity over abstraction
  - The single-file constraint: everything inline, no build step, no dependencies
  - What we intentionally left simple and why
- 10.5 — Performance Considerations
  - The font atlas: amortized cost of glyph rendering
  - The highlight cache: incremental re-tokenization avoids full-document scans
  - The `needsRedraw` flag: skipping frames when nothing changed
  - Canvas clipping: the text area clip rect prevents overdraw
  - The run canvas: reusing a single offscreen buffer instead of allocating per-frame
  - Where the bottlenecks are: very long lines, extremely large files, regex backtracking
- 10.6 — What We Built
  - A complete, functional text editor in ~2,000 lines of JavaScript
  - Font atlas rendering with device pixel ratio support
  - A document model with full undo/redo and action merging
  - Multi-line selection with mouse and keyboard
  - Syntax highlighting for ten languages with multi-line range support
  - A menu system with dropdowns, keyboard navigation, and state indicators
  - File open/save and drag-and-drop
  - A status bar, gutter, scrollbar, and active-line highlight
- 10.7 — Where to Go from Here
  - Find and replace: a command palette or dialog
  - Multiple cursors and simultaneous editing
  - Split views: *lite*'s binary tree of nodes
  - A plugin system: overriding methods, like *lite*'s Lua approach
  - Line wrapping for long lines
  - Minimap scrollbar
  - Bracket matching and auto-close
  - File tree sidebar
  - Language server protocol integration
  - Porting to WebGL or WebGPU for GPU-accelerated rendering

---

**Total: 10 chapters, ~60,000+ words**

Each chapter builds directly on the previous one. The reader starts with a blank canvas in Chapter 1 and finishes with a fully working text editor in Chapter 10. Every line of code in the final `index.html` is explained, motivated, and connected to the design philosophy of rxi's *lite*.

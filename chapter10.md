# Chapter 10: File I/O, Drag-and-Drop, and Putting It All Together

*Loading and saving files, the drag-and-drop overlay, architecture review, performance considerations, and where to go from here.*

---

## 10.1 — Opening Files

A text editor that cannot open files is a toy. The ability to load an existing file, edit it, and save the changes is the fundamental contract between the editor and the user. In a browser-based editor, file access is mediated by the browser's security model — we cannot read files from the filesystem directly. Instead, we rely on two mechanisms: the file input element and drag-and-drop.

The file input element is a standard HTML form control:

```html
<input type="file" id="fileInput" />
```

It is hidden with CSS (`display: none`) because we do not want a visible file input widget cluttering our canvas UI. Instead, we trigger it programmatically when the user selects File > Open from the menu or presses Ctrl+O:

```javascript
case "open":
  document.getElementById("fileInput").click();
  break;
```

Calling `.click()` on a file input element opens the browser's native file picker dialog. The user navigates to the file they want, selects it, and clicks Open. The browser fires a `change` event on the input element, which we handle:

```javascript
document.getElementById("fileInput").addEventListener("change", (e) => {
  if (e.target.files.length > 0) {
    this._openFile(e.target.files[0]);
    e.target.value = "";
  }
});
```

We take the first file from the input's `files` list and pass it to `_openFile`. The `e.target.value = ""` reset is important — without it, selecting the same file a second time would not fire the `change` event, because the value would not have changed.

The `_openFile` method reads the file's contents using the `FileReader` API:

```javascript
_openFile(file) {
  const reader = new FileReader();
  reader.onload = (ev) => {
    this.doc = new Doc(ev.target.result, file.name);
    this.scrollX = 0;
    this.scrollY = 0;
    this._computeGutter();
    this.needsRedraw = true;
  };
  reader.readAsText(file);
}
```

`FileReader.readAsText()` reads the file as a UTF-8 string (the default encoding). This is asynchronous — the `onload` callback fires when the read is complete, which is typically within a few milliseconds for source code files but could be longer for very large files.

In the callback, we create a new `Doc` from the file's text and name. The `Doc` constructor splits the text into lines, detects the syntax from the filename, creates a tokenizer, and highlights the document. The scroll is reset to the origin. The gutter width is recomputed (the new file might have a different number of lines). And `needsRedraw` is set to trigger a repaint.

The old `Doc` object is simply abandoned. JavaScript's garbage collector will reclaim it and all its associated data — the lines array, the highlight cache, the undo and redo stacks. There is no cleanup code, no event unregistration, no teardown. The new document replaces the old one in a single assignment. This is one of the benefits of our simple architecture: there are no lingering references to the old document, no callbacks registered against it, no threads working on it. Replacing it is safe and instant.

The `file.name` property provides the filename for display in the status bar and for syntax detection. A file dragged from the filesystem will have a name like `"main.js"` or `"README.md"`. The `detectSyntax` function (from Chapter 7) matches the extension and selects the appropriate syntax definition, so the new file is immediately highlighted in the correct language.

There are a few edge cases worth considering. If the file is very large — say, a 10MB log file — the `FileReader.readAsText` call might take a noticeable amount of time. During this time, the editor continues to display the old document. The user might type or navigate, and then the `onload` callback fires and replaces the document. This could be confusing if the user is mid-edit. A more robust implementation could show a loading indicator or disable input during the read. For our purposes, the read is fast enough for typical source files (under a megabyte) that the delay is imperceptible.

If the file is binary rather than text — an image, a compiled executable, a zip archive — `readAsText` will produce a string full of garbled characters. The editor will display it, but it will not be useful. A production editor would detect binary files (by checking for null bytes in the first few kilobytes) and refuse to open them, or display a hex view. Our editor makes no such check — it treats all files as text.

If the file's encoding is not UTF-8, the text may display incorrectly. `readAsText` defaults to UTF-8, which is correct for the vast majority of modern source code files. Files in legacy encodings (Latin-1, Shift-JIS, Windows-1252) would need explicit encoding detection, which is beyond our scope. This is a limitation shared by many lightweight editors.


## 10.2 — Saving Files

Saving in a browser is not the same as saving in a native application. A native editor writes directly to the filesystem — `fwrite` or `fs.writeFileSync`. A browser-based editor cannot do this (outside of the experimental File System Access API, which is not widely supported). Instead, we create a downloadable file and trigger the browser's download mechanism:

```javascript
_saveFile() {
  const blob = new Blob([this.doc.lines.join("\n")],
    { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = this.doc.filename;
  a.click();
  URL.revokeObjectURL(url);
  this.doc.dirty = false;
}
```

The `Blob` constructor takes an array of data chunks and a MIME type. We join the document's lines with newlines to reconstruct the full file text, and we create a blob with the `text/plain` type.

`URL.createObjectURL` creates a temporary URL that points to the blob's data in memory. This URL looks something like `blob:http://localhost/a1b2c3d4-e5f6-7890-abcd-ef1234567890` — it is a local reference that only works within this page.

We then create a temporary `<a>` element, set its `href` to the blob URL, and set its `download` attribute to the desired filename. The `download` attribute tells the browser to download the linked resource rather than navigating to it, and it specifies the default filename for the download. Calling `.click()` on the anchor programmatically triggers the download.

`URL.revokeObjectURL` releases the blob URL. Without this, the URL would persist in memory until the page is unloaded. Since we only need it for the duration of the download trigger, we revoke it immediately.

Finally, we clear the dirty flag. The document has been saved (or at least offered for download — the user might cancel the download dialog, but we have no way to detect that). The `[modified]` indicator in the status bar disappears.

The user experience is that pressing Ctrl+S or selecting File > Save triggers a browser download. The file appears in the user's downloads folder (or wherever their browser saves downloads). This is not as seamless as a native save — the user has to find the file in their downloads and potentially move it to the desired location — but it is the best we can do within the browser's security model. The File System Access API (supported in Chrome and Edge) would allow us to write directly to the original file, but implementing it would add complexity for a feature that only works in some browsers.

The filename used for the download is `this.doc.filename`, which is set when the file is opened (from `file.name`) or defaults to `"untitled"` for new documents. If the user creates a new document and saves without opening a file first, the download will be named `"untitled"`.

It is worth noting the alternative: the File System Access API, available in Chrome and Edge, provides `showSaveFilePicker()` and `FileSystemWritableFileStream`, which allow the editor to write directly to a file on disk. This would give us native save behavior — pressing Ctrl+S would overwrite the original file in place, without a download dialog. The API also provides `showOpenFilePicker()` for opening files without the hidden input trick. However, the API is not available in Firefox or Safari as of this writing, and it requires HTTPS (or localhost). Using it would mean the editor only works fully in some browsers, which conflicts with our goal of universal simplicity. We use the Blob download approach because it works everywhere, even though it is less seamless.

If you wanted to add File System Access API support as a progressive enhancement, the pattern would be: try `window.showSaveFilePicker` first, and fall back to the Blob download if it is not available. The file handle from `showOpenFilePicker` could be stored on the document, and subsequent saves could write to the same handle without prompting. This would give the best of both worlds — native save in supported browsers, universal fallback everywhere else.


## 10.3 — Drag and Drop

Drag-and-drop is the second way to open a file, and it is often the most convenient. The user drags a file from their operating system's file manager (Finder, Explorer, or a Linux file manager) and drops it onto the editor window. The editor reads the file and opens it, just like File > Open.

The implementation requires three event listeners:

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

The `dragover` event fires continuously while a dragged item is over the canvas. Its handler does two things: it calls `e.preventDefault()` (which is mandatory — without it, the browser does not consider the canvas a valid drop target, and the `drop` event will never fire), and it shows the drop overlay by setting `showDropOverlay = true`.

The `dragleave` event fires when the dragged item moves off the canvas. Its handler hides the overlay.

The `drop` event fires when the user releases the dragged item over the canvas. Its handler prevents the browser's default drop behavior (which might try to navigate to the file or display it), hides the overlay, and opens the first dropped file using the same `_openFile` method that the file input uses.

The `e.dataTransfer.files` property is a `FileList` — the same type returned by `<input type="file">`. We take the first file. If multiple files are dropped, we ignore all but the first. A multi-tab editor would open all of them; our single-document editor opens one.

The drop overlay, which we described in Chapter 8, provides visual feedback during the drag. Without it, the user would have no indication that the editor can accept dropped files. The overlay — a blue tint with a dashed border and "Drop file to open" text — makes the interaction discoverable and confident. The user sees the overlay, knows the drop will be accepted, and releases the mouse.

The drag-and-drop flow is one of the simplest in the editor — three event handlers, one boolean flag, one method call. But it is one of the most impactful in terms of user experience. Being able to drag a file onto the editor and start working immediately, with no dialog boxes or navigation, is a small delight that makes the editor feel modern and responsive.

There is an interesting interaction between drag-and-drop and the existing document. When the user drops a file, the old document is replaced entirely — there is no "are you sure?" confirmation, even if the old document has unsaved changes. A production editor would check the dirty flag before replacing the document and prompt the user to save or discard changes. We omit this check for simplicity, but it would be a straightforward addition: check `this.doc.dirty` in the `drop` handler, and if true, show a confirmation (either a browser `confirm()` dialog or a custom canvas-drawn dialog) before proceeding.

The drop handler only accepts the first file if multiple files are dropped. A more complete editor could open all dropped files in tabs, or offer a file picker if multiple files are dropped. But in our single-document model, opening one file is the only sensible behavior.


## 10.4 — Architecture Review

With all the pieces in place, let us step back and examine the architecture of the editor as a whole. The complete editor is built from five classes and two standalone objects, all in a single HTML file with no external dependencies.

**`Config`** and **`Theme`** are plain objects that store all configurable values — font family, font size, line height, tab size, blink rate, undo merge timeout, and every color used in the editor. They are defined once at the top of the file and read throughout the code.

**`FontAtlas`** encapsulates glyph rendering. It rasterizes the printable ASCII characters onto an offscreen canvas, stores their positions in a lookup table, and provides `drawColoredText` and `_drawTintedRun` methods for drawing colored text using the tinted-blit technique. It is created once and rebuilt when the font size or DPR changes.

**`SyntaxDef`** and **`Tokenizer`** handle syntax highlighting. `SyntaxDef` is a passive data object holding a language's name, extensions, and pattern rules. `Tokenizer` is a stateless engine that converts a line of text into tokens using a given syntax. The `Syntaxes` array holds all registered language definitions.

**`Doc`** is the document model. It holds the text as an array of lines, the cursor as a `(line, col)` pair, the selection as two endpoints, the undo and redo stacks, the highlight cache, and the dirty flag. It provides methods for all text operations (insert, delete, undo, redo), all cursor and selection management, and all query operations (get line, get selected text, word boundaries).

**`Editor`** is the top-level controller. It owns the canvas, the font atlas, the document, the menu, and all UI state (scroll position, mouse position, drag state, active menu). It binds all event listeners, routes all input, manages the render loop, and draws every frame. It is the glue that connects everything.

The data flow follows a strict pattern:

1. An input event fires (keyboard, mouse, wheel, resize, paste, drop).
2. The event handler updates the editor's state — cursor position, selection, scroll offset, menu state, or document content.
3. The handler sets `this.needsRedraw = true`.
4. On the next animation frame, the render loop checks `needsRedraw`, calls `_draw`, and clears the flag.
5. `_draw` reads the current state and renders everything from scratch.

There are no callbacks between components. The `Doc` does not notify the `Editor` when text changes. The `FontAtlas` does not notify anyone when it is rebuilt. The `Editor` does not register listeners on the `Doc`. Instead, the `Editor` calls methods on the `Doc` and the `FontAtlas` directly, and it knows to redraw because it sets `needsRedraw` itself, in the same event handler that made the change.

This is *lite*'s philosophy in action. The author of *lite* deliberately avoided event listeners throughout the codebase, favoring polling over callbacks. The result is simpler, less error-prone code. There are no forgotten listeners, no ordering dependencies between event handlers, no risk of an event firing after its context has been destroyed. The state is always consistent when it is read, because it is only read at one point — during the draw call.

The single-file constraint is also deliberate. Everything — HTML, CSS, configuration, theme, font atlas, syntax definitions, tokenizer, document model, editor controller, and boot code — lives in one `index.html` file. There is no build step, no bundler, no transpiler, no package manager, no `node_modules` directory. You can copy this file to any computer, open it in any modern browser, and it works. This is not how you would structure a large production application, but for a tool that you want to understand completely, it is the right choice. Every part of the editor is visible in one scroll, searchable with Ctrl+F, and modifiable with any text editor.

The trade-off is that the file is about two thousand lines long, which is at the upper end of what a single file can comfortably hold. If the editor grew significantly — adding a file tree, a terminal, a settings panel, a plugin system — it would benefit from being split into modules. But at its current size, the single-file approach works well, and the absence of a build system lowers the barrier to understanding, modifying, and learning from the code.

There are several things we intentionally kept simple that a production editor would handle differently. Our document model uses a flat array of strings — a production editor for very large files would use a piece table or rope. Our syntax highlighting uses regex patterns — a production editor would use a Tree-sitter grammar or a TextMate grammar for more accurate highlighting. Our undo system stores full text copies — a production system might store incremental diffs to save memory. Our rendering does a full repaint on every change — a production system might use *lite*'s hash-grid caching or a retained-mode rendering approach.

In each case, we chose the simpler approach because it is easier to understand, easier to implement, and fast enough for the file sizes we target. The more complex approaches exist because they solve problems that appear at scale — files with millions of lines, languages with deeply nested grammars, editing sessions with thousands of undo entries. For a code editor that handles typical source files (a few hundred to a few thousand lines), the simple approaches are not just adequate — they are optimal, because they produce the simplest, most understandable code.

This is perhaps the most important lesson of the project. Simplicity is not a compromise. It is a feature. It makes the code easier to read, easier to debug, easier to extend, and easier to trust. The reader who understands every line of this editor has a deeper understanding of text editing than someone who can use a complex framework but cannot explain how it works.


## 10.5 — Performance Considerations

The editor is fast. On a modern machine, opening a file, editing text, scrolling, and navigating all feel instant. But "fast" is the result of specific design decisions, not an accident. Let us examine where performance matters and how the editor achieves it.

**The font atlas** amortizes the cost of glyph rendering. Rasterizing a glyph is expensive — the browser must parse the font's vector outlines, apply hinting, rasterize at the target size, and produce an anti-aliased bitmap. The atlas does this once per character (95 characters) and caches the result. All subsequent text drawing uses `drawImage` to stamp pre-rendered bitmaps, which is a fast memory copy. The amortized cost per character is effectively zero.

**The tinted-blit technique** reduces the number of draw calls. Instead of calling `fillText` for each character (or even each token), we call `drawImage` once per color run. A typical line of highlighted code has five to ten color runs, so a screenful of forty lines produces 200–400 `drawImage` calls. This is well within the performance budget of a canvas application at 60fps.

**The highlight cache** avoids re-tokenizing the entire document on every edit. When the user types a character, only the edited line (and possibly a few subsequent lines, if the tokenizer state changed) is re-tokenized. The early-exit optimization in `_rehighlightFrom` — stopping when the state matches the old cached state — makes the common case O(1). The cache also enables lazy tokenization: lines that are never scrolled to are never tokenized.

**The `needsRedraw` flag** eliminates rendering work when nothing has changed. During idle time, the cursor blinks at 1Hz, producing two redraws per second. Between blinks, no rendering happens at all — the render loop runs, checks the flag, and returns immediately. During active editing, the flag is set on every input event, producing one redraw per event. But between events, no rendering happens.

**Canvas clipping** prevents overdraw. The text area is clipped to its bounds, so scrolled text that extends past the gutter or below the status bar is not drawn. This is a hardware-level optimization — the canvas implementation discards pixels outside the clip path before they are composited, saving both computation and memory bandwidth.

**The run canvas** is reused across frames and across runs. Rather than allocating a new offscreen canvas for each `_drawTintedRun` call, we allocate one and reuse it. The canvas is grown only when a run exceeds its current width, which is rare because the initial width of 1024 pixels accommodates most runs.

**Visible-line-only rendering** ensures that the drawing cost is proportional to the viewport size, not the document size. A 100-line file and a 100,000-line file render at the same speed, because only the 40 or so visible lines are drawn. The line number gutter uses the same optimization.

Where are the bottlenecks? There are three scenarios where performance might degrade:

**Very long lines.** A line with 10,000 characters produces a large number of tokens and a very wide text run. The font atlas's run canvas would need to grow to accommodate the run width, and the `drawImage` call for such a wide run would be expensive. In practice, source code rarely has lines this long, but minified JavaScript or data files might. The solution would be to clip the run to the visible portion of the viewport, drawing only the characters that are on screen.

**Extremely large files.** A file with a million lines would not cause rendering problems (we only draw visible lines), but editing operations that use `Array.splice` — inserting or deleting lines — would be slow, because `splice` shifts all subsequent elements. The undo stack would also grow large. For files this size, a more efficient data structure (a rope or piece table) would be warranted.

**Regex backtracking.** Some of the syntax patterns use regex features that can cause catastrophic backtracking on certain inputs. For example, a pattern like `"(?:[^"\\]|\\.)*"` for matching strings is generally fast, but pathological inputs (a line with many backslashes and no closing quote) could cause the regex engine to explore exponentially many possibilities. The risk is low for typical source code but exists in theory.

For the file sizes and editing patterns that our editor targets — source code files of a few thousand lines, edited by a human at human speed — none of these bottlenecks are realistic concerns. The editor is fast enough to feel instant, and the design decisions that make it fast are the same decisions that make it simple.

There is one more performance consideration that is easy to overlook: **startup time.** When the editor loads, it creates the font atlas (rasterizing 95 characters), parses the welcome text (splitting into lines), tokenizes the entire document (running the regex-based tokenizer on each line), and renders the first frame. All of this happens synchronously before the user sees anything. For our small welcome document, this takes a few milliseconds. For a large file opened via URL parameters or pre-loaded content, it could take longer. The tokenization is the most expensive step — each line requires running multiple regex patterns, and a large file might have tens of thousands of lines. If startup time became a concern, we could defer the initial tokenization and highlight lines lazily as they are scrolled into view, which is what our `getTokensForLine` method already supports.

The overall performance profile of the editor is characterized by **low latency per interaction** (every keystroke, click, and scroll produces a visible result within one frame — 16.6ms at 60fps) and **low idle CPU usage** (when nothing is happening, no code runs between cursor blink toggles). This is the profile of a well-behaved interactive application, and it comes directly from the `needsRedraw` pattern: do work only when there is work to do.


## 10.6 — What We Built

Let us take a final inventory. Over the course of ten chapters, we have built a complete, functional text editor from a blank HTML canvas. The editor is approximately two thousand lines of JavaScript, all inline in a single HTML file, with no external dependencies. Here is what it contains:

**A font atlas rendering system** (Chapter 2) that rasterizes ASCII glyphs onto an offscreen canvas and stamps them in any color using the `globalCompositeOperation = "source-in"` tinting technique. The atlas respects the device pixel ratio for crisp rendering on high-DPI displays and is rebuilt automatically when the font size changes.

**A document model** (Chapter 3) that stores text as an array of strings, one per line. The model tracks the cursor position with a desired-column mechanism for natural vertical navigation, and a selection with two endpoints that can be normalized, extended, and queried.

**A complete undo/redo system** (Chapter 4) with two primitive operations (insert and delete), an undo stack that records every edit, intelligent action merging that combines rapid keystrokes into single undoable units, and a redo stack that is correctly invalidated on new edits.

**Keyboard input handling** (Chapter 5) for every key: printable characters, arrow keys with word jumping and selection extension, smart Home, Page Up/Down, Tab with four indentation behaviors, Enter with auto-indent, Backspace with tab-stop-aligned smart deletion, and keyboard shortcuts for undo, redo, save, open, select all, duplicate line, and zoom.

**Mouse input handling** (Chapter 6) for click-to-place-cursor, drag-to-select with auto-scroll, shift-click to extend selection, double-click to select word, mouse wheel scrolling with dropdown menu support, and scrollbar dragging with grab-point preservation.

**Syntax highlighting** (Chapter 7) with a regex-based tokenizer supporting both single-line patterns and multi-line ranges, an incremental highlight cache with early-exit optimization, and ten language definitions: JavaScript, HTML, CSS, Python, JSON, Lua, C/C++, Markdown, Rust, and Plain.

**A visual layout system** (Chapter 8) with four regions (menu bar, gutter, text area, status bar), a dynamic-width gutter, an active-line highlight, semi-transparent selection rectangles, a blinking cursor, a proportional scrollbar with rounded-rectangle thumb, and a file drop overlay.

**A menu system** (Chapter 9) with three top-level menus, dropdown panels with shadow and border, command items with shortcuts, separators, check indicators for toggle and radio state, dynamic syntax entries, scrollable dropdowns with scroll indicators, mouse hover and click handling, and full keyboard navigation.

**File I/O** (Chapter 10) with file opening via the file input element and drag-and-drop, file saving via Blob download, clipboard integration through both the synchronous event API and the asynchronous navigator API, and syntax detection from file extensions.

Every part of the editor is built from the same small set of primitives: `fillRect` for rectangles, the font atlas for text, `clip` for boundaries, and position arithmetic for layout. There are no third-party libraries, no framework abstractions, no build tools. The entire editor is understandable by a single person in a reasonable amount of time — which was, from the beginning, the goal.

It is worth reflecting on the journey from Chapter 1 to here. We started with a blank canvas and a CSS reset — a dark rectangle filling the browser window. In Chapter 2, we gave it the ability to render text through the font atlas. In Chapter 3, we created a data structure to hold text. In Chapter 4, we made the text editable with a full undo system. In Chapter 5, we connected the keyboard. In Chapter 6, we connected the mouse. In Chapter 7, we added color through syntax highlighting. In Chapter 8, we built the visual chrome — the gutter, the status bar, the scrollbar. In Chapter 9, we built the menu system. And in this chapter, we connected the editor to the filesystem.

Each chapter built directly on the previous ones. There were no detours, no circular dependencies, no features that required redesigning earlier work. The architecture supported incremental construction because it was designed for simplicity — each component has a clear boundary, a clear interface, and a clear purpose. Adding syntax highlighting did not require changing the font atlas. Adding the menu system did not require changing the document model. Adding file I/O did not require changing the rendering pipeline. The components are independent in their implementation and connected only through the editor class that orchestrates them.


## 10.7 — Where to Go from Here

The editor we have built is complete but not comprehensive. It is a solid foundation — a code editor that you can actually use for editing real code — but there are many features that a daily-driver editor would need. Here are the most impactful extensions, roughly ordered by complexity.

**Find and replace.** A text search that highlights all matches and allows stepping through them is one of the most-used features in any editor. The implementation would require a search input (either a command-palette-style overlay or a dedicated panel at the top or bottom of the text area), a match highlighting system that draws rectangles behind matching text on visible lines, and keyboard shortcuts for next/previous match and replace. The search itself can use `String.indexOf` for literal searches and the `RegExp` constructor for regex searches.

**Bracket matching and auto-close.** When the cursor is on a bracket (`{`, `(`, `[`), the matching bracket should be highlighted. When the user types an opening bracket, the closing bracket should be inserted automatically. This requires scanning the document for matching brackets (respecting nesting and ignoring brackets inside strings and comments) and adding a small insertion hook to the character input handler.

**Line wrapping.** Currently, long lines extend past the right edge of the viewport, and the user must scroll horizontally to see them. Soft line wrapping would display long lines across multiple visual rows without modifying the underlying text. This is a significant change — the line-to-y-position mapping would no longer be a simple multiplication, and the column-to-x-position mapping would need to account for wrap points. The coordinate conversion and selection rendering would all need to be updated.

**Multiple cursors.** Adding support for multiple simultaneous cursors (like VS Code's Ctrl+D) would require changing the cursor and selection from single values to arrays, and modifying every edit operation to apply to each cursor independently. The undo system would need to group multi-cursor edits into single undoable actions. This is one of the more complex extensions because it touches every part of the editor.

**Split views.** *Lite* implements a binary tree of nodes, where each node is either a horizontal split, a vertical split, or a leaf containing one or more views. This allows the user to view two parts of the same file side by side, or to view two different files at once. Implementing this would require separating the concept of a "view" (a viewport into a document) from the concept of a "document" (the text content), and managing multiple views with independent scroll positions and cursor states.

**A plugin system.** *Lite*'s most powerful feature is its plugin system, which allows plugins to override any function in the editor. A plugin that wants to run a build script on save simply wraps the save function with its own code that calls the original save and then executes the script. We could implement a similar system in JavaScript by storing methods as overridable properties and providing a `hook` function that wraps an existing method with before/after callbacks. The result would be an editor that can be extended without modifying its source code.

**A file tree sidebar.** A panel on the left side showing the directory structure of a project would require using the File System Access API (to read directory listings) or a server-side component (to serve directory listings over HTTP). In a pure browser environment without server support, this is limited to the files the user explicitly opens, but in an Electron or Tauri wrapper, full filesystem access would be available.

**Minimap.** A minimap is a zoomed-out view of the entire document, displayed as a narrow strip on the right side of the text area. The user can click on the minimap to jump to any part of the document. Implementing one would require rendering the entire document at a very small scale (perhaps 1–2 pixels per line) and overlaying a viewport indicator that shows which portion of the document is currently visible.

**Language Server Protocol integration.** The Language Server Protocol (LSP) defines a standard interface between an editor and a language server that provides autocomplete, go-to-definition, hover documentation, diagnostics, and other intelligent features. Integrating LSP would require a WebSocket connection to a language server process, a protocol implementation for the JSON-RPC messages, and UI elements for displaying autocomplete suggestions, diagnostics, and hover information.

**WebGL or WebGPU rendering.** Our Canvas 2D renderer is fast enough for a text editor, but a GPU-accelerated renderer would be significantly faster for very large files or very high refresh rates. The font atlas approach translates directly to GPU rendering — the atlas would be a texture, and each character would be a textured quad drawn by the GPU. Batch rendering could draw the entire visible text in a single draw call.

Each of these extensions builds on the foundation we have established. The architecture — a document model, a rendering pipeline, an input handling system, and a configuration layer — provides clean interfaces where new features can be attached. The simplicity of the existing code means that any of these extensions can be implemented by a single developer who understands the codebase, without needing to navigate a maze of abstractions.


## 10.8 — Closing Thoughts

We started this book with a blank canvas and a question: what does it take to build a text editor from scratch? The answer, it turns out, is not complexity. It is clarity.

The editor we built is not the most feature-rich editor in the world. It does not have a file tree, a terminal, a debugger, or an extension marketplace. It has about two thousand lines of code, which is roughly one-hundredth the size of VS Code's core editor and about one-fifth the size of *lite*. But it works. You can open a source file, edit it with syntax highlighting, undo your mistakes, select and copy text, save your changes, and see crisp, colored text on a high-resolution display. It does what a text editor needs to do.

More importantly, you understand how it works. Every pixel on the screen — from the glyph shapes in the font atlas to the selection rectangles to the menu bar's hover highlights — is produced by code you have read and can explain. There are no black boxes, no library internals, no framework magic. The editor is fully transparent to you.

This transparency is the gift that rxi gave the programming community with *lite*. The ability to look at a piece of software and understand it completely — to trust that you can fix it, extend it, or rewrite it if you need to — is rare and valuable. Most software is not like this. Most software is a tower of abstractions where each layer hides the complexity of the layer below it, and the programmer working at the top can only hope that the layers below are correct, performant, and stable. *Lite* rejected this approach. It chose simplicity, clarity, and directness, and the result was an editor that a single person could internalize in a week.

We have followed the same path. The decisions we made — an array of strings for the document, a font atlas for rendering, a `needsRedraw` flag for lazy repaint, plain objects for configuration, string-based action dispatch for menus, regex patterns for syntax highlighting — are not the most sophisticated solutions available. They are the simplest solutions that work correctly. And because they are simple, they are easy to understand, easy to debug, and easy to extend.

The editor you have built is yours. You can modify it, customize it, break it apart, and rebuild it. You can add the features you need and remove the ones you do not. You can use it as a starting point for a more ambitious project, or you can keep it as a reference implementation that you understand from top to bottom. The code is small enough to hold in your head and clear enough to return to after months away.

A text editor is the tool that builds every other tool. Now you know how to build one yourself.

If there is one thing to take away from this book, it is not any specific technique — not the font atlas, not the tinted-blit trick, not the undo merging algorithm, not the regex tokenizer. It is the approach. Start simple. Build incrementally. Understand every piece. Choose the straightforward solution over the clever one. Let the code be transparent enough that anyone — including your future self — can open the file, read the code, and know exactly what is happening and why.

This is how rxi built *lite*. This is how we built our editor. And it is how you can build anything.

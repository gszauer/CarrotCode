# Chapter 8: The User Interface — Layout, Gutter, and Status Bar

*Laying out the editor's visual regions, drawing line numbers, the active-line highlight, the status bar, and the scrollbar.*

---

## 8.1 — Layout Computation

The visual structure of the editor is divided into four regions: the **menu bar** at the top, the **gutter** on the left, the **text area** in the center, and the **status bar** at the bottom. A thin **scrollbar** sits at the right edge of the text area. Every pixel on the canvas belongs to one of these regions, and their boundaries are computed precisely in device pixels.

The layout is driven by the `_resize` method, which runs once during construction and again whenever the window size or device pixel ratio changes:

```javascript
_resize() {
  const w = window.innerWidth;
  const h = window.innerHeight;
  this.canvas.width = w * this.dpr;
  this.canvas.height = h * this.dpr;
  this.canvas.style.width = w + "px";
  this.canvas.style.height = h + "px";
  this.screenW = w;
  this.screenH = h;

  this.lineH = this.atlas.charHeight;
  this.charW = this.atlas.charWidth;
  this.menuBarH = Math.round(28 * this.dpr);
  this.statusBarH = Math.round(24 * this.dpr);

  this._computeGutter();

  this.textAreaX = this.gutterW;
  this.textAreaY = this.menuBarH;
  this.textAreaW = this.canvas.width - this.gutterW
    - Math.round(this.scrollbarWidth * this.dpr);
  this.textAreaH = this.canvas.height - this.menuBarH
    - this.statusBarH;

  this.needsRedraw = true;
}
```

The first block sets the canvas dimensions as we established in Chapter 1 — the backing store at `w * dpr` by `h * dpr`, the CSS size at `w` by `h`. Then we read the character dimensions from the font atlas. The `lineH` and `charW` are the device-pixel dimensions of a single character cell, and they are the fundamental units that drive the entire layout.

The menu bar and status bar have fixed heights in CSS pixels (28 and 24 respectively), scaled by DPR. These are intentionally smaller than the text lines — they are chrome, not content, and should be visually subordinate. The menu bar height was chosen to comfortably contain one line of text with padding above and below; the status bar is slightly shorter because it contains smaller, less prominent information.

The gutter width is dynamic:

```javascript
_computeGutter() {
  const digits = Math.max(3, String(this.doc.lineCount).length);
  this.gutterW = Math.round(
    (Config.gutterPaddingLeft + digits * (this.charW / this.dpr)
     + Config.gutterPaddingRight) * this.dpr
  );
}
```

The gutter must be wide enough to display the largest line number. For a 50-line file, that is two digits; for a 500-line file, three digits; for a 5,000-line file, four digits. We use a minimum of three digits (`Math.max(3, ...)`) so the gutter does not look excessively narrow in short files. The width is computed by adding the left padding, the width of the digit characters (number of digits times the CSS-pixel character width), and the right padding, then scaling the whole thing to device pixels.

The text area fills the remaining space. Its left edge is at the gutter's right edge (`this.gutterW`). Its top edge is at the menu bar's bottom edge (`this.menuBarH`). Its width is the canvas width minus the gutter width minus the scrollbar width. Its height is the canvas height minus the menu bar height minus the status bar height. These four values — `textAreaX`, `textAreaY`, `textAreaW`, `textAreaH` — define the clipping rectangle for all text rendering.

The layout is recomputed every time the window is resized, the DPR changes, or the font size changes (which triggers a font atlas rebuild and a resize). The `_computeGutter` method is also called after any edit that changes the line count, because the number of digits might change. If a file grows from 99 to 100 lines, the gutter widens from two digits to three, and the text area narrows accordingly.

This layout scheme is deliberately simple. There are no flexible containers, no constraint solvers, no layout passes. Each region's position is computed directly from the canvas dimensions and the fixed sizes of the adjacent regions. The computation is a handful of multiplications and subtractions, and it runs in constant time. This is another instance of the design principle that runs through the editor: use simple arithmetic wherever possible, and save complexity for the parts that genuinely need it.

The relationship between the layout metrics forms a dependency chain. The canvas dimensions come from the browser window. The font atlas comes from the font size and DPR. The character dimensions (`lineH`, `charW`) come from the atlas. The gutter width comes from the character width and the document's line count. The text area dimensions come from the canvas dimensions, the menu bar height, the status bar height, the gutter width, and the scrollbar width. The visible line range comes from the text area height and the line height. And the scrollbar thumb size comes from the visible line range and the total document height.

When any value in this chain changes, everything downstream must be recomputed. Changing the font size rebuilds the atlas, which changes the character dimensions, which changes the gutter width, which changes the text area width, which changes the number of visible lines. The `_resize` method recomputes the entire chain from top to bottom, and `_computeGutter` handles the gutter-specific portion. Because the chain is short and the computation is simple, recomputing everything is fast — there is no need for a dependency tracking system or incremental layout updates.

The layout metrics are stored as flat properties on the editor object: `this.menuBarH`, `this.gutterW`, `this.textAreaX`, and so on. They are not encapsulated in a layout object or computed by getter functions. This flat structure makes the drawing code straightforward — each drawing method reads the properties it needs directly from `this`, without indirection. The trade-off is that the properties must be kept consistent with each other, which is ensured by always computing them through `_resize` rather than modifying them individually.


## 8.2 — The Gutter

The gutter is the vertical strip on the left side of the editor that displays line numbers. It is one of the most important visual aids in a code editor — line numbers are referenced in error messages, code reviews, discussions, and debugging sessions. A gutter that is clear, well-aligned, and visually connected to the text area makes the editor feel professional.

```javascript
_drawGutter(ctx) {
  const startLine = Math.floor(this.scrollY / this.lineH);
  const visibleLines = Math.ceil(this.textAreaH / this.lineH) + 1;
  const endLine = Math.min(this.doc.lineCount,
    startLine + visibleLines);

  ctx.fillStyle = Theme.gutterBg;
  ctx.fillRect(0, this.menuBarH, this.gutterW, this.textAreaH);

  ctx.fillStyle = Theme.menuBorder;
  ctx.fillRect(this.gutterW - this.dpr, this.menuBarH,
    this.dpr, this.textAreaH);

  for (let i = startLine; i < endLine; i++) {
    const y = this.menuBarH + (i * this.lineH - this.scrollY);
    if (y + this.lineH < this.menuBarH
        || y > this.menuBarH + this.textAreaH) continue;

    const num = String(i + 1);
    const color = i === this.doc.cursorLine
      ? Theme.gutterActiveLine : Theme.gutterText;
    const numWidth = num.length * this.charW;
    const x = this.gutterW - Config.gutterPaddingRight * this.dpr
      - numWidth;
    this.atlas._drawTintedRun(ctx, num, x, y, color);
  }
}
```

The gutter is drawn in three layers. First, the background — a solid rectangle covering the gutter area. In our theme, the gutter background matches the editor background, creating a seamless look. Some themes use a slightly different gutter color to visually separate it from the text; our theme relies on the separator line and the muted text color to create the distinction.

Second, the separator line — a one-device-pixel-wide vertical line at the right edge of the gutter. This thin line visually separates the line numbers from the code without being obtrusive. It uses `Theme.menuBorder`, the same color used for the menu bar's bottom border, creating a consistent visual language for dividers throughout the editor.

Third, the line numbers. We only draw numbers for visible lines — the range from `startLine` to `endLine`. The visible range is computed from the scroll position and the text area height, just as in the text area drawing code. This means that in a 10,000-line file, we only draw the 40 or so line numbers that are on screen, not all 10,000.

Each line number is converted to a string (`String(i + 1)` — line numbers are one-indexed for display), and its pixel width is computed as `num.length * this.charW`. The x-position is calculated to right-align the number within the gutter: we start at the gutter's right edge, subtract the right padding, and subtract the number's width. This right-alignment ensures that single-digit and multi-digit numbers line up on their right edges, creating a clean column even as the numbers grow from 1 to 999.

The color of each line number depends on whether it is the cursor's line. The active line number is drawn in `Theme.gutterActiveLine` (a brighter gray), while all other line numbers use `Theme.gutterText` (a dim gray). This subtle highlight helps the user locate the cursor line at a glance, especially in large files where the cursor might be far from any distinguishing code feature.

The numbers are rendered using the font atlas's `_drawTintedRun` method — the same tinted-blit technique used for all text in the editor. This means line numbers are just as crisp as the code text, because they go through the same glyph atlas and DPR-aware rendering pipeline.

The gutter scrolls vertically in sync with the text area — both use the same `startLine` and `endLine` calculation, and both compute y-positions from `i * this.lineH - this.scrollY`. This synchronization is automatic because both methods use the same formula. If we ever changed the line height or scroll calculation in one method, we would need to change it in the other, but since both read from the same properties (`this.lineH`, `this.scrollY`, `this.menuBarH`), they stay in sync without any explicit coupling.

The gutter does not scroll horizontally. Line numbers are always fully visible regardless of the `scrollX` value. This is intentional — the gutter is a fixed reference that the user can always see, even when the text has scrolled far to the right. The text area's clipping rectangle begins at `this.gutterW`, so horizontally scrolled text never overlaps the gutter.

One visual detail: the gutter background is drawn as a single rectangle covering the full gutter area, and the line numbers are drawn on top of it. There is no per-line background — the gutter is a uniform color with numbers floating on it. The active line does not get a special gutter background (unlike some editors that highlight the entire row including the gutter). Our approach is simpler and cleaner: the active line number is distinguished only by its color, not by its background. The color change is subtle enough that it does not distract from the code, but noticeable enough that the eye catches it when scanning the gutter.


## 8.3 — The Text Area

The text area is where the code lives. It is the largest region of the editor and the most complex to draw, because it combines five visual layers: the active line highlight, the selection highlight, the text itself, and the cursor. All of these must be drawn within a clipping rectangle that prevents them from bleeding into the gutter or the status bar.

The drawing begins with clipping:

```javascript
_drawTextArea(ctx) {
  ctx.save();
  ctx.beginPath();
  ctx.rect(this.gutterW, this.menuBarH,
    this.textAreaW, this.textAreaH);
  ctx.clip();
  // ... draw everything ...
  ctx.restore();
}
```

The `ctx.save()` / `ctx.clip()` / `ctx.restore()` pattern is a standard canvas technique for constraining drawing to a region. After `clip()`, any drawing operations that extend outside the clipping rectangle are silently discarded. When we call `ctx.restore()`, the clip is removed and subsequent drawing (the menu bar, status bar, dropdown menus) can cover the full canvas. Without clipping, scrolled text would bleed past the gutter edge or below the status bar.

**The active line highlight** is a subtle background color that extends across the full width of the text area on the line where the cursor is:

```javascript
const cursorScreenY = this.menuBarH
  + (this.doc.cursorLine * this.lineH - this.scrollY);
if (cursorScreenY + this.lineH > this.menuBarH
    && cursorScreenY < this.menuBarH + this.textAreaH) {
  ctx.fillStyle = Theme.lineHighlight;
  ctx.fillRect(this.gutterW, cursorScreenY,
    this.textAreaW, this.lineH);
}
```

The line highlight is drawn before the text so the text renders on top of it. The visibility check ensures we do not draw the highlight when the cursor line has scrolled out of view — a minor optimization that avoids an unnecessary `fillRect` call. The highlight color (`Theme.lineHighlight`, `"#34343a"`) is only slightly lighter than the background (`"#2e2e32"`), producing a subtle band that is visible but not distracting.

**Selection rendering** is more complex because it must handle three cases: a single-line selection, the first line of a multi-line selection, a middle line, and the last line:

```javascript
_drawSelection(ctx, startLine, endLine, textX) {
  const sel = this.doc.getNormalizedSelection();
  ctx.fillStyle = Theme.selection;

  for (let i = Math.max(startLine, sel.fromLine);
       i <= Math.min(endLine - 1, sel.toLine); i++) {
    const y = this.menuBarH + (i * this.lineH - this.scrollY);
    let x1, x2;

    if (i === sel.fromLine && i === sel.toLine) {
      x1 = sel.fromCol * this.charW;
      x2 = sel.toCol * this.charW;
    } else if (i === sel.fromLine) {
      x1 = sel.fromCol * this.charW;
      x2 = (this.doc.getLine(i).length + 1) * this.charW;
    } else if (i === sel.toLine) {
      x1 = 0;
      x2 = sel.toCol * this.charW;
    } else {
      x1 = 0;
      x2 = (this.doc.getLine(i).length + 1) * this.charW;
    }

    ctx.fillRect(textX + x1 - this.scrollX, y,
      x2 - x1, this.lineH);
  }
}
```

For a single-line selection, we draw a rectangle from `fromCol` to `toCol`. For the first line of a multi-line selection, we draw from `fromCol` to one character past the end of the line — the extra character width indicates that the newline is included in the selection. For middle lines, we draw from column 0 to one past the line end. For the last line, we draw from column 0 to `toCol`.

The selection color (`Theme.selection`, `"#48505880"`) includes an alpha channel — the `80` hex suffix makes it semi-transparent. This means the selection overlays the text without obscuring it. The text is drawn on top of the selection, so the colored characters show through the translucent blue highlight. This layering — line highlight, then selection, then text — produces the expected visual result where all three are simultaneously visible.

The loop iterates only over lines that are both selected and visible, using `Math.max(startLine, sel.fromLine)` and `Math.min(endLine - 1, sel.toLine)`. This means a selection that spans the entire document only draws rectangles for the 40 or so visible lines, not for every line in the file.

Let us trace through a concrete selection example. The user selects from line 5, column 10 to line 8, column 3. The normalized selection is `{ fromLine: 5, fromCol: 10, toLine: 8, toCol: 3 }`. Suppose each character is 18 pixels wide:

- **Line 5** (first line): `x1 = 10 * 18 = 180`, `x2 = (lineLength + 1) * 18`. The rectangle starts at column 10 and extends past the end of the line. The `+ 1` adds an extra character width after the last character, visually indicating that the newline is included.
- **Line 6** (middle line): `x1 = 0`, `x2 = (lineLength + 1) * 18`. The full line is highlighted, including the trailing newline indicator.
- **Line 7** (middle line): Same as line 6.
- **Line 8** (last line): `x1 = 0`, `x2 = 3 * 18 = 54`. The rectangle covers from the start of the line to column 3. No trailing newline indicator, because the selection ends mid-line.

Each rectangle is drawn at `textX + x1 - this.scrollX` for its x-position — the text area's left edge, plus the column offset, minus the horizontal scroll. The y-position is `this.menuBarH + (i * this.lineH - this.scrollY)` — the standard line-to-canvas-y conversion. All four rectangles share the same semi-transparent fill color, producing a continuous blue band across the four lines.

**Text rendering** iterates over the visible lines and draws them using the font atlas:

```javascript
for (let i = startLine; i < endLine; i++) {
  const y = this.menuBarH + (i * this.lineH - this.scrollY);
  if (y + this.lineH < this.menuBarH
      || y > this.menuBarH + this.textAreaH) continue;

  const tokens = this.doc.getTokensForLine(i);
  const runs = [];
  for (let t = 0; t < tokens.length; t++) {
    const color = this.highlightEnabled
      ? tokenTypeToColor(tokens[t].type) : Theme.normal;
    runs.push({ text: tokens[t].text, color: color });
  }
  this.atlas.drawColoredText(ctx, runs,
    textX - this.scrollX, y);
}
```

Each line's tokens are retrieved from the highlight cache (or tokenized on demand), converted to color runs, and drawn at the appropriate y-position. The x-position starts at `textX` (the left edge of the text, past the gutter and padding) minus the horizontal scroll offset. The scroll offset is what makes horizontal scrolling work — long lines that extend past the right edge of the viewport are clipped by the clipping rectangle, and the user can scroll right to see them.

**The cursor** is drawn last, on top of everything else:

```javascript
if (this._lastBlinkOn) {
  const cx = textX + this.doc.cursorCol * this.charW
    - this.scrollX;
  const cy = this.menuBarH
    + (this.doc.cursorLine * this.lineH - this.scrollY);
  ctx.fillStyle = Theme.caret;
  ctx.fillRect(cx, cy + 2 * this.dpr,
    Math.max(2, this.dpr), this.lineH - 4 * this.dpr);
}
```

The cursor is a thin vertical rectangle — `Math.max(2, this.dpr)` pixels wide (at least 2 pixels for visibility) and slightly shorter than the line height (with 2 device pixels of padding above and below). It is only drawn when `_lastBlinkOn` is true — this is the blink state computed by the render loop, toggling between visible and invisible at `Config.cursorBlinkRate` intervals. The cursor color (`Theme.caret`, `"#93DDFA"`) is a light cyan that stands out against both the dark background and the colored syntax text.

The cursor position is computed from the cursor's document coordinates using the same formula as the coordinate conversion in Chapter 3 — column times character width for x, line times line height minus scroll for y. This is the reverse of the `_canvasToTextPos` conversion that the mouse handlers use, and the two must be exactly consistent. If the mouse conversion and the cursor drawing used different formulas, clicking on a character would place the cursor at a slightly wrong position.

The five layers — line highlight, selection, text, cursor — are drawn in a specific order that produces the correct visual result. The line highlight is behind everything, providing a subtle background. The selection is on top of the line highlight, providing a more visible colored band. The text is on top of both, so characters are always readable regardless of highlight or selection state. The cursor is on top of everything, so it is always visible even when it is on a selected character.

This layering is achieved entirely through drawing order — there is no blending mode or z-index system. Each layer simply draws on top of the previous one. The selection's semi-transparency (achieved through the alpha channel in its color) allows the line highlight to show through it, creating a composite appearance where both highlights are visible simultaneously. If the selection were fully opaque, it would hide the line highlight, and the user would lose the visual cue of the active line within a multi-line selection.

The clipping rectangle deserves further discussion. Without it, the text area's content would extend into the gutter (to the left) and below the status bar (at the bottom). The horizontal scroll could push text into negative x-coordinates that overlap the gutter. The vertical scroll could push text below the text area's bottom boundary. The clip constrains all of this — any pixels drawn outside the rectangle are silently discarded, regardless of how the scroll offset positions them. This makes the drawing code simpler, because it does not need to manually check whether each character or highlight rectangle falls within the visible area. It can just draw at the computed coordinates and trust the clip to handle the boundaries.

The one cost of clipping is that `ctx.save()` and `ctx.restore()` are not free — they push and pop the full canvas state, including the transformation matrix, fill/stroke styles, and the clip path. In practice, this cost is negligible compared to the actual drawing operations, but it is worth noting that the clip is only set once per frame, not once per line.


## 8.4 — The Status Bar

The status bar is a thin strip at the bottom of the editor that displays contextual information. It is the quietest part of the UI — small text, muted colors, always present but never demanding attention:

```javascript
_drawStatusBar(ctx) {
  const W = this.canvas.width;
  const y = this.canvas.height - this.statusBarH;

  ctx.fillStyle = Theme.statusBg;
  ctx.fillRect(0, y, W, this.statusBarH);

  const textY = y + Math.round(
    (this.statusBarH - this.atlas.charHeight) / 2
  );

  let leftText = this.doc.filename;
  if (this.doc.dirty) leftText += " [modified]";
  this.atlas._drawTintedRun(ctx, leftText,
    Math.round(10 * this.dpr), textY, Theme.statusText);

  const syntaxLabel = this.highlightEnabled
    ? this.doc.syntax.name
    : this.doc.syntax.name + " (off)";
  const rightText = syntaxLabel + "  Ln "
    + (this.doc.cursorLine + 1) + ", Col "
    + (this.doc.cursorCol + 1) + "  "
    + this.doc.lineCount + " lines";
  const rightW = rightText.length * this.charW;
  this.atlas._drawTintedRun(ctx, rightText,
    W - rightW - Math.round(10 * this.dpr),
    textY, Theme.statusText);
}
```

The status bar has a darker background than the text area (`Theme.statusBg`, `"#1d1d21"`), creating a visual anchor at the bottom of the editor. The text is vertically centered within the bar using the same formula as the menu bar: `(barHeight - charHeight) / 2`.

The left side shows the filename and a `[modified]` indicator when the document has unsaved changes. The right side shows four pieces of information: the syntax name (with an "(off)" suffix when highlighting is disabled), the cursor line and column (one-indexed for display — programmers expect line numbers to start at 1, not 0), and the total line count. These are the same pieces of information that the status bars of *lite*, VS Code, Sublime Text, and most other editors display. They answer the four questions a programmer most commonly has: "what file is this?", "what language mode am I in?", "where is my cursor?", and "how long is this file?".

The right-aligned text is positioned by computing its pixel width (`rightText.length * this.charW`) and subtracting from the canvas width with a small padding. Because we use a monospace font, the width calculation is exact — there is no need to call `measureText`.

The status bar updates on every frame that is drawn, because it reads from the cursor position and the dirty flag, both of which change frequently during editing. However, since the status bar is small (two text draws and one rectangle), the cost of redrawing it is trivial. There is no optimization to skip redrawing the status bar when its content has not changed — the full-redraw approach handles this automatically.

The choice of what to display in the status bar is driven by what programmers need to know at a glance. The filename confirms which file they are editing — essential when switching between multiple files or when the editor was opened via drag-and-drop. The dirty flag warns them about unsaved changes before they close the tab. The syntax name confirms that the correct language mode is active, which matters when the auto-detection picks the wrong syntax. The line and column numbers are referenced constantly during debugging ("the error is on line 42, column 15") and during pair programming ("go to line 180"). The total line count gives a sense of the file's size and helps estimate how far through a file the cursor is.

Other editors include additional information in the status bar — encoding (UTF-8 vs Latin-1), line ending style (LF vs CRLF), indentation mode (spaces vs tabs), and Git branch. We keep ours minimal, showing only the information that is useful on every glance.


## 8.5 — The Scrollbar

The scrollbar is a visual indicator of the viewport's position within the document and an interactive control for navigating. We covered its interaction behavior in Chapter 6; here we focus on how it is drawn.

```javascript
_drawScrollbar(ctx) {
  const sbRect = this._getScrollbarRect();
  if (!sbRect) return;

  const x = this.canvas.width
    - Math.round(this.scrollbarWidth * this.dpr);
  ctx.fillStyle = Theme.background;
  ctx.fillRect(x, this.menuBarH,
    Math.round(this.scrollbarWidth * this.dpr), this.textAreaH);

  const isHover = this.mouseX >= x;
  ctx.fillStyle = (isHover || this.isDraggingScrollbar)
    ? Theme.scrollbarHover : Theme.scrollbar;
  this._drawRoundRect(ctx,
    sbRect.x + 2 * this.dpr, sbRect.y,
    sbRect.w - 4 * this.dpr, sbRect.h,
    Math.round(3 * this.dpr));
}
```

The scrollbar is only drawn if `_getScrollbarRect` returns a non-null value — meaning the document is taller than the viewport. For short documents that fit entirely on screen, no scrollbar is shown.

The scrollbar track is drawn first — a rectangle matching the background color, covering the scrollbar's full height. Then the thumb is drawn as a rounded rectangle (using `_drawRoundRect`), inset by 2 device pixels from each side for a floating, pill-shaped appearance. The thumb color changes on hover and during dragging, providing visual feedback that the element is interactive.

The `_getScrollbarRect` method computes the thumb's geometry:

```javascript
_getScrollbarRect() {
  const totalH = this.doc.lineCount * this.lineH;
  if (totalH <= this.textAreaH) return null;
  const ratio = this.textAreaH / totalH;
  const sbH = Math.max(30 * this.dpr, ratio * this.textAreaH);
  const scrollRatio = this.scrollY / (totalH - this.textAreaH);
  const sbY = this.textAreaY
    + scrollRatio * (this.textAreaH - sbH);
  return { x: ..., y: sbY, w: ..., h: sbH };
}
```

The thumb height is proportional to the fraction of the document that is visible. If the viewport shows half the document, the thumb occupies half the track. If the viewport shows one-tenth, the thumb occupies one-tenth. The minimum height of 30 device pixels prevents the thumb from becoming too small to see or grab in very long documents.

The thumb position is proportional to the scroll offset within the scrollable range. When `scrollY` is 0, the thumb is at the top. When `scrollY` equals the maximum scroll (total height minus viewport height), the thumb is at the bottom. The linear mapping between scroll position and thumb position means the scrollbar is an accurate representation of the viewport's location in the document.

The scrollbar's visual weight is intentionally light. It is 12 CSS pixels wide, drawn with muted gray colors, and only brightens on hover. This follows the modern convention of "disappearing" scrollbars that are present but unobtrusive. The rounded corners of the thumb (3-device-pixel radius) and the 2-pixel inset from the track edges create a floating pill shape that looks contemporary. In contrast, older scrollbar designs used rectangular thumbs with visible track backgrounds and arrow buttons at each end — functional but visually heavy.

We do not draw a horizontal scrollbar. Horizontal scrolling is less common in a code editor (most lines fit within the viewport), and it is fully handled by the keyboard (Home/End) and mouse wheel (if horizontal scrolling is supported). Adding a horizontal scrollbar would be straightforward — the same proportional sizing and drag mechanics, applied horizontally — but we omit it for simplicity.


## 8.6 — The Redraw Cycle and Render Order

Every visual update in the editor goes through a single method: `_draw`. It is called by the render loop when `needsRedraw` is true, and it repaints the entire canvas from scratch:

```javascript
_draw() {
  const ctx = this.ctx;
  const W = this.canvas.width;
  const H = this.canvas.height;

  ctx.fillStyle = Theme.background;
  ctx.fillRect(0, 0, W, H);

  this._drawGutter(ctx);
  this._drawTextArea(ctx);
  this._drawScrollbar(ctx);
  this._drawMenuBar(ctx);
  this._drawStatusBar(ctx);

  if (this.activeMenu >= 0) {
    this._drawDropdown(ctx);
  }

  if (this.showDropOverlay) {
    this._drawDropOverlay(ctx, W, H);
  }
}
```

The render order is deliberate. We start with the background, which clears the entire canvas. Then we draw the gutter and text area, which occupy the main body of the editor. The scrollbar is drawn next, overlaying the right edge of the text area. Then the menu bar and status bar are drawn on top of everything — this ensures they cover any text or gutter content that might have been drawn in their regions. The dropdown menu, if open, is drawn on top of all of that. And the drop overlay, if active, covers everything.

This painter's algorithm — drawing back-to-front, with each layer covering the previous one — is the simplest approach to z-ordering in 2D rendering. It means the drawing order determines the visual stacking: later draws appear on top of earlier draws. There is no z-index system, no layer management, no compositor. Just a sequence of drawing calls, each one potentially overwriting parts of the previous ones.

The menu bar is drawn after the gutter and text area for a specific reason. The gutter extends from the menu bar's bottom edge to the status bar's top edge. If the gutter were drawn after the menu bar, the gutter's top would overwrite the menu bar's bottom border. By drawing the menu bar last (among the static elements), its border is always visible.

*Lite* takes a more sophisticated approach to redrawing. Rather than repainting everything, it uses a hash-grid-based caching system (detailed in rxi's "Cached Software Rendering" write-up) that detects which regions of the screen have changed between frames and only repaints those regions. The approach works by buffering all draw commands for each frame, hashing each command and mapping it to a grid of screen cells. If a cell's hash matches the previous frame's hash, that cell does not need to be redrawn. The application code acts as if it is doing a full redraw — it pushes the same draw commands every frame — but the renderer compares the command sequence with the previous frame's and only executes the commands whose output would differ. This gives the simplicity of full redraw with the efficiency of incremental updates.

The beauty of *lite*'s approach is that the application developer never thinks about dirty rectangles. There are no event listeners to register, no invalidation regions to track, no "damage" system to maintain. The code simply draws everything every frame, and the renderer is smart enough to skip the parts that have not changed. This leads to much simpler application code — the same simplicity benefit that we get from our `needsRedraw` flag, but with better per-frame performance for static content.

Our approach is simpler: we check `needsRedraw` and do a full repaint when it is set. This is less efficient — we repaint the entire canvas even if only the cursor blink state changed — but it is far simpler to implement. For a canvas-based editor of our size, the full repaint takes well under one millisecond, so the inefficiency is not perceptible. The `needsRedraw` flag itself provides the main optimization: when nothing has changed (no input, no blink toggle), no drawing happens at all. The cursor blinks at 1Hz, so we do one repaint per half-second during idle time. During active editing, we repaint on every keystroke, but the per-frame cost is low.

If we wanted to adopt *lite*'s approach, we could implement it without changing any of the drawing code. The draw methods would remain the same — they would just push commands to a buffer instead of calling `ctx.fillRect` and `ctx.drawImage` directly. The hash grid comparison would happen between the buffer-filling step and the actual rendering step. This is a pure optimization with no API change, which is why it is safe to start with the simpler full-repaint approach and add the hash grid later if performance becomes a concern.

The `needsRedraw` flag is set in many places throughout the editor: in every keyboard handler, every mouse handler, every scroll handler, the cursor blink toggle, the window resize handler, and after any edit operation. The pattern is always the same — perform the state change, then set `needsRedraw = true`. The flag is checked once per frame by the render loop, which calls `_draw` if set and clears the flag. This ensures that multiple state changes within a single event handler (for example, deleting a selection and inserting text in the same keystroke) produce only one repaint.


## 8.7 — The Drop Overlay

The drop overlay is a visual indicator that appears when the user drags a file over the editor window. It is not part of the normal UI — it appears only during a drag-over event and disappears immediately when the file is dropped or the drag leaves the window.

```javascript
_drawDropOverlay(ctx, W, H) {
  ctx.fillStyle = "rgba(80, 140, 255, 0.1)";
  ctx.fillRect(0, 0, W, H);
  ctx.strokeStyle = "#508CFF";
  ctx.lineWidth = 3 * this.dpr;
  const inset = 20 * this.dpr;
  ctx.setLineDash([8 * this.dpr, 6 * this.dpr]);
  ctx.strokeRect(inset, inset, W - inset * 2, H - inset * 2);
  ctx.setLineDash([]);

  ctx.font = Math.round(18 * this.dpr) + "px " + Config.fontFamily;
  ctx.fillStyle = "#508CFF";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText("Drop file to open", W / 2, H / 2);
  ctx.textAlign = "start";
}
```

The overlay consists of three elements: a semi-transparent blue tint over the entire canvas, a dashed border inset from the edges, and a centered "Drop file to open" message. The tint uses `rgba(80, 140, 255, 0.1)` — a very light blue that is visible but does not obscure the editor content underneath. The dashed border uses `setLineDash` with a pattern of 8 pixels on, 6 pixels off, scaled by DPR. The centered text is drawn with the standard `fillText` API rather than the font atlas, because this is the only place in the editor where we need centered text, and the atlas is not designed for centering.

The overlay is drawn last in the render order, on top of everything else including the dropdown menu. This ensures the user sees a clear, unobstructed signal that the editor is ready to receive a file. The overlay is controlled by the `showDropOverlay` boolean, which is set to `true` on `dragover` and `false` on `dragleave` or `drop`.

After the `setLineDash` call, we reset the dash pattern to an empty array (`ctx.setLineDash([])`). This is important — without the reset, subsequent `strokeRect` calls (for the scrollbar, the dropdown border, etc.) would also draw dashed lines. Canvas state persists across drawing calls within the same context, so any state modification must be either reverted or managed carefully.

Similarly, we reset `ctx.textAlign = "start"` after drawing the centered text. The atlas's `_drawTintedRun` assumes left-aligned text (the default), and setting `textAlign = "center"` would break all text drawing until the alignment is reset. This is one of the subtle pitfalls of the Canvas 2D API: every property you set on the context persists until you explicitly change it. The `ctx.save()` / `ctx.restore()` pattern can manage this automatically (as we do for the text area clipping), but for one-off properties like `textAlign` it is simpler to reset them manually.


## 8.8 — What We Have, and What Comes Next

We now have a complete visual layout. The editor draws four static regions (menu bar, gutter, text area, status bar), two interactive elements (scrollbar and cursor), two overlay layers (selection highlight and active line highlight), and two conditional overlays (dropdown menus and the file drop indicator). Every element is precisely positioned in device pixels, correctly scaled for high-DPI displays, and rendered using the font atlas for text or the Canvas 2D API for geometric shapes.

The drawing code is roughly 150 lines — `_draw` (10 lines), `_drawMenuBar` (25 lines), `_drawGutter` (20 lines), `_drawTextArea` (35 lines), `_drawSelection` (25 lines), `_drawScrollbar` (15 lines), `_drawStatusBar` (15 lines), `_drawDropOverlay` (15 lines). This is a small amount of code for a complete editor UI, and it follows a consistent pattern: compute positions from layout metrics, draw background rectangles, draw text using the atlas, handle visibility checks to skip off-screen elements.

In Chapter 9, we will build the most complex visual element in the editor: the dropdown menu system. Dropdown menus involve hit-testing, hover tracking, keyboard navigation, check indicators, scrollable content, shadow rendering, and click handling — all drawn on the canvas with no DOM assistance. It is the most thoroughly interactive piece of UI we will build.

Looking back at what we have accomplished in this chapter, a pattern emerges. Every drawing method follows the same structure: compute positions from layout metrics, set a fill color, draw a rectangle, render text with the atlas. The menu bar, gutter, text area, status bar, scrollbar, and overlays all use the same primitives — `fillRect` for shapes, `_drawTintedRun` for text, `clip` for boundaries. There are no custom rendering techniques, no special-purpose drawing APIs, no framework abstractions. The entire visual output of the editor is built from two operations: filling rectangles and stamping glyphs. This uniformity is not a limitation — it is a strength. It means every part of the UI is debuggable, understandable, and modifiable using the same mental model. If you can draw a rectangle and stamp text from the atlas, you can draw anything the editor needs.

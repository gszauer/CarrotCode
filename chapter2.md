# Chapter 2: The Font Atlas

*Rendering text on canvas the fast way — building a glyph atlas, understanding monospace metrics, and the tinted-blit technique for colored text.*

---

## 2.1 — The Problem with `fillText`

The Canvas 2D API gives you a function for drawing text: `ctx.fillText(string, x, y)`. It works. You set a font, set a fill color, call `fillText`, and characters appear on the canvas. For a button label, a tooltip, or a game score display, it is perfectly adequate.

For a text editor, it is not.

Consider what happens when you call `fillText`. The browser must take the string you have provided, look up each character in the current font, rasterize each glyph (converting the font's mathematical curves into a grid of pixels at the current size and resolution), apply subpixel anti-aliasing, composite the result onto the canvas, and advance to the next character. Most of this work happens in native code — the browser's text shaping and rendering pipeline — so it is fast in absolute terms. But "fast" is relative. A text editor might display forty lines of eighty characters on a typical screen. That is 3,200 characters per frame. On a high-resolution display, each character might be a 20×30 pixel glyph being composited onto a 5,120×2,880 backing store. If the editor is syntax-highlighted, different characters have different colors, which means you cannot draw an entire line in a single `fillText` call — you need one call per color run.

In practice, calling `fillText` for each color run on each visible line produces acceptable performance on modern hardware. You can build a working editor this way. But there is a better approach — one that is faster, gives us more control over the rendering, and teaches us a technique that is foundational to how real-time graphics applications work.

That technique is the texture atlas, and it is what game engines have been using for decades.

The idea is simple. Instead of asking the browser to rasterize glyphs on every frame, we rasterize them once, up front, into an offscreen image. This image — the atlas — contains a grid of pre-rendered glyphs. When we want to draw a character on screen, we do not ask the browser to look up the font, shape the glyph, and rasterize it. We just copy a rectangle of pixels from the atlas to the canvas. This is a `drawImage` call, which is one of the fastest operations the Canvas 2D API supports, because it is just a memory copy — no font lookup, no rasterization, no shaping.

This is closely related to what *lite* does at the C level. *Lite* uses `stb_truetype`, a single-header C library for reading TrueType fonts and rasterizing glyphs. When the editor starts, it rasterizes the glyphs it needs and caches them as bitmaps. When drawing text, it copies these cached bitmaps to the screen buffer. The underlying principle is identical: pay the cost of rasterization once, then reuse the result as many times as you need.

In our case, the "atlas" is an offscreen `<canvas>` element. We create it once, draw all the glyphs we need onto it in white, and then use it as the source for `drawImage` calls whenever we need to render text. But there is a wrinkle: we need colored text. We need keywords in yellow, strings in green, comments in gray, and so on. The atlas contains white glyphs. How do we draw them in color? That is the tinted-blit technique, which we will get to shortly. First, let us design and build the atlas itself.


## 2.2 — Designing the Font Atlas

A font atlas needs to answer two questions: which characters does it contain, and how are they arranged?

For a code editor, the character set is straightforward. The vast majority of source code consists of printable ASCII characters — the 95 characters from space (code point 32) through tilde (code point 126). This range covers all the letters, digits, punctuation, operators, brackets, and whitespace that appear in any programming language. We define our range with two constants:

```javascript
this.startChar = 32;
this.endChar = 126;
this.charCount = this.endChar - this.startChar + 1; // 95 characters
```

What about characters outside this range? Unicode identifiers, accented characters in comments, emoji in string literals — they all exist in real source code. We handle them with a fallback: any character that is not in the atlas is rendered directly with `fillText` at the point of use. This is slower than the atlas path, but it only applies to the rare non-ASCII characters. The common case — the 95 printable ASCII characters that make up the overwhelming majority of source code — goes through the fast atlas path.

The atlas is arranged as a grid. We choose 16 columns, which is a power of two (good for memory alignment, though that matters more on GPUs than in canvas) and divides nicely into our 95 characters. The number of rows is computed as the ceiling of `charCount / cols`:

```javascript
this.cols = 16;
this.rows = Math.ceil(this.charCount / this.cols); // ceil(95 / 16) = 6
```

So our atlas is a 16×6 grid of glyph cells. Each cell is `charWidth` pixels wide and `charHeight` pixels tall. But what are `charWidth` and `charHeight`?

We are using a monospace font, which means every character has the same advance width. We measure this width by creating a temporary canvas context, setting its font, and calling `measureText`:

```javascript
const measure = document.createElement("canvas").getContext("2d");
measure.font = this.font;
this.charWidth = Math.ceil(measure.measureText("M").width);
```

We use `Math.ceil` rather than rounding or truncating because we need an integer number of pixels, and we would rather have a slightly larger cell than risk clipping the edges of wider glyphs. The character "M" is traditionally used for width measurement because it is one of the widest characters in most fonts, but in a monospace font, every character is the same width, so the choice does not matter.

The character height is derived from the font size and the line height multiplier:

```javascript
this.charHeight = Math.ceil(this.scaledSize * Config.lineHeight);
```

Here, `this.scaledSize` is the font size in device pixels — `Math.round(fontSize * dpr)`. On a 2x Retina display with a 15px font, the scaled size is 30 pixels. With a line height of 1.5, the character height is `Math.ceil(30 * 1.5) = 45` device pixels. This height includes the vertical spacing between lines — each glyph cell is tall enough to contain the glyph itself plus the inter-line padding.

The atlas canvas is then created with dimensions large enough to hold the entire grid:

```javascript
this.atlasCanvas = document.createElement("canvas");
this.atlasCanvas.width = this.cols * this.charWidth;   // 16 * charWidth
this.atlasCanvas.height = this.rows * this.charHeight;  // 6 * charHeight
```

On our 2x Retina display with a 15px font, if `charWidth` comes out to 18 device pixels and `charHeight` to 45, the atlas canvas is 288×270 pixels. That is a tiny image — about 310 kilobytes of uncompressed RGBA pixel data. Creating it is fast, and using it as a source for `drawImage` is fast.

One important detail: the entire font atlas lives in device pixels. The font string is constructed with the scaled size — `"30px Consolas, 'Courier New', monospace"` on a 2x display — and the glyph measurements are in device pixels. The atlas canvas dimensions are in device pixels. When we later stamp glyphs from the atlas to the main canvas, both the source and destination are in device pixels, so there is no scaling involved in the `drawImage` call. This is what makes the rendering crisp: every pixel in the atlas maps one-to-one to a pixel on the main canvas, which maps one-to-one to a physical pixel on the display.

Before we render the glyphs, we also initialize a lookup table that maps character codes to their position in the atlas. This is a plain object — `this.glyphs = {}` — which we will populate in the next section.


## 2.3 — Rendering Glyphs to the Atlas

With the atlas canvas created and its dimensions computed, we can now rasterize the glyphs. This happens in the `_renderAtlas` method, which is called once from the constructor:

```javascript
_renderAtlas() {
  const ctx = this.atlasCanvas.getContext("2d");
  ctx.clearRect(0, 0, this.atlasCanvas.width, this.atlasCanvas.height);
  ctx.font = this.font;
  ctx.textBaseline = "top";
  ctx.fillStyle = "#ffffff";

  for (let i = 0; i < this.charCount; i++) {
    const ch = String.fromCharCode(this.startChar + i);
    const col = i % this.cols;
    const row = Math.floor(i / this.cols);
    const x = col * this.charWidth;
    const yOff = Math.floor((this.charHeight - this.scaledSize) / 2);
    const y = row * this.charHeight + yOff;
    ctx.fillText(ch, x, y);
    this.glyphs[this.startChar + i] = {
      x: x, y: row * this.charHeight,
      w: this.charWidth, h: this.charHeight
    };
  }
}
```

Let us walk through this step by step.

First, we get the 2D context of the atlas canvas and clear it. The canvas starts with all pixels transparent, which is important — the alpha channel of each pixel is what allows us to tint the glyphs later with the compositing trick.

We set three properties on the context. The `font` is the DPR-scaled font string we constructed in the constructor. `textBaseline = "top"` tells the browser to position the top of the text at the y-coordinate we specify, rather than the alphabetic baseline or the middle. This gives us predictable vertical positioning. `fillStyle = "#ffffff"` renders all glyphs in white. The color does not matter at this stage — we will tint the glyphs to the correct color when we draw them — but white is the right choice because it preserves the maximum alpha information. A white pixel with alpha 200 can be tinted to any color at any brightness. If we rendered in, say, dark gray, we would lose the ability to produce bright colors during tinting.

The loop iterates over all 95 characters. For each one, we compute its column and row in the grid using modular arithmetic: `col = i % this.cols` and `row = Math.floor(i / this.cols)`. The x-position of the glyph in the atlas is `col * this.charWidth`. The y-position needs a bit more thought.

Each cell in the atlas is `charHeight` pixels tall, but the glyph itself is only `scaledSize` pixels tall (the font size, without the line height padding). We want the glyph to be vertically centered within its cell, so we compute a vertical offset: `yOff = Math.floor((this.charHeight - this.scaledSize) / 2)`. The glyph is drawn at `y = row * this.charHeight + yOff` — that is, at the top of the cell plus the centering offset. This ensures that when we later stamp the entire cell onto the main canvas, the glyph appears vertically centered within the line height.

After drawing the glyph, we record its position in the lookup table. The key is the character code (an integer), and the value is an object with `x`, `y`, `w`, and `h`. Notice that the `y` in the lookup table is `row * this.charHeight`, not `row * this.charHeight + yOff`. The lookup table records the position of the full cell, not just the glyph within it. When we later copy from the atlas, we copy the entire cell — including the blank space above and below the glyph. This is exactly what we want, because the cell includes the line height padding.

The result of `_renderAtlas` is an offscreen canvas containing a 16×6 grid of white glyphs on a transparent background, and a lookup table that can translate any ASCII character code into the `(x, y, w, h)` rectangle of that character's cell in the atlas. If you were to make the atlas canvas visible (by appending it to the document body), you would see something like a typographer's specimen sheet — rows of white letters, numbers, and punctuation marks on a black background, neatly arranged in a grid.


## 2.4 — The Tinted-Blit Technique

We have a atlas full of white glyphs. We need colored text. Keywords should be yellow, strings should be green, comments should be gray. The naive approach would be to create a separate atlas for each color — one with yellow glyphs for keywords, one with green glyphs for strings, and so on. But that would multiply our memory usage by the number of distinct colors in the theme, and it would mean rebuilding multiple atlases whenever the font size changes.

The better approach is to keep a single white atlas and tint the glyphs to the correct color at draw time. This is the tinted-blit technique, and it relies on a feature of the Canvas 2D API called composite operations.

The key operation is `globalCompositeOperation = "source-in"`. Normally, when you draw something on a canvas, the new pixels are composited on top of the existing pixels using the default "source-over" mode — the new pixels cover the old ones, blended by alpha. But "source-in" does something different: it keeps only the pixels where both the existing content and the new content have non-zero alpha. The RGB values come from the new content, and the alpha comes from the intersection of the two.

In practical terms: if you have a canvas with a white letter "A" on a transparent background, and you fill the entire canvas with red using "source-in", the result is a red letter "A" on a transparent background. The red fill only appears where the white glyph had non-zero alpha — that is, where the letter was. The surrounding transparent pixels remain transparent because they had zero alpha and were excluded by the "source-in" operation.

This is exactly what we need. Here is the process for drawing a run of colored text:

1. Clear a temporary canvas (the "run canvas").
2. For each character in the run, copy its glyph cell from the atlas to the run canvas. This produces a row of white characters on a transparent background.
3. Set `globalCompositeOperation = "source-in"`.
4. Fill the entire run canvas with the desired color.
5. Reset `globalCompositeOperation = "source-over"`.
6. Copy the run canvas to the main canvas at the desired position.

After step 4, the run canvas contains a row of colored characters on a transparent background — exactly what we need to composite onto the editor's background. Step 6 is a single `drawImage` call that places the entire run on the main canvas.

Here is the implementation:

```javascript
_drawTintedRun(ctx, text, dx, dy, color) {
  if (text.length === 0) return;
  const w = text.length * this.charWidth;
  const h = this.charHeight;

  if (!this._runCanvas || this._runCanvas.width < w || this._runCanvas.height < h) {
    this._runCanvas = document.createElement("canvas");
    this._runCanvas.width = Math.max(w, 1024);
    this._runCanvas.height = h;
    this._runCtx = this._runCanvas.getContext("2d");
  }
  const rc = this._runCtx;
  rc.clearRect(0, 0, w, h);

  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    const g = this.glyphs[code];
    if (g) {
      rc.drawImage(this.atlasCanvas, g.x, g.y, g.w, g.h,
                   i * this.charWidth, 0, g.w, g.h);
    } else {
      rc.font = this.font;
      rc.fillStyle = "#ffffff";
      rc.textBaseline = "top";
      const yOff = Math.floor((this.charHeight - this.scaledSize) / 2);
      rc.fillText(String.fromCharCode(code), i * this.charWidth, yOff);
    }
  }

  rc.globalCompositeOperation = "source-in";
  rc.fillStyle = color;
  rc.fillRect(0, 0, w, h);
  rc.globalCompositeOperation = "source-over";

  ctx.drawImage(this._runCanvas, 0, 0, w, h, dx, dy, w, h);
}
```

Let us trace through this carefully.

The method takes five arguments: `ctx` (the main canvas context), `text` (the string to draw), `dx` and `dy` (the position on the main canvas), and `color` (a CSS color string like `"#E8BF6A"`).

The first thing it does is compute the pixel dimensions of the run: `w = text.length * this.charWidth` and `h = this.charHeight`. For a ten-character run with an 18-pixel character width, the run is 180 pixels wide.

Next, it checks whether the run canvas exists and is large enough. If not, it creates a new one. The minimum width is `Math.max(w, 1024)` — we allocate at least 1024 pixels wide even if the current run is shorter, because we expect to draw many runs and do not want to create a new canvas for each one. The run canvas and its context are stored as instance properties (`this._runCanvas`, `this._runCtx`) so they persist between calls.

The run canvas is cleared with `clearRect`. This is essential — the canvas must start with all pixels transparent for the compositing trick to work. If there were leftover pixels from a previous run, they would bleed through.

Then we loop through each character. For each one, we look up its glyph in the lookup table. If the glyph exists (the character is in the ASCII range), we copy its cell from the atlas to the run canvas using `drawImage`. The nine-argument form of `drawImage` specifies both the source rectangle (from the atlas) and the destination rectangle (on the run canvas). The destination x-position is `i * this.charWidth` — characters are placed side by side, each one `charWidth` pixels apart.

If the glyph does not exist — the character is outside the ASCII range — we fall back to `fillText`. We draw the character in white onto the run canvas at the appropriate position, mimicking what the atlas would have provided. This fallback is slower than the atlas path but handles any Unicode character the font supports. The key detail is that we still draw in white, because the tinting step that follows will color it correctly.

After all characters are drawn, we apply the tint. We set `globalCompositeOperation = "source-in"` and fill the entire run canvas with the desired color. The "source-in" operation replaces the white glyph pixels with the fill color while preserving their alpha values. Transparent pixels remain transparent. The result is a row of correctly colored glyphs on a transparent background.

We then reset the composite operation to `"source-over"` (the default) so that subsequent drawing operations on the run canvas behave normally.

Finally, we copy the run canvas to the main canvas with a single `drawImage` call. The source is the portion of the run canvas containing our text (from `0, 0` to `w, h`), and the destination is the position `dx, dy` on the main canvas. This composites the colored glyphs onto whatever background is already on the main canvas — the editor background, the line highlight, the selection highlight — with proper alpha blending.

The entire process — clear, stamp, tint, blit — is remarkably efficient. The stamping step is a series of memory copies from one canvas to another, with no text shaping or rasterization involved. The tinting step is a single fill operation. And the final blit is a single memory copy to the main canvas. For a typical line of syntax-highlighted code with five or six color runs, this produces five or six `drawImage` calls to the main canvas, compared to five or six `fillText` calls with the naive approach. The `drawImage` calls are faster because they skip the entire font rendering pipeline.

There is a subtlety in the anti-aliasing behavior that is worth noting. When the browser rasterizes a glyph with `fillText`, it produces pixels with varying alpha values at the edges of the glyph — this is anti-aliasing, and it makes the text look smooth. These alpha values are preserved in the atlas. When we tint with "source-in", the alpha values are preserved — the tint color replaces the white, but the alpha channel stays the same. When we blit to the main canvas, these partially-transparent edge pixels blend with the background, producing smooth, anti-aliased colored text. The tinted-blit technique does not sacrifice text quality. The output is visually identical to calling `fillText` directly with the desired color.

To be precise about what "source-in" does mathematically: for each pixel, the output color is the source color (our fill color), and the output alpha is `sourceAlpha * destinationAlpha`. A fully opaque pixel in the glyph (alpha 255) retains the fill color at full opacity. A partially transparent anti-aliased edge pixel (say, alpha 120) produces the fill color at alpha 120. A fully transparent pixel (alpha 0) stays transparent, because `sourceAlpha * 0 = 0` regardless of the source. This is exactly the behavior we need — the shape and smoothness of the original glyph is preserved, and only the color changes.

It is worth considering why we use "source-in" rather than other composite operations that might seem plausible. The "multiply" operation, for example, multiplies the source and destination color channels together. If you filled with a red color over a white glyph, you would get red — but the multiplication would reduce the brightness of partially-white anti-aliased pixels in unpredictable ways, producing color fringing. The "source-atop" operation is similar to "source-in" but also preserves the destination pixels outside the intersection, which would be correct but unnecessary here — we have already cleared the run canvas, so there are no destination pixels to preserve. "Source-in" is the cleanest and most predictable choice.

There is one more thing to understand about why we draw glyphs in white specifically, rather than some other color. The "source-in" operation takes its RGB values entirely from the source (the fill color), discarding the destination RGB values (the white glyph). So in principle, we could draw glyphs in any color — red, blue, gray — and the tinting would produce the same result. But white is the correct choice for two reasons. First, it is the convention, and anyone reading the code will immediately understand what is happening. Second, and more practically, if we ever switch to a different compositing strategy — for example, using "multiply" for a specific visual effect — white would be the only color that produces correct results, because multiplying any color by white leaves it unchanged. Drawing in white keeps our options open.


## 2.5 — Drawing Colored Text Runs

With `_drawTintedRun` handling the work of rendering a single colored string, we need a higher-level method that can render a complete line of syntax-highlighted text. A highlighted line is not a single string with a single color — it is a sequence of tokens, each with its own text and color. The keyword `function` is yellow, the space after it is white, the function name is blue, the parenthesis is cyan, and so on.

We represent this as an array of runs, where each run is an object with `text` and `color` properties:

```javascript
[
  { text: "function", color: "#E8BF6A" },
  { text: " ", color: "#c5c8c6" },
  { text: "hello", color: "#61AFEF" },
  { text: "()", color: "#56B6C2" },
  { text: " {", color: "#c5c8c6" },
]
```

The `drawColoredText` method takes this array and draws each run at the correct horizontal position:

```javascript
drawColoredText(ctx, runs, dx, dy) {
  let x = dx;
  for (let r = 0; r < runs.length; r++) {
    const run = runs[r];
    if (run.text.length > 0) {
      this._drawTintedRun(ctx, run.text, x, dy, run.color);
      x += run.text.length * this.charWidth;
    }
  }
}
```

The logic is straightforward. We start at position `dx` and iterate through the runs. For each non-empty run, we call `_drawTintedRun` to render it at the current x-position, then advance x by the pixel width of the run. Because we are using a monospace font, the pixel width is simply `text.length * this.charWidth` — no measurement needed, no kerning to worry about, no variable-width glyph advances to track.

This is one of the great simplifications that come from choosing a monospace font for a code editor. In a proportional font editor, you would need to measure the width of each character individually, accumulate the advances, and handle kerning pairs. With a monospace font, the horizontal position of any character on any line is a simple multiplication: `charIndex * charWidth`. This fact is used everywhere in the editor — not just in rendering, but in hit-testing mouse clicks, computing cursor positions, drawing selections, and placing the caret.

The `drawColoredText` method is called once per visible line during each frame. For a 40-line display with an average of five color runs per line, that is 200 calls to `_drawTintedRun` and 200 `drawImage` blits to the main canvas. This is well within the performance budget for a 60fps application.

Here is how the method is used in the editor's drawing code, which we will build fully in Chapter 8:

```javascript
for (let i = startLine; i < endLine; i++) {
  const y = this.menuBarH + (i * this.lineH - this.scrollY);
  const tokens = this.doc.getTokensForLine(i);
  const runs = [];
  for (let t = 0; t < tokens.length; t++) {
    const color = this.highlightEnabled
      ? tokenTypeToColor(tokens[t].type)
      : Theme.normal;
    runs.push({ text: tokens[t].text, color: color });
  }
  this.atlas.drawColoredText(ctx, runs, textX - this.scrollX, y);
}
```

The loop iterates over the visible lines (from `startLine` to `endLine`, computed from the scroll position and the viewport height). For each line, it gets the syntax tokens from the document's highlight cache, converts them to color runs by looking up each token type in the theme, and calls `drawColoredText`. The y-position is computed from the line index, the line height, the scroll offset, and the menu bar height. The x-position starts at `textX` (the left edge of the text area, past the gutter) minus the horizontal scroll offset.

Notice the `highlightEnabled` check. When syntax highlighting is turned off, all tokens use `Theme.normal` (the default text color), producing uniformly colored text. The rendering path is the same — the tokens are still there, with their original text boundaries — but the color variation is suppressed. This is a clean separation of concerns: the tokenizer produces tokens, the theme maps tokens to colors, and the renderer draws colored runs. Disabling highlighting only changes the mapping step.

There is one more detail in `_drawTintedRun` that deserves attention: the non-ASCII fallback path. When a character's code is not in the `glyphs` lookup table, the method falls back to direct `fillText`:

```javascript
} else {
  rc.font = this.font;
  rc.fillStyle = "#ffffff";
  rc.textBaseline = "top";
  const yOff = Math.floor((this.charHeight - this.scaledSize) / 2);
  rc.fillText(String.fromCharCode(code), i * this.charWidth, yOff);
}
```

This draws the character in white onto the run canvas at the correct position, using the same font and vertical centering as the atlas glyphs. The subsequent tinting step will color it correctly. The result is that non-ASCII characters render correctly — they just take the slower `fillText` path instead of the fast atlas path. If you open a file containing Unicode identifiers or comments in a non-Latin script, the characters will appear correctly, though the rendering will be slightly slower for lines containing many non-ASCII characters.

In practice, this fallback is rarely triggered for source code files. Most programming languages restrict identifiers to ASCII, and even in languages that support Unicode identifiers (like Python 3, Rust, or JavaScript), the vast majority of real-world code uses ASCII. The atlas path handles the common case, and the fallback handles everything else.


## 2.6 — Memory Management and the Run Canvas

The run canvas deserves a closer look, because its memory management strategy is a small but instructive example of a pattern that appears throughout graphics programming: allocate once, reuse many times, grow only when necessary.

When `_drawTintedRun` is first called, `this._runCanvas` is `null`. The method creates a new canvas with a width of `Math.max(w, 1024)` — either large enough for the current run, or 1024 pixels, whichever is bigger. The height is always `this.charHeight`, since all runs are a single line tall.

On subsequent calls, the method checks whether the existing run canvas is large enough for the current run. If the run is shorter than the canvas (the common case), we reuse the canvas as-is. We only `clearRect` the portion we need — `rc.clearRect(0, 0, w, h)`, where `w` is the width of the current run, not the full canvas. This avoids clearing pixels we are not going to use.

If the run is longer than the canvas (a rare case — it would require a single-color run longer than 1024 pixels / charWidth characters, which is about 56 characters at typical sizes), we create a new, larger canvas. The old canvas is abandoned and will be garbage collected.

This strategy has several nice properties. We avoid creating a new canvas for every `_drawTintedRun` call, which would be expensive — canvas creation involves allocating a pixel buffer and creating a GPU-backed texture in some browser implementations. We avoid allocating a massive canvas up front, which would waste memory if most runs are short. And we handle the occasional long run gracefully by growing the canvas when needed.

The initial size of 1024 pixels is a heuristic. At a typical character width of 18 device pixels, 1024 pixels can hold about 56 characters. Since syntax tokens are usually short — a keyword like `function` is 8 characters, a string literal might be 30 characters — this covers the vast majority of runs without ever needing to grow. The few runs that exceed this length (a very long string literal, or a long comment) trigger a one-time reallocation that brings the run canvas up to the needed size.

The run canvas context is also stored as an instance property (`this._runCtx`). Getting a canvas context with `getContext("2d")` is not free — the browser may need to set up internal state, allocate caches, and configure the rendering pipeline. By storing the context alongside the canvas, we avoid re-obtaining it on every call.

One thing we do not do is maintain a pool of run canvases. A single run canvas is sufficient because we draw runs sequentially — we never need two run canvases active at the same time. The sequence is always: clear the run canvas, stamp glyphs, tint, blit to main canvas, then repeat for the next run. The run canvas is available for reuse as soon as the blit is complete.

This is a recurring pattern in our editor: we allocate temporary resources lazily, reuse them aggressively, and grow them only when needed. It applies to the run canvas here, and the same thinking will appear later in how we manage the highlight cache and the undo stack.


## 2.7 — Rebuilding the Atlas

The font atlas is built for a specific font family, font size, and device pixel ratio. When any of these change, the atlas must be rebuilt from scratch. In our editor, this happens in three situations: when the user zooms in or out (changing `Config.fontSize`), when the window moves to a display with a different DPR, and when the editor first starts.

Rebuilding the atlas is simple because we encapsulated everything in the `FontAtlas` class. When we need a new atlas, we create a new instance:

```javascript
_rebuildAtlas() {
  this.atlas = new FontAtlas(Config.fontFamily, Config.fontSize, this.dpr);
  this._resize();
}
```

The old `FontAtlas` instance — with its atlas canvas, run canvas, and glyph lookup table — is abandoned. JavaScript's garbage collector will reclaim the memory.

Calling `this._resize()` after creating the new atlas is essential, because the character width and height may have changed. The `_resize` method reads `this.atlas.charWidth` and `this.atlas.charHeight` and uses them to recompute the layout metrics:

```javascript
_resize() {
  // ... canvas sizing ...
  this.lineH = this.atlas.charHeight;
  this.charW = this.atlas.charWidth;
  this.menuBarH = Math.round(28 * this.dpr);
  this.statusBarH = Math.round(24 * this.dpr);
  this._computeGutter();
  this.textAreaX = this.gutterW;
  this.textAreaY = this.menuBarH;
  this.textAreaW = this.canvas.width - this.gutterW
                   - Math.round(this.scrollbarWidth * this.dpr);
  this.textAreaH = this.canvas.height - this.menuBarH - this.statusBarH;
  this.needsRedraw = true;
}
```

The `lineH` and `charW` properties are the device-pixel dimensions of a character cell, derived directly from the atlas. Everything else — the gutter width, the text area dimensions, the visible line count — flows from these two values. When the font size changes, these values change, and the entire layout reconfigures itself.

The gutter width deserves a brief mention. It is computed by `_computeGutter`:

```javascript
_computeGutter() {
  const digits = Math.max(3, String(this.doc.lineCount).length);
  this.gutterW = Math.round(
    (Config.gutterPaddingLeft + digits * (this.charW / this.dpr)
     + Config.gutterPaddingRight) * this.dpr
  );
}
```

The gutter needs to be wide enough to display the largest line number in the document, plus padding on each side. The number of digits is at least 3 (so the gutter does not look absurdly narrow in a short file) and grows as the file gets longer. The digit width is `this.charW / this.dpr` — converting back to CSS pixels for the padding calculation, then multiplying the total by `this.dpr` to get device pixels. The result is a gutter width that adapts to both the font size and the document length.

This adaptive gutter is a small detail that makes the editor feel polished. In a file with 10 lines, the gutter is narrow. In a file with 10,000 lines, it is wider to accommodate five-digit line numbers. The text area adjusts its width accordingly, so the text always starts at the right position.

The zoom shortcuts in the editor (Ctrl+= to zoom in, Ctrl+- to zoom out) modify `Config.fontSize` and call `_rebuildAtlas`:

```javascript
case "zoomIn":
  Config.fontSize = Math.min(40, Config.fontSize + 1);
  this._rebuildAtlas();
  break;
case "zoomOut":
  Config.fontSize = Math.max(8, Config.fontSize - 1);
  this._rebuildAtlas();
  break;
case "zoomReset":
  Config.fontSize = 15;
  this._rebuildAtlas();
  break;
```

The font size is clamped between 8 and 40 CSS pixels. At 8 pixels, text is tiny but legible. At 40 pixels, it fills the screen with large, easy-to-read characters. The reset option returns to the default 15 pixels.

When the DPR changes (detected in the resize handler), we also rebuild:

```javascript
window.addEventListener("resize", () => {
  this.dpr = window.devicePixelRatio || 1;
  this.atlas = new FontAtlas(Config.fontFamily, Config.fontSize, this.dpr);
  this._resize();
});
```

This handles the case where the user drags the browser window from a standard monitor to a Retina monitor. The DPR changes from 1 to 2, the atlas is rebuilt at the higher resolution, and the text appears sharp on the new display.

The cost of rebuilding the atlas is low. Creating a canvas, setting up a font, and rasterizing 95 characters takes a few milliseconds at most. The user will never notice a pause when zooming or switching monitors. If we were building an editor that supported very large character sets — the full Unicode Basic Multilingual Plane, for example — the rebuild cost would be higher, and we might want to use a more incremental approach, rasterizing glyphs on demand as they are first used and caching them in the atlas. But for our 95-character ASCII atlas, the simple approach of rebuilding everything is both fast and simple.


## 2.8 — What We Have, and What Comes Next

In this chapter, we have built the rendering engine for our text editor. The `FontAtlas` class encapsulates everything related to glyph rasterization and text drawing:

The atlas itself is an offscreen canvas containing a 16×6 grid of 95 white ASCII glyphs, rendered at native resolution for the current display. A glyph lookup table maps character codes to their rectangle in the atlas. The tinted-blit technique uses the Canvas 2D API's `globalCompositeOperation = "source-in"` to color white glyphs to any desired color, using a reusable run canvas as an intermediate buffer. The `drawColoredText` method accepts an array of `{ text, color }` runs and draws them sequentially, advancing the x-position by `text.length * charWidth` for each run. Non-ASCII characters fall back to direct `fillText`, ensuring correctness for any Unicode content. The atlas is rebuilt from scratch when the font size or device pixel ratio changes.

This is a small amount of code — the entire `FontAtlas` class is under a hundred lines — but it is the foundation of every visual element in the editor. Every character you see on screen, from the code in the text area to the line numbers in the gutter to the labels in the menu bar to the cursor position in the status bar, is rendered through this class. It is the most performance-critical code in the editor, and it is also some of the simplest.

The design follows the same principles we established in Chapter 1: do the simple thing, do it once, reuse the result. The glyph rasterization happens once, when the atlas is created. The tinting and blitting happen per-run, not per-character. The run canvas is allocated once and reused. There are no complex caching strategies, no lazy loading, no atlas packing algorithms. For 95 characters, the simple grid layout is optimal.

In Chapter 3, we will build the data structure that holds the text itself — the `Doc` class. A document is an array of lines with a cursor, a selection, and methods for querying and modifying the text. It is the model in our model-view architecture: the `Doc` knows about the text, and the `FontAtlas` knows how to render it. The editor ties them together. With the atlas in hand, we can draw any string in any color at any position on the canvas. What we need now is something to draw.

# Chapter 1: The Blank Canvas

*Why build a text editor from scratch? Why canvas? Setting up the project, understanding device pixel ratio, and drawing your first rectangle.*

---

## 1.1 — Why Build a Text Editor?

There is a particular kind of software that programmers use more than any other: the text editor. It is the tool that builds every other tool. Compilers, web servers, operating systems, games — they all begin as text in an editor window. And yet, most programmers have never built one. The text editor sits in a strange blind spot. It feels like it should be simple — after all, you are just putting characters on a screen — but the moment you start, you discover a web of interacting concerns that are far richer than they appear. Cursor movement, selection, undo, word boundaries, syntax highlighting, scroll, layout, rendering — each of these is a system in its own right, and they all have to work together seamlessly, sixty times per second, without the user ever noticing the machinery.

This book is about building that machinery from nothing.

We are not going to use a rich text framework, or a code editor library, or even a `<textarea>` element. We are going to take an empty HTML canvas, a blank rectangle of pixels, and turn it into a fully functional text editor — one that supports syntax highlighting for ten programming languages, multi-line selection, full undo and redo with intelligent action merging, a menu system with dropdown menus and keyboard navigation, file loading and saving, drag-and-drop, and crisp rendering on high-DPI displays. The final product will be roughly two thousand lines of JavaScript, all in a single HTML file, with no dependencies, no build step, and no frameworks.

The editor we build is inspired by rxi's *lite*, a lightweight text editor written mostly in Lua with the lowest-level parts in C. *Lite* is a remarkable piece of software — not because it has more features than VS Code or Vim, but because it has fewer. It is around ten thousand lines of code total, most of it clear and direct Lua. It aims, in its author's words, to provide something practical, pretty, small and fast, implemented as simply as possible. That philosophy — simplicity as a deliberate engineering choice, not a limitation — is the guiding principle of this book.

There is a deeper reason to build a text editor from scratch, beyond the educational value. When you use software that you cannot understand, you are dependent on its authors in a way that you might not fully appreciate until the software breaks, or changes in a direction you do not like, or is abandoned. One of the most striking observations about *lite* comes from a programmer who decided to make it his daily editor. He noted that the entire editor was written in such a clear manner that even if the original author were to suddenly disappear, he would feel comfortable maintaining it himself. It took him about a week to internalize the codebase to the point where he was confident he could fix anything that needed fixing. That is an extraordinary property for a piece of software to have. It comes directly from the decision to keep things simple.

We are going to make the same decision. At every point in this book, when we face a choice between a clever solution and a straightforward one, we will choose the straightforward one. When we face a choice between adding abstraction and keeping the code flat, we will keep it flat. The reader who finishes this book will not just have a working text editor — they will have a text editor they understand completely, down to every pixel that appears on screen.

A word about what "from scratch" means in our context. We are building for the browser, which means we have the Canvas 2D API for drawing, the DOM event system for input, and the Clipboard API for copy and paste. We are not writing a font rasterizer or an event loop from zero — the browser provides those. But we are not using any higher-level abstractions either. No `contentEditable`, no `<textarea>`, no `document.execCommand`, no third-party libraries. Our text editor will be a canvas element and the code that draws to it. Everything between the raw browser APIs and the finished editor is ours.

By the end of this book, you will have built that editor. But more importantly, you will understand *how* text editors work — the data structures, the algorithms, the rendering tricks, the input handling patterns — and you will be able to modify, extend, or rebuild any part of it with confidence. Let us begin with a blank canvas.


## 1.2 — Why Canvas Over the DOM?

If you have ever tried to build a text editing experience in a web browser, you have probably encountered the two standard approaches. The first is to use a `<textarea>` element, which gives you a plain text editing surface with built-in cursor, selection, undo, keyboard handling, and clipboard support. The second is to use `contentEditable`, a DOM attribute that turns any element into an editable region, giving you rich text capabilities on top of the browser's layout engine. Both of these approaches are tempting because they give you so much for free.

They are also traps.

The `<textarea>` element works well for what it is designed for: editing a few lines of plain text in a form. But the moment you want syntax highlighting, line numbers, custom selection rendering, or any visual behavior that goes beyond the browser's built-in styling, you are fighting the element rather than working with it. You cannot color individual words in a textarea. You cannot draw a highlight behind the current line. You cannot render a gutter with line numbers that scroll in sync with the text. Some editors work around these limitations by overlaying a transparent textarea on top of a separate rendering layer, using the textarea only for its input handling. This is a valid approach, but it introduces its own complexity: keeping two representations in sync, handling the mismatch between the textarea's internal state and your display state, and dealing with platform-specific quirks in how textareas handle composition input, scrolling, and selection.

The `contentEditable` approach gives you more visual control, since you can style individual spans of text with CSS. This is how several well-known web editors work, including early versions of CodeMirror and the Ace editor. But `contentEditable` comes with a different set of problems, and they are worse. The browser's editing behavior is specified loosely, which means it behaves differently across Chrome, Firefox, Safari, and Edge. Pressing Enter might insert a `<br>`, or a `<div>`, or a `<p>`, depending on the browser and the current DOM structure. Backspace at the beginning of a styled span might merge it with the previous span, or delete the span entirely, or do nothing — and the behavior can change between browser versions. The developer who builds on `contentEditable` spends an enormous amount of time compensating for browser inconsistencies, and the result is code that is fragile, difficult to test, and difficult to understand. Modern editors like ProseMirror and the current version of CodeMirror have largely moved away from relying on `contentEditable` for text mutation, using it only as an input target while managing the DOM themselves.

The canvas approach avoids all of these problems by giving up on the browser's text editing machinery entirely. A `<canvas>` element is just a bitmap — a two-dimensional grid of pixels. The browser does not know or care that we are drawing text on it. There is no DOM structure to fight, no inconsistent editing behavior to compensate for, no styling system to work around. We are in complete control of every pixel.

This is exactly the approach that *lite* takes, translated from a desktop context to a browser context. *Lite* uses SDL (Simple DirectMedia Library) to create a window and handle input, and it renders everything with its own software renderer. There is no widget toolkit, no text layout engine, no operating system text editing APIs. The editor calls functions like `renderer.draw_rect` and `renderer.draw_text` directly. The author of the blog post about making *lite* his primary editor noticed this and called it out as a sign of respect: having direct access to the most basic drawing functions, without them being hidden behind layers of abstraction, means the programmer is trusted to be capable of using them.

We are going to do the same thing. Our canvas element is our window surface. Our Canvas 2D API is our renderer. And we will draw every rectangle, every glyph, and every cursor blink ourselves.

There are trade-offs to this approach, and we should be honest about them. Canvas-based text editing loses the accessibility features that the browser provides for native text inputs. Screen readers will not be able to read the content of our editor. Platform-specific text input features, like spell checking, autocomplete, and input method editors (IMEs) for non-Latin scripts, will not work automatically. The clipboard requires us to manually intercept the browser's copy, cut, and paste events. Selection is not handled by the operating system, so we cannot rely on the platform's native selection behavior.

These are real limitations, and they matter for production software. For the purposes of this book — building a code editor for programmers — they are acceptable trade-offs. Code editors generally do not use spell checking or autocomplete from the operating system, and most programmers work in ASCII or UTF-8 with Latin scripts. If you wanted to extend this editor for general-purpose text editing, you would need to add an IME composition layer and accessibility annotations, both of which are possible but beyond our scope. For now, we will focus on the core: rendering text, handling input, and making it feel right.


## 1.3 — Project Setup

Let us begin building. Open your text editor of choice (the irony is noted) and create a new file called `index.html`. This single file will contain everything: the HTML structure, the CSS, and all of the JavaScript. We are deliberately keeping everything in one place. There is no build system, no module bundler, no package.json, no node_modules directory. You can open this file in any web browser and it will run. This is not how you would structure a large production application, but it is exactly the right structure for learning, for experimenting, and for a tool that you want to understand completely.

Here is the skeleton:

```html
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Lite Canvas Editor</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  html, body { width: 100%; height: 100%; overflow: hidden; background: #2e2e32; }
  canvas { display: block; position: absolute; top: 0; left: 0; }
  #fileInput { display: none; }
  body { user-select: none; -webkit-user-select: none; }
</style>
</head>
<body>
<canvas id="canvas"></canvas>
<input type="file" id="fileInput" />
<script>
"use strict";

// Our editor code will go here.

</script>
</body>
</html>
```

Let us walk through every line, because nothing here is accidental.

The `<!DOCTYPE html>` declaration and `<html lang="en">` are standard HTML5 boilerplate. The `<meta charset="UTF-8">` ensures that our file is interpreted as UTF-8, which is important because our source code and the files we edit may contain characters outside the ASCII range. The `<meta name="viewport">` tag tells mobile browsers not to do any scaling — we will handle our own sizing.

The `<style>` block contains a CSS reset. The universal selector `*` sets `margin: 0`, `padding: 0`, and `box-sizing: border-box` on every element. This prevents the browser's default margins from creating gaps around the page edges. The `html` and `body` elements are set to `width: 100%` and `height: 100%` so they fill the entire browser window. `overflow: hidden` prevents scrollbars from appearing — we will handle all scrolling ourselves inside the canvas. The `background: #2e2e32` sets a dark gray background color. We use this specific color because it matches our editor's theme, so there is no flash of white before the canvas renders.

The canvas itself is styled with `display: block` (to remove the inline element's baseline gap), `position: absolute`, `top: 0`, and `left: 0`. This positions it at the top-left corner of the window, covering the entire viewport.

The `#fileInput` is a hidden file input element. We cannot style a file input to look like part of our canvas editor, so we hide it entirely and trigger it programmatically when the user chooses File > Open or presses Ctrl+O. The browser will show its native file picker dialog, the user will select a file, and we will read the file's contents using the FileReader API. This is a common pattern for canvas-based applications that need file input.

The `user-select: none` and `-webkit-user-select: none` on the body are important. Without these, clicking and dragging on the canvas would cause the browser to try to select page content (like the hidden file input), which produces a blue selection highlight that is confusing and visually distracting. By disabling user selection on the body, we ensure that all selection behavior comes from our own code.

Finally, `"use strict"` at the top of our script block enables JavaScript strict mode, which catches common coding mistakes — assigning to undeclared variables, using `with` statements, and other things that are almost always bugs. There is no reason not to use strict mode in new code.

The body of our HTML contains exactly two elements: a canvas and a hidden file input. Everything else — the menu bar, the status bar, the gutter, the text content, the cursor, the scrollbar — will be drawn onto the canvas with JavaScript.

If you save this file and open it in a browser, you will see a dark gray rectangle filling the entire window. That is our blank canvas. Now let us make it sharp.


## 1.4 — Device Pixel Ratio: The First Thing You Must Get Right

If you have ever drawn to a canvas and noticed that your text and lines look blurry, especially on a MacBook or a high-resolution monitor, you have encountered the device pixel ratio problem. Getting this right is the single most important rendering decision in a canvas application, and if you get it wrong, nothing else matters — the entire editor will look soft and unprofessional.

The issue is this. A modern display might have a physical resolution of 2560×1600 pixels, but the operating system reports the window as 1280×800 CSS pixels. Each CSS pixel corresponds to a 2×2 block of physical pixels. The ratio between physical pixels and CSS pixels is the device pixel ratio, and you can read it with `window.devicePixelRatio`. On a standard desktop monitor, this value is 1. On a Retina MacBook, it is 2. On some high-end phones, it can be 3 or higher. On Windows with 150% scaling, it is 1.5.

When you create a canvas element and set its `width` and `height` attributes, you are setting the size of its internal bitmap in pixels. When you set its CSS `width` and `height` properties (via `style.width` and `style.height`), you are setting how large it appears on screen. If these two sizes are the same, and the device pixel ratio is greater than 1, the browser will upscale the canvas bitmap to fill the CSS dimensions, and everything will look blurry.

The solution is to set the canvas bitmap size to `cssWidth * devicePixelRatio` by `cssHeight * devicePixelRatio`, and then set the CSS size to the original CSS dimensions. This gives the canvas enough physical pixels to render at native resolution. The browser displays it at the CSS size, and because the bitmap has the right number of physical pixels, everything looks crisp.

Here is how we implement this:

```javascript
class Editor {
  constructor(canvasEl) {
    this.canvas = canvasEl;
    this.ctx = canvasEl.getContext("2d");
    this.dpr = window.devicePixelRatio || 1;

    this._resize();
  }

  _resize() {
    const w = window.innerWidth;
    const h = window.innerHeight;
    this.canvas.width = w * this.dpr;
    this.canvas.height = h * this.dpr;
    this.canvas.style.width = w + "px";
    this.canvas.style.height = h + "px";
    this.screenW = w;
    this.screenH = h;
    this.needsRedraw = true;
  }
}
```

Let us trace through what happens on a Retina display. The browser window is 1280×800 CSS pixels, so `window.innerWidth` is 1280 and `window.innerHeight` is 800. The device pixel ratio is 2. We set `canvas.width = 1280 * 2 = 2560` and `canvas.height = 800 * 2 = 1600`. This gives us a 2560×1600 pixel bitmap — one pixel per physical pixel. We set `canvas.style.width = "1280px"` and `canvas.style.height = "800px"`, so the canvas fills the same 1280×800 CSS area. When the browser displays the canvas, it maps those 2560×1600 bitmap pixels onto the 2560×1600 physical pixels, one to one. No upscaling, no blur.

The crucial implication is that all of our drawing coordinates are now in device pixels, not CSS pixels. When we want to draw a rectangle that is 100 CSS pixels wide, we need to draw it 200 device pixels wide on a Retina display. When we compute font sizes, line heights, padding, and margins, we multiply our logical values by `this.dpr`. This is a discipline that runs through the entire codebase. Whenever you see `Math.round(28 * this.dpr)` or `Config.textPaddingLeft * this.dpr` in the code, that is the DPR conversion at work.

We also store `this.dpr` and `this.screenW`/`this.screenH` as instance properties so they are available throughout the editor. The `_resize` method is called once during construction and again whenever the window is resized. We also re-read the device pixel ratio on resize, because it can change — if you drag a window from a standard monitor to a Retina monitor, or if the user changes their system scaling.

Let us test this with a simple rectangle:

```javascript
_draw() {
  const ctx = this.ctx;
  const W = this.canvas.width;
  const H = this.canvas.height;

  // Fill the entire canvas with the background color
  ctx.fillStyle = "#2e2e32";
  ctx.fillRect(0, 0, W, H);

  // Draw a test rectangle
  ctx.fillStyle = "#93DDFA";
  ctx.fillRect(
    Math.round(50 * this.dpr),
    Math.round(50 * this.dpr),
    Math.round(200 * this.dpr),
    Math.round(40 * this.dpr)
  );
}
```

If you add this method and call it from the constructor, you should see a crisp cyan rectangle in the upper-left area of the screen, 200 CSS pixels wide and 40 CSS pixels tall, with sharp edges on any display. If you remove the DPR multiplication and just draw at `(50, 50, 200, 40)`, the rectangle will appear at half its intended size on a Retina display and will look blurry if the CSS/bitmap size mismatch is present.

Notice the `Math.round` calls. When the DPR is a non-integer like 1.5 (common on Windows with 150% scaling), multiplying by DPR can produce fractional pixel values. Drawing at fractional coordinates causes the canvas to anti-alias, which produces blurry edges. Rounding to the nearest integer keeps everything crisp. This is especially important for the thin lines and small details in a text editor — the cursor bar, the gutter separator, the menu borders. A one-pixel line drawn at position 10.5 becomes a two-pixel blurry line. The same line drawn at position 11 is sharp.

There is one more thing to handle: what happens when the device pixel ratio changes at runtime. This can happen when the user drags the browser window between monitors with different DPR values, or when the user changes their system-level zoom. We handle this in our resize event listener:

```javascript
window.addEventListener("resize", () => {
  this.dpr = window.devicePixelRatio || 1;
  this.atlas = new FontAtlas(Config.fontFamily, Config.fontSize, this.dpr);
  this._resize();
});
```

When the window is resized, we re-read the device pixel ratio. If it has changed, we also rebuild the font atlas (which we will build in Chapter 2), because the font atlas is rendered at the DPR-scaled font size. Then we call `_resize` to recompute all the layout metrics and trigger a redraw. This means that if you drag the editor window from a 1x monitor to a 2x monitor, the text will re-render at the higher resolution without any blurriness. Getting this right is one of the small details that make a canvas application feel native.


## 1.5 — The Render Loop

A text editor is not a video game. The screen does not change sixty times per second. Most of the time, the screen is completely static — the user is reading, or thinking, or has walked away from their computer. Redrawing the entire editor on every frame would waste CPU time and battery life. But when the screen does need to change — when the user types a character, moves the cursor, scrolls, or when the cursor blinks — the update needs to happen immediately, with no perceptible delay.

This is the same design challenge that *lite* faces. The *lite* editor takes an elegant approach: it redraws everything whenever it needs to redraw anything, but it only redraws when something has actually changed. On the Lua side, the code acts as if it is performing a full redraw every frame. But underneath, in the C renderer, a hash-grid-based caching system detects which regions of the screen have actually changed and only repaints those regions. This gives the simplicity of a full-redraw architecture with the performance of dirty-rectangle tracking.

We take a similar but simpler approach. We maintain a boolean flag called `needsRedraw`. When anything in the editor changes — a keystroke, a mouse click, a scroll event, a window resize — the code that handles that change sets `needsRedraw = true`. Our render loop checks this flag on every frame. If it is true, we do a full repaint. If it is false, we do nothing.

The render loop itself uses `requestAnimationFrame`, which is the browser's mechanism for scheduling work that should happen before the next screen refresh. Unlike `setInterval` or `setTimeout`, `requestAnimationFrame` automatically synchronizes with the display's refresh rate (typically 60Hz or higher), pauses when the tab is in the background (saving CPU and battery), and provides a high-resolution timestamp for timing calculations.

Here is our render loop:

```javascript
_loop(time) {
  const dt = (time - this.lastTime) / 1000;
  this.lastTime = time;

  // Update cursor blink timer
  this.cursorBlink += dt;
  if (this.cursorBlink >= Config.cursorBlinkRate * 2) {
    this.cursorBlink = 0;
  }

  // Check if blink state changed
  const blinkOn = this.cursorBlink < Config.cursorBlinkRate;
  if (this._lastBlinkOn !== blinkOn) {
    this.needsRedraw = true;
    this._lastBlinkOn = blinkOn;
  }

  // Only draw if needed
  if (this.needsRedraw) {
    this._draw();
    this.needsRedraw = false;
  }

  requestAnimationFrame((t) => this._loop(t));
}
```

The loop starts by computing the delta time `dt` — the number of seconds since the last frame. This is important for time-based animations like the cursor blink. We do not want the cursor to blink faster on a 120Hz monitor than on a 60Hz monitor, so we use real elapsed time rather than frame count.

The cursor blink timer is a simple accumulator. It counts up from zero. When it is less than `Config.cursorBlinkRate` (0.5 seconds), the cursor is visible. When it is between `cursorBlinkRate` and `cursorBlinkRate * 2`, the cursor is hidden. When it reaches `cursorBlinkRate * 2` (one full second), it resets to zero and the cycle repeats. Each time the blink state transitions (from on to off, or off to on), we set `needsRedraw = true` to trigger a repaint.

This is the only time-based state change in the entire editor. Everything else is driven by input events. When the user types, scrolls, clicks, or resizes the window, the event handler updates the editor state and sets `needsRedraw = true`. The next time the loop runs (within one frame — usually 16.6ms on a 60Hz display), it sees the flag, performs the repaint, and clears the flag.

The `_draw` method is where all the actual rendering happens. For now, it is just filling the canvas with the background color. As we build out the editor in subsequent chapters, it will grow to include the gutter, the text area, the menu bar, the status bar, the scrollbar, the cursor, selections, the drop overlay, and dropdown menus. But the loop itself will never change. This is a pattern worth internalizing: the loop is the heartbeat of the application, and it should be as simple and stable as possible. All the complexity lives in the draw and update methods, not in the loop itself.

One subtle design decision is worth noting. We do not reset the cursor blink timer to zero on every keystroke inside the loop — that happens in the event handlers themselves. When the user types a character or moves the cursor, the event handler sets `this.cursorBlink = 0`, which makes the cursor immediately visible and restarts the blink cycle. This is standard editor behavior: the cursor should always be visible during active editing, and it should only start blinking after a pause.

We start the loop in the constructor with `this._loop(performance.now())`. The `performance.now()` call gives us an initial timestamp so that the first frame's delta time calculation is valid. From there, `requestAnimationFrame` takes over and calls `_loop` on every frame for the lifetime of the page.


## 1.6 — Configuration and Theming

Before we write any more rendering code, we need to establish where the editor's configurable values live. Font family, font size, line height, tab width, gutter padding, cursor blink rate, undo merge timeout — these are all values that affect the editor's behavior and appearance, and they should be easy to find, easy to change, and separated from the logic that uses them.

We use two plain objects: `Config` for behavioral settings and `Theme` for colors.

```javascript
const Config = {
  fps: 60,
  cursorBlinkRate: 0.5,
  scrollSpeed: 3,
  fontFamily: "Consolas, 'Courier New', monospace",
  fontSize: 15,
  lineHeight: 1.5,
  gutterPaddingLeft: 10,
  gutterPaddingRight: 15,
  textPaddingLeft: 8,
  tabSize: 4,
  undoMergeTimeout: 300,
};
```

Every number in `Config` is in CSS pixels (logical pixels), not device pixels. The DPR scaling happens at the point of use, not in the configuration. This means the configuration values are easy to reason about — `fontSize: 15` means fifteen-point text, regardless of whether the display is 1x or 2x. The conversion to device pixels happens when we create the font atlas, compute layout metrics, or draw to the canvas.

The `fontFamily` is a CSS font stack. We start with Consolas, which is a monospace font available on Windows and macOS. If Consolas is not available, we fall back to Courier New, and finally to the browser's default monospace font. We use a monospace font because text editors depend on characters being a uniform width. Monospace fonts make column alignment trivial — the x-coordinate of the nth character on any line is always `n * charWidth`. With a proportional font, you would need to measure the width of every character individually, which is more complex and slower.

The `lineHeight: 1.5` is a multiplier on the font size. A 15px font with a 1.5 line height produces lines that are 22.5 CSS pixels tall (rounded to the nearest device pixel). This gives a comfortable amount of vertical spacing between lines — enough to read easily without feeling wasteful. Many code editors use a line height between 1.3 and 1.6.

The `tabSize: 4` determines how many spaces are inserted when the user presses Tab. Some programmers prefer 2, some prefer 8. We use 4 as a sensible default. The `undoMergeTimeout: 300` is in milliseconds — if two consecutive edits happen less than 300ms apart, they may be merged into a single undo entry. We will explore this in detail in Chapter 4.

Now for the theme:

```javascript
const Theme = {
  background:       "#2e2e32",
  gutterBg:         "#2e2e32",
  gutterText:       "#5a5a65",
  gutterActiveLine: "#70707a",
  lineHighlight:    "#34343a",
  caret:            "#93DDFA",
  selection:        "#48505880",
  text:             "#c5c8c6",
  menuBg:           "#252529",
  menuBorder:       "#404046",
  menuText:         "#c5c8c6",
  menuHover:        "#3a3a42",
  menuTextHover:    "#ffffff",
  dropdownBg:       "#303035",
  dropdownBorder:   "#505058",
  dropdownHover:    "#45454f",
  statusBg:         "#1d1d21",
  statusText:       "#808088",
  scrollbar:        "#50505a",
  scrollbarHover:   "#70707a",
  keyword:          "#E8BF6A",
  keyword2:         "#d197d9",
  literal:          "#97c279",
  string:           "#97c279",
  comment:          "#676b6f",
  number:           "#D19A66",
  symbol:           "#61AFEF",
  operator:         "#56B6C2",
  func:             "#61AFEF",
  normal:           "#c5c8c6",
};
```

The `Theme` object contains every color the editor uses, organized into three groups. The first group covers the editor's structural elements: the background, gutter, line highlight, cursor, selection, and default text color. The second group covers the UI chrome: the menu bar, dropdown menus, status bar, and scrollbar. The third group covers syntax highlighting token colors: keywords, strings, comments, numbers, operators, and so on.

Every color is a CSS hex string. Some include an alpha channel — the selection color `"#48505880"` has `80` as its alpha byte, making it semi-transparent. This lets the selection overlay blend with the text and background underneath, rather than hiding it completely. This is the same approach that *lite* and most modern code editors take for selection rendering.

The specific color values are inspired by *lite*'s default theme, but adjusted for our editor. The background is a neutral dark gray — not pure black, which is harsh on the eyes, but dark enough to provide good contrast with the syntax colors. The text color `"#c5c8c6"` is a warm light gray, easier on the eyes than pure white. The syntax colors follow a common pattern: warm yellows and oranges for keywords and numbers, greens for strings and literals, blues for symbols and functions, a muted gray for comments, and cyan for operators. This produces a balanced, readable color scheme where different syntactic elements are easy to distinguish without being garish.

The decision to use a flat object rather than a nested structure (like `Theme.syntax.keyword` or `Theme.ui.menu.background`) is deliberate. A flat object is simpler to work with — you look up a color with `Theme.keyword` instead of navigating a hierarchy. It is also simpler to redefine — if you wanted to change the theme, you would just assign new values to the properties you want to change, without worrying about deep merging.

Separating configuration from code like this has a practical benefit that goes beyond readability. If you later decide to add a theme picker, or to load themes from a JSON file, or to let the user customize their colors, you only need to replace the values in the `Theme` object. The rest of the code just references `Theme.keyword` or `Theme.background` — it does not care where the color values came from. Similarly, if you want to add a settings panel where the user can change the font size or tab width, you modify `Config.fontSize` or `Config.tabSize` and rebuild the relevant parts of the editor. The configuration objects are the seams where customization can enter the system.

*Lite* takes a similar approach with its configuration and theme files. Color themes are Lua files that set values in a theme table, and the user module can override any configuration value. By keeping our own configuration in plain JavaScript objects at the top of the file, we are following the same pattern: everything that is configurable is visible, accessible, and separate from the code that uses it.


## 1.7 — The Editor Class Skeleton

With the HTML skeleton, the DPR-aware canvas, the render loop, and the configuration objects in place, we can now sketch out the overall structure of the `Editor` class. This class is the central controller of the entire application. It owns the canvas, the rendering context, the font atlas, the document, the menu, and all of the editor's state. Every input event is routed through it, and every frame is drawn by it.

Here is the initial skeleton, with the pieces we have built so far and placeholders for what comes later:

```javascript
class Editor {
  constructor(canvasEl) {
    this.canvas = canvasEl;
    this.ctx = canvasEl.getContext("2d");
    this.dpr = window.devicePixelRatio || 1;

    // Font atlas (Chapter 2)
    // this.atlas = new FontAtlas(Config.fontFamily, Config.fontSize, this.dpr);

    // Document (Chapter 3)
    // this.doc = new Doc("", "untitled");

    // Scroll state
    this.scrollY = 0;
    this.scrollX = 0;

    // Cursor blink
    this.cursorBlink = 0;
    this.lastTime = 0;
    this.needsRedraw = true;
    this._lastBlinkOn = true;

    // Mouse state
    this.mouseX = 0;
    this.mouseY = 0;
    this.mouseDown = false;

    // Layout metrics (computed on resize)
    this.menuBarH = 0;
    this.statusBarH = 0;
    this.gutterW = 0;
    this.textAreaX = 0;
    this.textAreaY = 0;
    this.textAreaW = 0;
    this.textAreaH = 0;
    this.lineH = 0;
    this.charW = 0;

    this._resize();
    this._bindEvents();
    this._loop(performance.now());
  }

  _resize() {
    const w = window.innerWidth;
    const h = window.innerHeight;
    this.canvas.width = w * this.dpr;
    this.canvas.height = h * this.dpr;
    this.canvas.style.width = w + "px";
    this.canvas.style.height = h + "px";
    this.screenW = w;
    this.screenH = h;

    this.menuBarH = Math.round(28 * this.dpr);
    this.statusBarH = Math.round(24 * this.dpr);

    // textArea fills the remaining space
    this.textAreaY = this.menuBarH;
    this.textAreaH = this.canvas.height - this.menuBarH - this.statusBarH;

    this.needsRedraw = true;
  }

  _bindEvents() {
    window.addEventListener("resize", () => {
      this.dpr = window.devicePixelRatio || 1;
      this._resize();
    });
  }

  _loop(time) {
    const dt = (time - this.lastTime) / 1000;
    this.lastTime = time;
    this.cursorBlink += dt;
    if (this.cursorBlink >= Config.cursorBlinkRate * 2) this.cursorBlink = 0;

    const blinkOn = this.cursorBlink < Config.cursorBlinkRate;
    if (this._lastBlinkOn !== blinkOn) {
      this.needsRedraw = true;
      this._lastBlinkOn = blinkOn;
    }

    if (this.needsRedraw) {
      this._draw();
      this.needsRedraw = false;
    }
    requestAnimationFrame((t) => this._loop(t));
  }

  _draw() {
    const ctx = this.ctx;
    const W = this.canvas.width;
    const H = this.canvas.height;

    // Background
    ctx.fillStyle = Theme.background;
    ctx.fillRect(0, 0, W, H);

    // Menu bar
    ctx.fillStyle = Theme.menuBg;
    ctx.fillRect(0, 0, W, this.menuBarH);
    ctx.fillStyle = Theme.menuBorder;
    ctx.fillRect(0, this.menuBarH - this.dpr, W, this.dpr);

    // Status bar
    ctx.fillStyle = Theme.statusBg;
    ctx.fillRect(0, H - this.statusBarH, W, this.statusBarH);
  }
}

// Boot
const editor = new Editor(document.getElementById("canvas"));
```

If you put this in the script block of your HTML file (along with the `Config` and `Theme` objects), you should see a dark canvas with a slightly darker strip at the top (the menu bar) and an even darker strip at the bottom (the status bar). The middle area is the text area background. Nothing is interactive yet, but the visual structure is already taking shape.

Notice the layout metric properties: `menuBarH`, `statusBarH`, `gutterW`, `textAreaX`, `textAreaY`, `textAreaW`, `textAreaH`, `lineH`, `charW`. These are all zero-initialized in the constructor and computed in `_resize`. Every part of the drawing code references these properties rather than computing positions from scratch. This means that when we change the font size, or the window is resized, or the DPR changes, we only need to recompute these metrics once, and all the drawing code automatically adapts.

The `gutterW`, `lineH`, and `charW` properties are not yet computed here because they depend on the font atlas, which we will build in Chapter 2. Once we have the atlas, we will know the exact pixel width and height of a character, and we can compute the gutter width from the number of digits in the line count. For now, the layout is simplified — no gutter, no text. But the structure is correct, and it is ready to receive the pieces we will add in subsequent chapters.

The boot sequence at the bottom is as simple as it gets: we get a reference to the canvas element and pass it to the Editor constructor. The constructor sets up the canvas, computes the layout, binds the events, and starts the render loop. The editor is alive.

There is a design principle worth articulating here, because it will guide every decision in the rest of this book. The Editor class is not an event-driven architecture with callbacks and event emitters. It is a poll-and-draw architecture. Input events update state, and the render loop checks the state and draws. There are no callbacks registered by components. There are no event listeners that one part of the editor registers with another part. There are no lifecycle hooks.

This is the same approach that *lite* takes, and the original author's reasoning is worth understanding. In avoiding event listeners, *lite* also avoids having to manage unregistering event handlers or worry about handlers that are never unregistered through error. This leads to simpler, less error-prone code. When a part of the editor needs to know if something has changed, it checks a property on the state object. When a change happens, the code that makes the change sets `needsRedraw = true`. The next time through the loop, the screen updates. There is no possibility of a forgotten listener firing at the wrong time, no possibility of an event handler being called after its context has been destroyed, and no possibility of event ordering bugs. The state is always consistent because there is a single moment — the draw call — when the state is observed and rendered.

This architecture scales well for an application of our size. For a very large application with hundreds of independent components, a more structured event system might be warranted. But for a text editor — a single-document, single-window application — the poll-and-draw pattern is exactly right. It is what *lite* uses, it is what most games use, and it will serve us well.


## 1.8 — What We Have, and What Comes Next

Let us take stock. In this chapter, we have:

1. Established why building a text editor from scratch is worthwhile, and why the philosophy of *lite* — simplicity over abstraction — is the right guide.
2. Chosen the canvas as our rendering surface, understanding the trade-offs compared to DOM-based approaches.
3. Created the HTML skeleton: a canvas element, a hidden file input, and a CSS reset.
4. Solved the device pixel ratio problem, ensuring crisp rendering on any display.
5. Built a render loop with `requestAnimationFrame`, a `needsRedraw` flag for lazy rendering, and a cursor blink timer.
6. Defined `Config` and `Theme` objects to separate configurable values from logic.
7. Sketched the Editor class with its layout computation, event binding, and draw method.

The result is a blank canvas that fills the browser window, with a menu bar region at the top and a status bar region at the bottom, all rendering at native resolution. It is not much to look at yet. But the foundation is solid. Every design decision we have made — DPR-aware sizing, lazy redraw, poll-and-draw architecture, flat configuration objects, device-pixel arithmetic — will carry through the entire rest of the project. We will not revisit any of these decisions. They are done.

In Chapter 2, we will give the editor the ability to render text. Not with `fillText` — that would be too slow for an editor that might display thousands of characters on screen. Instead, we will build a font atlas: an offscreen canvas containing a pre-rendered grid of glyphs that can be stamped onto the main canvas at any position, in any color, with a single `drawImage` call per color run. This is the technique that game engines have used for decades, and it is what will make our editor fast enough to feel instant.

Turn the page, and let us make this canvas speak.

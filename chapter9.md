# Chapter 9: The Menu System

*Building a top menu bar with dropdown menus, hover states, keyboard navigation, scrollable dropdowns, and check indicators.*

---

## 9.1 — Menu Data Structure

A menu system needs to answer several questions at once. What items are available? How are they grouped? Which ones have keyboard shortcuts? Which ones have state (like a checkbox)? And how can the menu adapt when the editor's state changes — for example, listing all available syntaxes even though new syntaxes could be added?

We answer all of these with a simple data structure: an array of top-level menu items, each containing an array of child items.

```javascript
_createMenu() {
  return [
    { label: "File", children: [
      { label: "New", action: "new", shortcut: "Ctrl+N" },
      { label: "Open...", action: "open", shortcut: "Ctrl+O" },
      { label: "Save", action: "save", shortcut: "Ctrl+S" },
      { type: "separator" },
      { label: "Close", action: "close" },
    ]},
    { label: "Edit", children: [
      { label: "Undo", action: "undo", shortcut: "Ctrl+Z" },
      { label: "Redo", action: "redo", shortcut: "Ctrl+Y" },
      { type: "separator" },
      { label: "Cut", action: "cut", shortcut: "Ctrl+X" },
      { label: "Copy", action: "copy", shortcut: "Ctrl+C" },
      { label: "Paste", action: "paste", shortcut: "Ctrl+V" },
      { type: "separator" },
      { label: "Select All", action: "selectAll", shortcut: "Ctrl+A" },
      { label: "Duplicate Line", action: "duplicateLine",
        shortcut: "Ctrl+D" },
    ]},
    { label: "View", children: [
      { label: "Zoom In", action: "zoomIn", shortcut: "Ctrl+=" },
      { label: "Zoom Out", action: "zoomOut", shortcut: "Ctrl+-" },
      { label: "Reset Zoom", action: "zoomReset" },
      { type: "separator" },
      { label: "Syntax Highlighting", action: "toggleHighlight",
        checked: () => this.highlightEnabled },
      { type: "separator" },
      ...this._buildSyntaxMenuItems(),
    ]},
  ];
}
```

Each top-level item has a `label` (displayed in the menu bar) and a `children` array (the dropdown items). Each child item can be one of three types:

A **command item** has a `label`, an `action` (a string identifier), and an optional `shortcut` (displayed right-aligned in the dropdown). The shortcut string is purely informational — it tells the user what keyboard shortcut performs the same action, but it does not define the shortcut. The actual keyboard handling happens in `_onKeyDown`, completely independent of the menu.

A **separator** has `type: "separator"` and nothing else. It renders as a thin horizontal line that visually groups related items — file operations together, clipboard operations together, zoom controls together.

A **checked item** has all the properties of a command item plus a `checked` function that returns a boolean. When `checked()` returns true, a bullet indicator is drawn next to the item. This is used for the "Syntax Highlighting" toggle (checked when highlighting is enabled) and for the syntax list (checked for the currently active syntax).

The `checked` property is a function, not a static boolean, because the checked state depends on the editor's current state. The syntax highlighting toggle needs to read `this.highlightEnabled` at draw time, and each syntax item needs to compare `this.doc.syntax` with its own syntax reference. By using functions, the menu items automatically reflect the current state every time they are drawn — no update mechanism needed.

The View menu demonstrates the most interesting structural feature: dynamic content via `_buildSyntaxMenuItems`:

```javascript
_buildSyntaxMenuItems() {
  const items = [];
  for (let i = 0; i < Syntaxes.length; i++) {
    const syn = Syntaxes[i];
    items.push({
      label: syn.name,
      action: "syntax_" + i,
      checked: () => this.doc.syntax === syn,
    });
  }
  return items;
}
```

This method generates a menu item for each registered syntax definition. The `action` is a string like `"syntax_0"`, `"syntax_1"`, etc. — a convention that the action dispatcher will parse to extract the index. The `checked` function captures the syntax object in a closure and compares it to the document's current syntax. The spread operator (`...this._buildSyntaxMenuItems()`) inlines these items into the View menu's children array, so they appear as regular menu items alongside the zoom controls and the highlight toggle.

This pattern — static structure with dynamic data merged in at construction time — is simple but effective. If we added a new syntax definition to the `Syntaxes` array, it would automatically appear in the View menu the next time the editor is created. The menu does not need to be told about new syntaxes; it discovers them by reading the array.

The decision to use strings for action identifiers (rather than function references) deserves explanation. We could have defined each menu item with a direct callback:

```javascript
{ label: "Undo", action: () => this.doc.undo() }
```

But strings have advantages. They are serializable — you could save and load menu configurations as JSON. They are inspectable — a debugger can print `"undo"` more informatively than `[Function]`. They create a single dispatch point (`_executeMenuAction`) where all menu-triggered behavior is visible in one place. And they decouple the menu data structure from the methods it invokes — the menu does not need to know that `this.doc.undo()` exists; it just sends the string `"undo"` to the dispatcher, and the dispatcher routes it.

The string-based approach also handles the dynamic syntax actions naturally. The `_buildSyntaxMenuItems` method generates action strings like `"syntax_0"`, `"syntax_1"`, etc. without needing to create closures that capture the syntax index. The dispatcher parses the string to extract the index:

```javascript
if (action && action.startsWith("syntax_")) {
  const idx = parseInt(action.substring(7));
  this._setSyntax(idx);
}
```

This is a convention — a simple protocol between the menu item definition and the action dispatcher. It is not type-safe or enforced by the language, but it is easy to understand and easy to extend.

The menu is created once during the editor's construction. It is not rebuilt when the document changes or when the syntax changes — the `checked` functions handle the dynamic state. This means the menu array is stable for the lifetime of the editor, which simplifies memory management and avoids the overhead of recreating the data structure on every state change.


## 9.2 — Drawing the Menu Bar

The menu bar is drawn as a horizontal strip across the top of the canvas, with each top-level menu label positioned side by side:

```javascript
_drawMenuBar(ctx) {
  const W = this.canvas.width;
  const h = this.menuBarH;

  ctx.fillStyle = Theme.menuBg;
  ctx.fillRect(0, 0, W, h);
  ctx.fillStyle = Theme.menuBorder;
  ctx.fillRect(0, h - this.dpr, W, this.dpr);

  let x = Math.round(12 * this.dpr);
  const textY = Math.round((h - this.atlas.charHeight) / 2);

  for (let i = 0; i < this.menu.length; i++) {
    const item = this.menu[i];
    const itemW = this._getMenuItemW(i);

    const isActive = this.activeMenu === i;
    const isHover = this.mouseX >= x && this.mouseX < x + itemW
      && this.mouseY >= 0 && this.mouseY < h;

    if (isActive || isHover) {
      ctx.fillStyle = Theme.menuHover;
      ctx.fillRect(x, 0, itemW, h);
    }

    const color = (isActive || isHover)
      ? Theme.menuTextHover : Theme.menuText;
    this.atlas._drawTintedRun(ctx, item.label,
      x + Math.round(8 * this.dpr), textY, color);
    x += itemW;
  }
}
```

The background fills the full width of the canvas. A one-pixel border line marks the bottom edge, separating the menu bar from the text area below.

Each menu label is positioned with a running x-offset. The width of each label is computed by `_getMenuItemW`:

```javascript
_getMenuItemW(idx) {
  return this.menu[idx].label.length * this.charW
    + Math.round(16 * this.dpr);
}
```

The width is the label text's pixel width (characters times character width) plus 16 CSS pixels of horizontal padding (8 on each side). The `_getMenuItemX` method computes the x-position of a specific menu item by summing the widths of all preceding items:

```javascript
_getMenuItemX(idx) {
  let x = Math.round(12 * this.dpr);
  for (let i = 0; i < idx; i++) {
    x += this.menu[i].label.length * this.charW
      + Math.round(16 * this.dpr);
  }
  return x;
}
```

The initial offset of 12 CSS pixels provides left padding before the first menu label.

Hover detection is done during drawing by comparing the stored mouse position (`this.mouseX`, `this.mouseY`) against each item's bounds. If the mouse is within an item's rectangle, the item gets a highlighted background (`Theme.menuHover`) and brighter text (`Theme.menuTextHover`). This is a polling approach to hover detection — rather than tracking `mouseenter` and `mouseleave` events (which do not exist for canvas-drawn elements), we check the mouse position on every draw and highlight accordingly. Since we already store the mouse position on every `mousemove` event, and we redraw whenever the mouse moves while a menu is open, the hover highlighting is responsive and correct.

The `isActive` check highlights the menu label that has an open dropdown. This tells the user which menu they are currently browsing. Both `isActive` and `isHover` produce the same visual treatment — a highlighted background and brighter text — which is intentional. The user should not need to distinguish between "my mouse is over this label" and "this label's dropdown is open"; both indicate that the label is the current focus.

This polling approach to hover has an important implication: the menu bar only shows hover highlights when it is being redrawn. During normal editing, mouse movement over the menu bar does not trigger a redraw (because `needsRedraw` is not set by mouse movement when no menu is open). This means the menu bar labels do not highlight when the user moves the mouse over them unless a dropdown is already open. This is actually the correct behavior for most native menu bars — they only show hover states when the menu is in an "active" state (a dropdown is open). However, some applications do show hover states even when no dropdown is open. We could implement this by setting `needsRedraw = true` in `_onMouseMove` when the mouse is within the menu bar region, but we choose not to — it would cause unnecessary redraws during normal editing.

When a dropdown is open, `_onMouseMove` does set `needsRedraw = true` unconditionally when `activeMenu >= 0`. This ensures that hover highlights within the dropdown and on the menu bar labels are updated in real time as the mouse moves.


## 9.3 — Drawing the Dropdown

The dropdown is the most complex drawing method in the entire editor. It renders a floating panel below the active menu label, containing command items, separators, check indicators, shortcut labels, and scroll indicators — all within a clipped, scrollable region.

The dropdown's position and size are computed first:

```javascript
const dropX = this._getMenuItemX(mi);
const dropY = this.menuBarH;
const dropW = Math.round(220 * this.dpr);
const itemH = Math.round(26 * this.dpr);
const sepH = Math.round(8 * this.dpr);
```

The dropdown's left edge aligns with the left edge of the menu label that opened it. Its top edge is the bottom of the menu bar. The width is a fixed 220 CSS pixels — wide enough to accommodate most menu labels and shortcuts side by side. Each item is 26 CSS pixels tall. Separators are 8 CSS pixels tall.

The total height is computed by summing the heights of all children. If this exceeds the available viewport space, the dropdown is clamped to fit:

```javascript
let totalH = 0;
for (let i = 0; i < children.length; i++) {
  totalH += children[i].type === "separator" ? sepH : itemH;
}

const maxH = this.canvas.height - dropY
  - Math.round(8 * this.dpr);
const clampedH = Math.min(totalH, maxH);
const needsScroll = totalH > maxH;
```

The `maxH` is the canvas height minus the dropdown's top edge minus a small bottom margin (8 CSS pixels). This ensures the dropdown never extends past the bottom of the screen. When `totalH > maxH`, the dropdown enters scrollable mode — its content is taller than its visible area, and the user can scroll within it using the mouse wheel or keyboard navigation.

The dropdown is drawn in layers. First, a shadow — a dark semi-transparent rectangle offset 3 pixels down and right, giving the dropdown a floating appearance:

```javascript
ctx.fillStyle = "rgba(0,0,0,0.35)";
ctx.fillRect(dropX + 3 * this.dpr, dropY + 3 * this.dpr,
  dropW, clampedH);
```

Then the background — a solid rectangle in `Theme.dropdownBg` — and a border in `Theme.dropdownBorder`:

```javascript
ctx.fillStyle = Theme.dropdownBg;
ctx.fillRect(dropX, dropY, dropW, clampedH);
ctx.strokeStyle = Theme.dropdownBorder;
ctx.lineWidth = this.dpr;
ctx.strokeRect(dropX + 0.5, dropY + 0.5, dropW - 1, clampedH - 1);
```

The `strokeRect` uses half-pixel offsets (`+ 0.5`) to produce a crisp one-pixel border. Without the offset, the stroke would straddle the pixel boundary, producing a two-pixel-wide blurry line. This is a standard canvas technique for sharp single-pixel lines.

The shadow, background, and border together create a convincing "floating panel" appearance. The shadow gives the dropdown depth — it appears to hover above the editor content below it. The background covers whatever was underneath (code, gutter, status bar), and the border provides a clean edge. These three layers are a minimal but effective implementation of a floating UI element, achieving with three `fillRect`/`strokeRect` calls what a CSS-based dropdown would achieve with `box-shadow`, `background-color`, and `border` properties.

One visual detail worth noting: the dropdown background color (`Theme.dropdownBg`, `"#303035"`) is slightly lighter than the menu bar background (`Theme.menuBg`, `"#252529"`), which creates a subtle visual hierarchy. The menu bar is the darkest element, the dropdown is slightly lighter, and the hover highlight within the dropdown is lighter still. This graduated brightness draws the eye to the interactive element — the hovered item — while keeping the surrounding chrome visually recessive.

Then the content is drawn inside a clipping region:

```javascript
ctx.save();
ctx.beginPath();
ctx.rect(dropX, dropY, dropW, clampedH);
ctx.clip();
```

The clip ensures that items scrolled outside the visible area are not drawn. The y-position of each item starts at `dropY - this._dropdownScrollY` — when the scroll offset is positive, items shift upward, and items at the top scroll out of view while items at the bottom scroll into view.

Each item is drawn based on its type. **Separators** are a thin horizontal line centered vertically within the separator's height:

```javascript
ctx.fillStyle = Theme.dropdownBorder;
ctx.fillRect(dropX + 8 * this.dpr, y + sepH / 2,
  dropW - 16 * this.dpr, this.dpr);
```

**Command items** have up to four visual components: a hover highlight, a check indicator, a label, and a shortcut.

The hover highlight is drawn when the mouse is over the item or when the keyboard selection (`menuHoverItem`) points to it:

```javascript
const isHover = this.mouseX >= dropX
  && this.mouseX < dropX + dropW
  && this.mouseY >= y && this.mouseY < y + itemH
  && this.mouseY >= dropY && this.mouseY < dropY + clampedH;
const isKeyHover = this.menuHoverItem === i;

if (isHover || isKeyHover) {
  ctx.fillStyle = Theme.dropdownHover;
  ctx.fillRect(dropX + 1, y, dropW - 2, itemH);
}
```

The mouse hover check includes an additional constraint: `this.mouseY >= dropY && this.mouseY < dropY + clampedH`. This prevents items that have scrolled outside the visible region from responding to hover. Without this check, an item that is technically at a y-position above `dropY` (because it has scrolled out of view) could still highlight if the mouse happened to be at that y-position in another part of the canvas.

The **check indicator** is a bullet character (`\u2022`, "•") drawn in the caret color when the item's `checked` function returns true:

```javascript
const hasCheck = typeof child.checked === "function";
if (hasCheck) {
  const isChecked = child.checked();
  if (isChecked) {
    this.atlas._drawTintedRun(ctx, "\u2022",
      dropX + checkCol, textY, Theme.caret);
  }
  this.atlas._drawTintedRun(ctx, child.label,
    dropX + labelCol, textY, color);
} else {
  this.atlas._drawTintedRun(ctx, child.label,
    dropX + Math.round(12 * this.dpr), textY, color);
}
```

Items with a `checked` function have their label indented further right (`labelCol = 26 * dpr` instead of `12 * dpr`) to make room for the bullet. The bullet is drawn at `checkCol = 12 * dpr`, in the gap between the left edge and the label. Items without a check function use the normal left padding.

The bullet character is drawn using the font atlas's `_drawTintedRun`, which routes it through the non-ASCII fallback path (since `\u2022` is outside the ASCII 32–126 range). This means it is rendered with `fillText` at draw time rather than stamped from the atlas, but since there are at most a few bullets on screen at any time, the performance difference is negligible.

The **shortcut text** is right-aligned within the item:

```javascript
if (child.shortcut) {
  const scW = child.shortcut.length * this.charW;
  this.atlas._drawTintedRun(ctx, child.shortcut,
    dropX + dropW - scW - Math.round(12 * this.dpr),
    textY, Theme.gutterText);
}
```

The shortcut is drawn in `Theme.gutterText`, a muted gray that is visible but subordinate to the label text. This dimmer color signals that the shortcut is secondary information — the user's attention should be on the item label, not the shortcut.

After all items are drawn, the clip is restored:

```javascript
ctx.restore();
```

If the dropdown is scrollable, scroll indicators are drawn on top of the restored (unclipped) canvas. These are small arrow characters (`^` and `v`) centered at the top and bottom of the dropdown, drawn over a small background rectangle that covers the partially-visible items beneath them:

```javascript
if (needsScroll) {
  if (this._dropdownScrollY > 0) {
    ctx.fillStyle = Theme.dropdownBg;
    ctx.fillRect(dropX + 1, dropY, dropW - 2,
      Math.round(12 * this.dpr));
    this.atlas._drawTintedRun(ctx, "^",
      dropX + dropW / 2 - this.charW / 2,
      dropY, Theme.gutterText);
  }
  if (this._dropdownScrollY < totalH - clampedH) {
    const bottomY = dropY + clampedH
      - Math.round(12 * this.dpr);
    ctx.fillStyle = Theme.dropdownBg;
    ctx.fillRect(dropX + 1, bottomY, dropW - 2,
      Math.round(12 * this.dpr));
    this.atlas._drawTintedRun(ctx, "v",
      dropX + dropW / 2 - this.charW / 2,
      bottomY, Theme.gutterText);
  }
}
```

The top indicator appears only when there are items scrolled above the visible area (`_dropdownScrollY > 0`). The bottom indicator appears only when there are items below the visible area. These indicators serve a dual purpose: they tell the user that scrolling is possible, and they cover the partially-visible items at the edges to prevent visual clutter.

The indicators are drawn *after* `ctx.restore()` — outside the clipping region — because they need to cover the clipped content at the edges. If they were drawn inside the clip, they would be part of the scrollable content and would scroll along with it, which would defeat their purpose.

The choice to use simple text characters (`^` and `v`) rather than graphical arrows is consistent with our approach throughout the editor: we use the font atlas for everything, avoiding custom graphics or icon fonts. The characters are rendered in `Theme.gutterText`, the same muted color used for secondary information elsewhere, making them visible but unobtrusive.

The dropdown scroll can be controlled in two ways: the mouse wheel (handled in `_onWheel`, which adjusts `_dropdownScrollY` when a dropdown is open) and the keyboard (handled by `_scrollDropdownToItem`, which auto-scrolls when Arrow Down or Arrow Up moves the selection past the visible bounds). Both mechanisms modify the same `_dropdownScrollY` property, so they interoperate seamlessly — the user can scroll with the wheel and then continue navigating with the keyboard from the scrolled position.


## 9.4 — Click Handling

The menu system has three click targets: the menu bar labels, the dropdown items, and everything else. Clicking a label toggles the dropdown. Clicking an item executes the action. Clicking outside closes the dropdown.

The **menu bar click** handler iterates through the labels and checks which one was clicked:

```javascript
_handleMenuBarClick(px) {
  let x = Math.round(12 * this.dpr);
  for (let i = 0; i < this.menu.length; i++) {
    const w = this._getMenuItemW(i);
    if (px >= x && px < x + w) {
      this.activeMenu = (this.activeMenu === i) ? -1 : i;
      this.menuHoverItem = -1;
      this._dropdownScrollY = 0;
      this.needsRedraw = true;
      return;
    }
    x += w;
  }
  this.activeMenu = -1;
  this.needsRedraw = true;
}
```

The toggle logic — `(this.activeMenu === i) ? -1 : i` — means clicking the same label again closes the dropdown, and clicking a different label opens its dropdown instead. The `_dropdownScrollY` is reset to 0 so each dropdown opens scrolled to the top. The `menuHoverItem` is reset to -1 (no item highlighted) so the dropdown opens without a pre-selected item.

The **dropdown click** handler is more complex because it must account for scroll offset and the clamped height:

```javascript
_handleDropdownClick(px, py) {
  // ... compute dropX, dropY, dropW, itemH, sepH, totalH,
  //     maxH, clampedH ...

  if (px < dropX || px >= dropX + dropW
      || py < dropY || py >= dropY + clampedH) {
    return false;
  }

  const scrollOff = this._dropdownScrollY || 0;
  let y = dropY - scrollOff;
  for (let i = 0; i < children.length; i++) {
    const h = children[i].type === "separator" ? sepH : itemH;
    if (py >= y && py < y + h
        && py >= dropY && py < dropY + clampedH
        && children[i].type !== "separator") {
      this._executeMenuAction(children[i].action);
      this.activeMenu = -1;
      this.menuHoverItem = -1;
      this.needsRedraw = true;
      return true;
    }
    y += h;
  }
  return true;
}
```

The method first checks if the click is outside the dropdown bounds — if so, it returns `false`, telling the caller that the click was not handled by the dropdown and should be processed as a regular click (which will close the dropdown). If the click is inside the dropdown, it iterates through the items (with the scroll offset applied to the y-positions) to find the clicked item. Separators are skipped. The additional `py >= dropY && py < dropY + clampedH` check ensures that items scrolled out of view cannot be clicked.

The return value convention is important. `true` means "I handled this click; do not process it further." `false` means "this click is not mine; handle it elsewhere." The `_onMouseDown` handler uses this to decide what to do:

```javascript
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
```

If the dropdown handled the click, we are done. If it returned false, we check if the click was on the menu bar (which might open a different dropdown). If it was not on the menu bar either, the click was outside the entire menu system — we close the dropdown and let the click fall through to normal processing on the next frame.

Let us trace through a complete menu interaction to see how the pieces fit together:

1. **User clicks on "Edit" label in the menu bar.** `_onMouseDown` fires. No dropdown is open (`activeMenu === -1`), so the dropdown check is skipped. The click is above `menuBarH`, so `_handleMenuBarClick` is called. It iterates the labels, finds that the click falls within "Edit", and sets `activeMenu = 1`. `needsRedraw = true`.

2. **Next frame renders.** `_draw` is called. `_drawMenuBar` draws "Edit" with the active highlight. Because `activeMenu >= 0`, `_drawDropdown` runs and draws the Edit dropdown with Undo, Redo, separator, Cut, Copy, Paste, separator, Select All, Duplicate Line.

3. **User moves the mouse down over "Copy".** `_onMouseMove` fires. `activeMenu >= 0`, so `needsRedraw = true`. On the next frame, `_drawDropdown` detects the mouse hover over the Copy item and draws it with the hover highlight.

4. **User clicks on "Copy".** `_onMouseDown` fires. `activeMenu >= 0`, so `_handleDropdownClick` is called. It computes the item geometry with the scroll offset, finds that the click is on the Copy item, calls `_executeMenuAction("copy")`, sets `activeMenu = -1`, and returns `true`. The dispatcher copies the selected text to the clipboard. `needsRedraw = true`.

5. **Next frame renders.** `activeMenu === -1`, so no dropdown is drawn. The menu bar returns to its normal appearance. The text content is unchanged but the clipboard now contains the selected text.

The entire interaction — open, hover, click, close — is handled by the interplay of three methods (`_handleMenuBarClick`, `_handleDropdownClick`, `_drawDropdown`) and three state variables (`activeMenu`, `menuHoverItem`, `mouseX`/`mouseY`). There are no event listeners on menu items, no event bubbling, no focus management. The drawing code performs hit-testing during the draw pass, and the click handler performs hit-testing during the click. Both use the same geometry calculations, so they are always consistent.


## 9.5 — Keyboard Navigation

When a dropdown is open, the keyboard enters "menu mode." Arrow keys navigate the items, Enter selects, Left/Right switch between menus, and Escape closes the dropdown. This keyboard navigation is handled by `_handleMenuKeyDown`, which is called from `_onKeyDown` when `this.activeMenu >= 0`:

```javascript
_handleMenuKeyDown(e) {
  const mi = this.activeMenu;
  const children = this.menu[mi].children;
  if (e.key === "Escape") {
    this.activeMenu = -1;
    this.needsRedraw = true;
  } else if (e.key === "ArrowDown") {
    let next = this.menuHoverItem + 1;
    while (next < children.length
           && children[next].type === "separator") next++;
    if (next < children.length) this.menuHoverItem = next;
    this._scrollDropdownToItem(children);
    this.needsRedraw = true;
  } else if (e.key === "ArrowUp") {
    let prev = this.menuHoverItem - 1;
    while (prev >= 0
           && children[prev].type === "separator") prev--;
    if (prev >= 0) this.menuHoverItem = prev;
    this._scrollDropdownToItem(children);
    this.needsRedraw = true;
  } else if (e.key === "Enter") {
    if (this.menuHoverItem >= 0
        && children[this.menuHoverItem].type !== "separator") {
      this._executeMenuAction(
        children[this.menuHoverItem].action);
      this.activeMenu = -1;
      this.menuHoverItem = -1;
    }
  } else if (e.key === "ArrowLeft") {
    this.activeMenu =
      (mi - 1 + this.menu.length) % this.menu.length;
    this.menuHoverItem = -1;
    this._dropdownScrollY = 0;
    this.needsRedraw = true;
  } else if (e.key === "ArrowRight") {
    this.activeMenu = (mi + 1) % this.menu.length;
    this.menuHoverItem = -1;
    this._dropdownScrollY = 0;
    this.needsRedraw = true;
  }
  e.preventDefault();
}
```

Arrow Down and Arrow Up move `menuHoverItem` by one, skipping separators. The while loop advances past any separators to the next selectable item. If there is no next item (we are at the end of the list), the hover index stays where it is — we do not wrap around. This is a deliberate choice: wrapping from the bottom to the top of a menu is disorienting, and the user's expectation when pressing Down at the last item is that nothing happens, not that the selection jumps to the top.

The separator skipping is important for usability. Without it, the user would have to press Arrow Down twice to move past a separator — once to land on the separator (which is not selectable) and once to move to the next item. The while loop makes separators invisible to keyboard navigation, which is the expected behavior in every menu system.

The initial `menuHoverItem` is -1, meaning no item is highlighted. The first press of Arrow Down moves to item 0 (the first selectable item). If the user opens a dropdown and immediately presses Enter without pressing Arrow Down first, nothing happens — `menuHoverItem` is -1, so the Enter check `menuHoverItem >= 0` fails. This prevents accidental activation of the first item on dropdown open.

Arrow Left and Arrow Right switch to the adjacent top-level menu using modular arithmetic: `(mi - 1 + this.menu.length) % this.menu.length` wraps from the first menu to the last, and `(mi + 1) % this.menu.length` wraps from the last to the first. Unlike vertical navigation, horizontal wrapping is expected — pressing Right on the last menu should wrap to the first, because the menu bar is conceptually circular. The hover item is reset and the scroll is reset so the new dropdown opens fresh.

Enter executes the hovered item's action, then closes the dropdown. If no item is hovered (`menuHoverItem < 0`), Enter does nothing — the user must select an item first.

Escape closes the dropdown without executing anything.

Every key calls `e.preventDefault()` to prevent the browser from handling the key in its default way. This is especially important for Arrow keys (which would scroll the page) and Escape (which might blur the focused element or exit fullscreen).

The `_scrollDropdownToItem` method keeps the keyboard-selected item visible when the dropdown is scrollable:

```javascript
_scrollDropdownToItem(children) {
  const itemH = Math.round(26 * this.dpr);
  const sepH = Math.round(8 * this.dpr);
  const maxH = this.canvas.height - this.menuBarH
    - Math.round(8 * this.dpr);

  let totalH = 0;
  for (let i = 0; i < children.length; i++) {
    totalH += children[i].type === "separator" ? sepH : itemH;
  }
  if (totalH <= maxH) return;

  let y = 0;
  for (let i = 0; i < this.menuHoverItem; i++) {
    y += children[i].type === "separator" ? sepH : itemH;
  }
  const h = children[this.menuHoverItem].type === "separator"
    ? sepH : itemH;

  if (!this._dropdownScrollY) this._dropdownScrollY = 0;
  if (y < this._dropdownScrollY) {
    this._dropdownScrollY = y;
  } else if (y + h > this._dropdownScrollY + maxH) {
    this._dropdownScrollY = y + h - maxH;
  }
}
```

This computes the y-position of the hovered item by summing the heights of all preceding items. If the item is above the visible area, the scroll snaps to show it at the top. If the item is below the visible area, the scroll snaps to show it at the bottom. If the item is already visible, no scroll change occurs.

This auto-scrolling is essential for the View menu, which can contain many syntax entries. Without it, pressing Arrow Down past the bottom of the visible area would move the highlight off-screen, leaving the user blind to their selection. The auto-scroll keeps the selected item always visible, regardless of how long the menu is.


## 9.6 — Menu Actions

When the user selects a menu item — by clicking or pressing Enter — the `_executeMenuAction` method is called with the item's `action` string:

```javascript
_executeMenuAction(action) {
  switch (action) {
    case "new":
      this.doc = new Doc("", "untitled");
      this.scrollX = 0; this.scrollY = 0;
      this._computeGutter();
      break;
    case "open":
      document.getElementById("fileInput").click();
      break;
    case "save":
      this._saveFile();
      break;
    case "close":
      this.doc = new Doc("", "untitled");
      this.scrollX = 0; this.scrollY = 0;
      this._computeGutter();
      break;
    case "undo": this.doc.undo(); break;
    case "redo": this.doc.redo(); break;
    case "cut":
      if (this.doc.hasSelection()) {
        navigator.clipboard.writeText(
          this.doc.getSelectedText());
        this.doc.deleteSelection();
      }
      break;
    case "copy":
      if (this.doc.hasSelection()) {
        navigator.clipboard.writeText(
          this.doc.getSelectedText());
      }
      break;
    case "paste":
      navigator.clipboard.readText().then(text => {
        if (text) {
          this._insertTextAtCursor(text);
          this.needsRedraw = true;
        }
      });
      break;
    case "selectAll": this._selectAll(); break;
    case "duplicateLine": this._duplicateLine(); break;
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
    case "toggleHighlight":
      this.highlightEnabled = !this.highlightEnabled;
      break;
    default:
      if (action && action.startsWith("syntax_")) {
        const idx = parseInt(action.substring(7));
        this._setSyntax(idx);
      }
      break;
  }
  this.needsRedraw = true;
}
```

The dispatcher is a switch statement on the action string. Each case performs the corresponding operation. Most cases call methods we have already built: `Doc.undo`, `Doc.redo`, `_saveFile`, `_selectAll`, `_duplicateLine`, `_rebuildAtlas`, `_setSyntax`.

The `default` case handles dynamic actions — specifically, syntax selection. Actions like `"syntax_0"`, `"syntax_3"`, `"syntax_9"` are parsed by checking the `"syntax_"` prefix and extracting the numeric index. This is a simple string-based dispatch convention that avoids the need for a registration system or a map of action names to handlers.

There is an important distinction between synchronous and asynchronous actions. Most actions are synchronous — they modify the editor state immediately and rely on the `needsRedraw = true` at the end to trigger a repaint. The "paste" action is asynchronous — it calls `navigator.clipboard.readText()`, which returns a promise. The text is inserted and `needsRedraw` is set when the promise resolves, which might be on a later frame. This means there is a brief delay between selecting Paste from the menu and seeing the pasted text, but in practice the delay is imperceptible.

The "open" action is also asynchronous in a different way. It calls `document.getElementById("fileInput").click()`, which triggers the browser's file picker dialog. The user selects a file, and the `change` event on the file input fires some time later. The file is read via `FileReader.readAsText()`, and when the read completes, a new `Doc` is created. This chain of asynchronous steps is invisible to the menu system — the action just kicks off the process and returns.

The `needsRedraw = true` at the bottom of the method ensures that even actions with no visible effect (like copying text with no selection) still trigger a repaint. This is slightly wasteful but safe — it guarantees that the dropdown closing is rendered, regardless of what the action did.

It is worth noting that the menu actions duplicate some of the functionality that the keyboard shortcuts provide. Ctrl+Z and Edit > Undo both call `this.doc.undo()`. Ctrl+S and File > Save both call `this._saveFile()`. This duplication is intentional. The keyboard shortcuts and the menu items are two different interfaces to the same underlying operations. The keyboard shortcuts are defined in `_onKeyDown` and call the operations directly. The menu items define action strings that the dispatcher maps to the same operations. The two paths converge at the same methods — `undo`, `_saveFile`, `_selectAll`, etc. — so the behavior is identical regardless of how the user triggers it.

One action deserves special attention: "new" and "close" both create a new empty document. In a more complete editor, "close" might close the current tab in a multi-tab interface, while "new" might create an additional tab. In our single-document editor, both operations have the same effect: the current document is replaced with an empty one, and the scroll is reset to the origin. The distinction exists in the menu for conceptual clarity — "New" implies creating something, while "Close" implies dismissing the current file — even though the implementation is identical.


## 9.7 — The Modal Input Problem

The menu system introduces a fundamental challenge that we have alluded to in previous chapters: modal input. When a dropdown is open, the keyboard and mouse behave differently than when it is closed. Arrow keys navigate the menu instead of the cursor. Enter selects a menu item instead of inserting a newline. Clicks on dropdown items execute actions instead of placing the cursor.

Our solution is straightforward: check the modal state at the top of each input handler and route to the appropriate handler. In `_onKeyDown`:

```javascript
if (this.activeMenu >= 0) {
  this._handleMenuKeyDown(e);
  return;
}
```

In `_onMouseDown`:

```javascript
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
```

In `_onWheel`:

```javascript
if (this.activeMenu >= 0) {
  // ... scroll the dropdown instead of the document ...
  return;
}
```

In `_onMouseMove`:

```javascript
if (this.activeMenu >= 0) {
  this.needsRedraw = true;
}
```

The pattern is consistent: if a menu is open, handle the input in the menu context and return before the normal handling code runs. The `return` statements are the firewall — they prevent any menu-mode input from leaking into text-editing mode.

This approach works because our editor has exactly two modes: normal editing and menu browsing. If we had more modes (command palette, find/replace dialog, settings panel), we would need a more general modal input system — perhaps a stack of input handlers, where each handler can consume events or pass them to the next handler in the stack. But for two modes, the simple `if (this.activeMenu >= 0)` check is clear and correct.

The modal approach mirrors how *lite* handles its command view. When the command view is active, keyboard input goes to the command input rather than the document. The mechanism is different — *lite* routes input through its view hierarchy, and the focused view receives the events — but the principle is the same: only one UI element handles input at a time, and there is a clear mechanism for determining which one.

The transition between modes is also simple. Opening a menu sets `this.activeMenu` to the menu index. Closing a menu sets it to -1. There is no cleanup, no event unregistration, no state machine. The mode is a single integer, and the input handlers check it. The simplicity of this approach is its strength — there are no mode-related bugs to hunt, because the mode is always consistent (it is a single variable) and the checks are always performed (they are at the top of every handler).


## 9.8 — What We Have, and What Comes Next

The menu system is the most complex piece of UI in the editor, and it is built entirely from the same primitives as everything else: `fillRect` for backgrounds and highlights, `_drawTintedRun` for text, `clip` for scroll boundaries, and mouse/keyboard position checks for interaction. There are no DOM elements, no CSS styles, no HTML templates. The dropdown is drawn, hit-tested, and navigated entirely in JavaScript and canvas.

The system handles three menus (File, Edit, View) with a combined total of over twenty items, including separators, shortcuts, check indicators, and dynamically generated syntax entries. It supports mouse hover, mouse click, keyboard navigation with separator skipping, scrollable content for long menus, scroll indicators, and auto-scrolling during keyboard navigation. And it does all of this in roughly 200 lines of code.

In Chapter 10, we will tie everything together. We will review the complete architecture of the editor — how the classes interact, how data flows from input to state to rendering, and how the design decisions we made in Chapter 1 carry through to the final product. We will examine file I/O in detail, discuss performance characteristics, and look at where the editor could go next.

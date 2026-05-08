# Research Agent 3: Text Editing And UI Plan

## Executive Summary

The editor should follow lite's shape, not VS Code's architecture. Use a single immediate-mode UI loop, a small view hierarchy, line-array `Document` objects, a command registry, focused keybindings, visible-line rendering, and cooperative background jobs. Add a VS Code-like side bar as first-party UI, not as an extension host.

The target shell:

- One WebGL2 canvas renders all editor text and UI through the Slug renderer.
- TypeScript browser application, with browser APIs as the platform host.
- IndexedDB-backed virtual filesystem for imported workspaces.
- Drag/drop file and folder import into that virtual filesystem.
- Hidden native textarea input bridge for shortcuts, clipboard, IME, and mobile virtual keyboards.
- High-DPI canvas backing sized from `devicePixelRatio`, while editor layout and hit testing stay in CSS pixels.
- A retained application state tree describes documents, views, tabs, sidebar state, search results, and chat turns.
- Each frame builds a draw command list from that state, then the renderer batches Slug text/path instances.
- Text editing is implemented in plain TypeScript: no CodeMirror, Monaco, DOM textareas for main editing, canvas 2D, React, CSS layout, or external parser libraries.
- Syntax highlighting starts with lite-style incremental regex/pattern tokenizers. Tree-sitter-level parsing is explicitly out of initial scope.
- The chat assistant is a small harness panel. It owns turns, streams model output, dispatches local tools, and feeds selected editor/project context into model calls.

Recommended implementation order after the Slug renderer:

1. Minimal immediate-mode UI and layout primitives.
2. Document model, cursor/selection, undo/redo, keybindings.
3. Visible-line text editor with gutter, scroll, caret, selection, and tabs.
4. Syntax highlighter and command palette.
5. Side bar with file tree and search.
6. Chat assistant harness.
7. Splits, richer search/replace, persistence, polish.

This deliberately leaves out a plugin architecture. The code can stay maintainable by using fixed modules with explicit ownership: `app`, `ui`, `editor`, `project`, `search`, `assistant`, `renderer`, `font`.

## Lite/lite-xl Findings

### Lite Architecture

rxi's implementation overview says lite's core owns documents, syntax highlighting, cooperative threads, logging, UI layout, input, and rendering, with complexity added only after the simplest method proves impractical (https://rxi.github.io/lite_an_implementation_overview.html). That is the correct baseline for this project.

Relevant lite source patterns:

- `core.init` creates a root view, command view, status view, document list, project file list, cooperative thread list, and a `redraw` flag; the frame loop polls events, updates layout, runs threads, and redraws when needed (https://github.com/rxi/lite/blob/master/data/core/init.lua).
- `Doc` stores `lines`, one selection, undo/redo stacks, syntax, highlighter state, filename, line-ending mode, and clean/dirty change IDs (https://github.com/rxi/lite/blob/master/data/core/doc/init.lua).
- `DocView` computes visible line range from scroll position, renders gutter first, then clips and renders text body, selection, line highlight, and caret only for visible lines (https://github.com/rxi/lite/blob/master/data/core/docview.lua).
- `RootView` uses a binary split tree of nodes. A leaf has one or more views as tabs; split nodes have two children and a divider. Locked nodes are used for command/status bars (https://github.com/rxi/lite/blob/master/data/core/rootview.lua).
- `command.add(predicate, map)` stores command functions with predicates; `keymap` maps strokes to ordered command names and executes the first valid command (https://github.com/rxi/lite/blob/master/data/core/command.lua, https://github.com/rxi/lite/blob/master/data/core/keymap.lua).
- `CommandView` is just a single-line `DocView` with suggestion rendering. This is a useful trick: prompts, palette, goto-line, find, and save-as can all reuse editor text input behavior (https://github.com/rxi/lite/blob/master/data/core/commandview.lua).
- `Highlighter` tokenizes visible lines immediately and tokenizes wanted lines incrementally in a coroutine; it tracks `first_invalid_line` and `max_wanted_line` (https://github.com/rxi/lite/blob/master/data/core/doc/highlighter.lua).
- `treeview.lua` and `projectsearch.lua` are small views over `core.project_files`; project search yields every 100 lines to keep UI responsive (https://github.com/rxi/lite/blob/master/data/plugins/treeview.lua, https://github.com/rxi/lite/blob/master/data/plugins/projectsearch.lua).

Important lite design lesson: polling and full redraw simplify ownership. The overview explicitly calls out that lite avoids event listener complexity and redraws the UI from current state when `core.redraw` is true (https://rxi.github.io/lite_an_implementation_overview.html). We should use that. The renderer can still internally cache/batch.

### Lite XL Additions Worth Copying

Lite XL keeps the same conceptual architecture but adds features we should selectively adopt:

- Multiple selections and carets are stored as a flat `selections` array of line/column quads, iterated in order (https://github.com/lite-xl/lite-xl/blob/master/data/core/doc/init.lua).
- `DocView` accounts for syntax-specific fonts, tab offsets, horizontal scroll, multiple selections, IME composition, gutter selection, overwrite caret, and visible-line early exit (https://github.com/lite-xl/lite-xl/blob/master/data/core/docview.lua).
- `Node` adds scrollable tabs, close buttons, tab drag/split behavior, fixed-size locked panes, and resizable locked panes (https://github.com/lite-xl/lite-xl/blob/master/data/core/node.lua).
- `CommandView` supports typed options such as initial text, select text, show suggestions, typeahead, wrapping, validation, and variable visible command count (https://github.com/lite-xl/lite-xl/blob/master/data/core/commandview.lua).
- The highlighter has a `resume` mechanism for long tokenization and explicit insert/remove notifications (https://github.com/lite-xl/lite-xl/blob/master/data/core/doc/highlighter.lua).

Adopt carefully:

- Phase 1 should use one cursor/selection. Design the data structures so Lite XL-style multiple selections can be added without rewriting commands.
- Implement IME only after the main editor is usable. JavaScript composition events can feed an `ime` state, but Slug rendering needs custom underline/caret decorations.
- Implement tab scrolling, close buttons, and split drag after basic tab/split behavior works.

### Renderer/UI Model From Lite XL

Lite XL uses immediate-mode rendering: begin frame, issue `draw_text`/`draw_rect`, end frame (https://lite-xl.com/developer-guide/advanced-topics/how-renderer-works/). It also uses a command buffer and hash grid to reduce dirty work internally while the Lua side behaves as though it redraws the whole UI.

For this WebGL2 editor, use the same API shape but not the same software renderer:

```js
renderer.beginFrame(viewport);
ui.draw(appState, renderer);
renderer.endFrame();
```

`renderer.drawText`, `renderer.drawPath`, and `renderer.drawRect` should append commands. `endFrame` sorts/batches by clip, font atlas/path album, shader, and blend state, then issues WebGL2 draws. Dirty rectangles are optional later; first optimize by drawing only visible editor lines and batching well.

### Syntax Highlighting Model

Lite's tokenizer uses ordered patterns and a `symbols` table; the first pattern match wins, and range patterns carry line-to-line state for strings/comments (https://rxi.github.io/lite_an_implementation_overview.html). Lite XL's syntax docs describe the same basic model, with supported token types such as `normal`, `symbol`, `comment`, `keyword`, `number`, `string`, `operator`, and `function` (https://lite-xl.com/developer-guide/syntaxes-and-themes/creating-syntaxes/).

Use this as the first syntax engine. It is enough for C/C++/JS/Lua/Python/Markdown-like highlighting and easy to implement with no dependencies.

## Proposed Editor Architecture

### Module Layout

```text
src/
  app/
    app_state.js
    main_loop.js
    commands.js
    keymap.js
    input.js
    input_bridge.js
    scheduler.js
    settings.js
  ui/
    view.js
    root_view.js
    node.js
    panel.js
    command_view.js
    status_view.js
    sidebar_view.js
    widgets.js
  editor/
    document.js
    document_store.js
    edit_ops.js
    undo.js
    selection.js
    cursor_motion.js
    doc_view.js
    text_layout_cache.js
    syntax.js
    tokenizer.js
    highlighter.js
  project/
    project_store.js
    file_tree.js
    file_index.js
    fs_host.js
  search/
    doc_search.js
    project_search.js
  assistant/
    chat_store.js
    turn_runner.js
    context_builder.js
    tool_registry.js
    sse_parser.js
    openai_client.js
    transcript_view.js
  renderer/
    draw_list.js
    slug_renderer.js
    path_builder.js
    clip_stack.js
    theme.js
  font/
    ... from earlier font-parser plan
```

Use plain objects/classes. Avoid a framework. The application loop owns update/draw order and all input routing.

### Runtime Ownership

```text
AppState
  settings
  project: ProjectStore
  docs: DocumentStore
  rootView: RootView
  commandView: CommandView
  statusView: StatusView
  sidebar: SidebarView
  assistant: ChatStore
  scheduler: CooperativeScheduler
  rendererResources
```

`DocumentStore` ensures only one `Document` exists for a canonical file path, matching lite's rule that opening the same filename returns the existing doc (https://rxi.github.io/lite_an_implementation_overview.html).

`RootView` handles editor tabs/splits only. The VS Code-like side bar should be a fixed first-party pane outside the editor split tree:

```text
Window
  ActivityBar  fixed width
  Sidebar      fixed/resizable width, mode = files/search/chat
  MainArea
    EditorRoot split tree with tab leaves
  StatusBar    fixed height
  CommandView  overlay or bottom locked prompt
```

This avoids forcing file tree/search/chat into the same tab model as editable documents.

### Immediate-Mode View Contract

```js
class View {
  constructor() {
    this.rect = { x: 0, y: 0, w: 0, h: 0 };
    this.scroll = { x: 0, y: 0, targetX: 0, targetY: 0 };
    this.cursor = "default";
  }
  update(dt, app) {}
  draw(ctx) {}
  hitTest(x, y) { return this; }
  onMouseDown(e) { return false; }
  onMouseMove(e) { return false; }
  onMouseUp(e) { return false; }
  onWheel(e) { return false; }
  onTextInput(text) { return false; }
  onKeyDown(e) { return false; }
}
```

`ctx` is a drawing context over the Slug renderer:

```js
ctx.pushClip(rect);
ctx.rect(rect, color);
ctx.path(pathId, transform, color);
ctx.text(fontId, textRun, x, baselineY, color, options);
ctx.popClip();
```

Every `draw` call should be deterministic from state. Do not attach per-widget DOM events. Input routing should happen through root hit testing and active/focused view state.

## Text Buffer/Data Structures

### Initial Buffer Choice

Use a line array first:

```js
class Document {
  lines = ["\n"];          // each line includes trailing "\n" except possibly imported text normalized on load
  selections = [new Selection(1, 1, 1, 1)];
  primarySelection = 0;
  undo = new UndoStack();
  redo = new UndoStack();
  filename = null;
  absPath = null;
  crlf = false;
  syntax = null;
  highlighter = new Highlighter(this);
  cleanChangeId = 1;
  changeId = 1;
  version = 1;
}
```

Reasoning:

- Lite proves a line array is enough for a small fast editor (https://github.com/rxi/lite/blob/master/data/core/doc/init.lua).
- Most rendering, syntax highlighting, search, and hit testing are line-based.
- For a code editor, edits are usually local and files are moderately sized.
- It is far simpler than a piece table or rope and lets us build the editor now.

Risk:

- Huge files and long single lines will hurt. Mitigation is line virtualization and a later optional piece-table backend behind the same `Document` API.

### Position Convention

Use 1-based line and UTF-16 column initially, because JavaScript strings expose UTF-16 indexing and it keeps command code small. Store helper functions so this can later become codepoint/grapheme-aware:

```js
Position { line: number, col: number } // line >= 1, col >= 1
Selection { head: Position, anchor: Position }
```

For phase 1, treat code editing as mostly ASCII/UTF-8-compatible text. Add UTF-16 surrogate and grapheme correctness later. The font/layout research already scopes complex Unicode and shaping as later work.

### Core Document API

```js
doc.getText(line1, col1, line2, col2)
doc.setSelection(line1, col1, line2 = line1, col2 = col1, swap = false)
doc.getSelection(sort = false)
doc.hasSelection()
doc.sanitizePosition(line, col)
doc.positionOffset(line, col, motionOrByteOffset)
doc.insert(line, col, text)
doc.remove(line1, col1, line2, col2)
doc.textInput(text)
doc.deleteTo(motion)
doc.moveTo(motion, view)
doc.selectTo(motion, view)
doc.undo()
doc.redo()
```

Mirror lite's raw operation split:

- `rawInsert` mutates lines and pushes inverse `remove` to the undo stack.
- `rawRemove` mutates lines and pushes inverse `insert`.
- Public `insert/remove` clear redo and stamp a timestamp/change group.
- Selection changes are recorded as undo records so undo restores cursor position.

### Undo/Redo

Use grouped undo records with merge windows:

```js
UndoRecord =
  | { type: "insert", line, col, text, time, group }
  | { type: "remove", line1, col1, line2, col2, time, group }
  | { type: "selection", selections, time, group }
```

Merge consecutive text input into one group when:

- same command kind,
- adjacent insertion/deletion,
- no explicit command boundary,
- time delta is below `settings.undoMergeMs`.

Create a command boundary after paste, newline, indent, delete-line, move-lines, save, mouse click, selection command, and focus change.

### Editing Operations

Minimum command set:

- text input
- newline with current-line indentation
- newline above/below
- backspace/delete char
- delete previous/next word
- select all/none
- cut/copy/paste
- indent/unindent selected lines
- duplicate lines
- delete lines
- move lines up/down
- join lines
- toggle line comment
- upper/lower case selection
- goto line
- save/save as/reload
- undo/redo

This matches the practical subset in lite's `commands/doc.lua` (https://github.com/rxi/lite/blob/master/data/core/commands/doc.lua).

## Rendering And Layout Loop

### Frame Loop

```js
function frame(now) {
  const dt = clamp(now - lastNow, 0, 50) / 1000;
  input.flushToApp(app);
  scheduler.runBudgeted(settings.frameBudgetMs);
  app.root.update(dt, app);

  if (app.needsRedraw || app.animating) {
    renderer.beginFrame(canvasSize, theme);
    app.root.draw(drawCtx);
    renderer.endFrame();
    app.needsRedraw = false;
  }

  requestAnimationFrame(frame);
}
```

Use lite's `redraw` flag pattern. Any input event sets `needsRedraw = true`. Background jobs set it when they publish results. Blinking caret can set it only when blink phase changes.

### Visible-Line Rendering

`DocView.draw` should:

1. Draw background.
2. Compute `lineHeight`, `gutterWidth`, `contentRect`.
3. Compute visible lines from scroll Y.
4. Draw gutter line numbers for visible lines.
5. Push clip to text area.
6. For each visible line:
   - draw current-line highlight,
   - draw selection rectangles,
   - draw syntax token text runs,
   - draw diagnostics/squiggles later,
   - draw caret overlay.
7. Pop clip.
8. Draw scrollbars.

Lite and Lite XL both follow this visible-line model (https://github.com/rxi/lite/blob/master/data/core/docview.lua, https://github.com/lite-xl/lite-xl/blob/master/data/core/docview.lua).

### Text Layout Cache

Keep per-document, per-line caches:

```js
LineLayout {
  docVersion: number,
  syntaxVersion: number,
  fontKey: string,
  tabSize: number,
  text: string,
  tokens: Token[],
  glyphRuns: GlyphRun[],
  xByColumn: Float32Array | null,
  width: number
}

GlyphRun {
  tokenType: string,
  color: Color,
  fontId: number,
  glyphs: GlyphInstance[]
}
```

Invalidation:

- Text edit invalidates edited line range.
- Syntax retokenization invalidates affected lines.
- Font size/theme/tab size invalidates all visible layout caches.
- Horizontal scroll does not invalidate glyph positions, only draw transforms.

For monospace code font, fast path hit testing:

```js
col = clamp(1 + round((x + scrollX - textLeft) / charWidth), 1, line.length)
```

Still keep `xByColumn` for tabs, non-ASCII, proportional UI font, and future syntax fonts.

### Scrollbars

Start with simple immediate-mode scrollbars:

- Vertical scrollbar track inside each scrollable view.
- Thumb size proportional to `viewHeight / scrollableHeight`.
- Wheel updates `scroll.targetY`.
- Dragging thumb captures mouse in `RootView`.
- Horizontal scrollbar for editor text area.

Do not use DOM scrollbars. They will not line up with WebGL clipping/input.

## Syntax Highlighting Model

### Syntax Definition Format

Use JS objects shaped like lite/lite-xl:

```js
syntax.add({
  name: "JavaScript",
  files: [/\.m?js$/, /\.jsx$/],
  comment: "//",
  blockComment: ["/*", "*/"],
  patterns: [
    { pattern: /\/\/.*$/, type: "comment" },
    { begin: /\/\*/, end: /\*\//, type: "comment" },
    { begin: /"/, end: /"/, escape: "\\", type: "string" },
    { pattern: /\b\d+(\.\d+)?\b/, type: "number" },
    { pattern: /[A-Za-z_$][\w$]*/, type: "symbol" },
    { pattern: /\s+/, type: "normal" },
    { pattern: /./, type: "operator" },
  ],
  symbols: {
    "function": "keyword",
    "const": "keyword",
    "let": "keyword",
    "return": "keyword",
  }
});
```

Implementation details:

- Patterns are tried in order.
- A range pattern stores `{ patternIndex, nestedSyntax? }` as state.
- `symbols` remaps exact token text after a generic symbol pattern matches, exactly like lite's tokenizer approach (https://rxi.github.io/lite_an_implementation_overview.html).
- Token output is a flat array of `{ type, text, startCol, endCol }`.
- Merge adjacent tokens of same type and whitespace into fewer draw calls, like lite's `push_token`.

### Incremental Highlighter

```js
class Highlighter {
  lines = [];
  firstInvalidLine = 1;
  maxWantedLine = 0;
  running = false;

  invalidate(line) {}
  getLine(line) {}
  tokenizeLine(line, prevState, resume) {}
  startBackgroundJob() {}
}
```

Rules:

- `getLine(line)` must synchronously return valid tokens for visible rendering.
- Background job tokenizes from `firstInvalidLine` to `maxWantedLine` in chunks of about 40 lines, mirroring lite/lite-xl.
- If token state changes, following lines remain invalid until retokenized.
- Long lines should tokenize with a time budget and resume state to avoid frame stalls.

### First Languages

Implement:

- Plain text
- JavaScript/TypeScript-lite
- C/C++/C#-lite
- Lua
- Python
- JSON
- Markdown-lite

Keep language rules in data files. No plugin loader is required; just import the built-in syntaxes.

## Tabs/Splits/Sidebar

### Editor Root

Use a binary split tree:

```js
Node {
  type: "leaf" | "hsplit" | "vsplit",
  rect,
  divider: 0.5,
  locked: { x?: boolean, y?: boolean } | null,
  resizable: boolean,
  a: Node | null,
  b: Node | null,
  views: View[],
  activeView: View,
  tabOffset: number,
  tabWidth: number
}
```

Phase 1:

- leaf tabs
- close tab
- switch tab
- split left/right/up/down by command
- resize split divider

Phase 2:

- scrollable tabs
- close button on hover
- drag tab to split/move
- save/restore session layout

This combines lite's simple split tree with a small subset of Lite XL's modern tab behavior (https://github.com/rxi/lite/blob/master/data/core/rootview.lua, https://github.com/lite-xl/lite-xl/blob/master/data/core/node.lua).

### Side Bar

Use three modes behind an activity bar:

```js
SidebarState {
  visible: true,
  width: 300,
  active: "files" | "search" | "chat",
  files: FileTreeState,
  search: SearchPanelState,
  chat: ChatPanelState
}
```

Activity bar:

- fixed width, e.g. 44 px
- icon buttons for files, search, chat
- selected mode drawn with accent bar/background
- no text labels unless needed for accessibility/tooltips later

Side bar content:

- Files: file tree, open file on click, context menu later.
- Search: query input, case/regex toggles, result list grouped by file, open on click.
- Chat: transcript, composer, attach-context controls, stop button, tool/result disclosure rows.

The side bar is a first-party view with fixed modes. Do not support arbitrary contributed panels.

### Status Bar

Status bar content:

- left: active file dirty marker, filename, current message/error
- center optional: background job/search/chat status
- right: line/col, selection count, line ending, indentation, syntax, token budget/model status

Lite's status view is a compact state renderer over active view/doc information (https://github.com/rxi/lite/blob/master/data/core/statusview.lua). Use that approach.

## Search And File Tree

### File Tree

`ProjectStore` owns:

```js
ProjectStore {
  rootHandleOrPath,
  files: ProjectEntry[],
  ignoredPatterns,
  version,
  scanInProgress,
}

ProjectEntry {
  path,
  name,
  type: "file" | "dir",
  depth,
  size,
  modified,
}
```

Scan strategy:

- Browser-only prototype: use File System Access API directory handles when available.
- Local-host production: a tiny dependency-free host process exposes `listDir`, `readFile`, `writeFile`, and file watching/polling.
- Sort dirs first, files second, alphabetical.
- Apply ignore globs for `.git`, `node_modules`, build output, binary/large files.
- Yield every directory or every N files through `scheduler` to keep rendering responsive, like lite's project scan coroutine (https://github.com/rxi/lite/blob/master/data/core/init.lua).

Tree view rendering:

- Flatten visible nodes each frame from expansion state.
- Use fixed row height.
- Clip to visible rows.
- Draw chevrons/icons through Slug vector paths or icon font glyphs.
- Click dir toggles expansion; click file opens document.

### Document Find/Replace

Panel/palette find:

- `Ctrl+F`: inline command/prompt or search box.
- `F3`/`Shift+F3`: next/previous.
- options: case-sensitive, regex, whole word.
- live preview selects current match and scrolls it into view.

Implementation:

```js
findInDoc(doc, startLine, startCol, query, options) -> Range | null
replaceInSelectionOrDoc(doc, query, replacement, options) -> count
```

Use JavaScript `RegExp` for regex mode. For literal mode, use `indexOf` on each line. Do not use external search libraries.

### Project Search

Use lite's `projectsearch.lua` as the model: produce a results view/panel, scan files incrementally, yield periodically, click result opens file at line/col (https://github.com/rxi/lite/blob/master/data/plugins/projectsearch.lua). Lite XL user docs expose the same UX with `Ctrl+Shift+F` and clickable results (https://lite-xl.com/user-guide/introduction/).

Project search state:

```js
SearchJob {
  id,
  query,
  options,
  fileIndex,
  lineIndex,
  results,
  done,
  cancelled,
}
```

Search result:

```js
{ path, line, col, text, matchLength }
```

Limit result count initially, e.g. 10,000, and show truncation status.

## Keybinding/Command Model

### Commands

Use lite/lite-xl style command predicates, but implemented in JS:

```js
commands.add({
  id: "doc.save",
  title: "Doc: Save",
  when: app => app.activeView instanceof DocView,
  run: app => app.activeDoc.save()
});
```

Command registry:

```js
class CommandRegistry {
  map = new Map();
  add(command) {}
  perform(id, app, ...args) {}
  validCommands(app) {}
  prettify(id) {}
}
```

Predicates keep keybindings context-aware. Lite XL's docs describe this pattern directly: a command has a predicate that decides if it can run (https://lite-xl.com/developer-guide/commands-and-shortcuts/commands/).

### Keymap

```js
keymap.add({
  "Ctrl+Shift+P": ["core.findCommand"],
  "Ctrl+P": ["core.findFile"],
  "Ctrl+S": ["doc.save"],
  "Ctrl+F": ["find.open"],
  "Ctrl+Shift+F": ["sidebar.search"],
  "Ctrl+B": ["sidebar.toggle"],
  "Ctrl+`": ["assistant.focus"],
  "Escape": ["command.escape", "doc.selectNone"],
});
```

Store bindings in normalized stroke form. A key can map to an ordered list of command IDs; execute the first valid command, matching lite's keymap behavior (https://github.com/rxi/lite/blob/master/data/core/keymap.lua).

Use OS-specific override tables later for macOS `Meta` conventions.

### Command Palette

The command palette should reuse `CommandView`:

- single-line document for input,
- suggestions from `commands.validCommands(app)`,
- fuzzy matching,
- selected suggestion with up/down,
- enter executes,
- right-side keybinding text.

Fuzzy scoring can be no-dependency:

```js
score(candidate, query):
  lower both
  walk candidate to match query chars in order
  reward contiguous chars, word starts, earlier start
  reject if any query char missing
```

## Assistant Harness Plan

### Why GabCode Is The Useful Reference

GabCode's README describes a portable core/thin shell split, tag-based tool calls, loop-break guard, built-in tools, sub-agents, compaction, streaming, cancellation, history, and slash commands (https://github.com/gszauer/GabCode). Its technical reference makes the useful architectural boundary explicit: core owns session/conversation/tool logic; shell owns filesystem, HTTP, terminal/UI, and platform APIs (https://github.com/gszauer/GabCode/blob/main/TECHNICAL.md).

Use that split conceptually:

```text
Editor UI
  ChatPanelView renders transcript and composer
Assistant Core
  TurnRunner, context builder, tools, transcript, compaction
Host Bridge
  read/write/list files, shell/search, HTTP streaming, persistence
```

Do not copy GabCode's XML tag tool protocol as the only design. OpenAI's current Responses API is designed for stateful, tool-using interactions and built-in tools (https://platform.openai.com/docs/api-reference/responses). The OpenAI guide also recommends the Responses API over Chat Completions for newer stateful/agentic behavior (https://platform.openai.com/docs/guides/responses-vs-chat-completions). Since this editor targets OpenAI endpoints, use native Responses function calling first. Keep a simple OpenAI-compatible Chat Completions fallback only if local model compatibility is desired.

### API Boundary

Because the UI is a local editor, do not bake an API key into frontend source. Use one of two host modes:

- Browser prototype: user-configured endpoint and key are stored locally; direct `fetch` is acceptable only for local experimentation.
- Real app: dependency-free local host process exposes `POST /assistant/turn`, keeps the OpenAI key server-side, and streams events to the WebGL UI.

The host process can be a tiny Node server using built-in `http`, `fs`, and `child_process`, with no npm dependencies, or a C++ host later. The browser UI stays dependency-free either way.

### OpenAI Endpoints

Primary:

- `POST /v1/responses` for model turns, tool calls, and streaming. The Responses API reference says it creates model responses, supports text/image input, stateful interactions, built-in tools, and function calling (https://platform.openai.com/docs/api-reference/responses).
- Streaming should use server-sent events. OpenAI's streaming guide says Responses streaming uses typed semantic events such as `response.created`, `response.output_text.delta`, `response.completed`, and `error` (https://platform.openai.com/docs/guides/streaming-responses).

Conversation state options:

- Manual state: store full transcript/context in `ChatStore` and send the current window each turn.
- `previous_response_id`: chain responses when using stored OpenAI response state.
- Conversations API: create a durable conversation ID and pass it to Responses. OpenAI's conversation state guide describes Conversations as long-running objects containing messages, tool calls, and tool outputs (https://platform.openai.com/docs/guides/conversation-state).

Recommendation:

- Phase 1: manually manage state in the editor, because we need deterministic transcript/context packing and local history.
- Phase 2: optionally store `previous_response_id` or `conversation_id` for resumable hosted state.
- For long sessions, implement local summarization/compaction first. The OpenAI guide also documents `/responses/compact` for advanced long-running conversations (https://platform.openai.com/docs/guides/conversation-state).

### Chat Store

```js
ChatStore {
  sessions: ChatSession[],
  activeSessionId,
  config: {
    provider: "openai",
    apiBaseUrl,
    model,
    useHostedConversation: false,
    maxToolCallsPerTurn: 10,
    maxContextChars,
  }
}

ChatSession {
  id,
  title,
  messages: ChatMessage[],
  responseChain: { previousResponseId?: string, conversationId?: string },
  status: "idle" | "streaming" | "tool" | "compacting" | "error",
  selectedContext: ContextAttachment[],
  createdAt,
  updatedAt,
}
```

Message:

```js
ChatMessage {
  id,
  role: "user" | "assistant" | "tool" | "system-note",
  content,
  parts,
  toolName,
  toolArgs,
  toolResult,
  error,
  tokenEstimate,
}
```

### Context Builder

The editor itself is the context harness:

```js
ContextBuilder.build(app, request) -> InputItem[]
```

Available context attachments:

- active file path and dirty status
- selected text with line numbers
- visible range around cursor
- all open tabs summary
- search results
- file tree/project root
- explicit files added by user
- previous assistant summaries

Keep context explicit. The chat panel should show attached files/ranges as removable chips. Default context for a coding question:

1. Active file path.
2. Current selection if non-empty, otherwise +/- 120 lines around cursor.
3. Open tab names.
4. User prompt.

### Tools

Initial local tools:

```js
readFile(path)
listFiles(path, depth)
searchText(query, options)
getOpenDocuments()
getActiveDocument()
getSelection()
applyEdit(path, range, replacement)
openFile(path, line, col)
```

Later:

```js
writeFile(path, content)
runShell(command)
diagnostics()
```

Tool safety:

- `applyEdit` should stage a preview diff and require user approval before mutating documents.
- `writeFile` and `runShell` should be disabled by default until the host permission model exists.
- The assistant can suggest edits even before automated edit application.

GabCode has two useful safeguards to copy:

- duplicate tool-call guard so one bad call cannot loop forever,
- max tool calls per turn with a clear stop condition (https://github.com/gszauer/GabCode/blob/main/core/session.cpp).

### Streaming And Cancellation

Implement a small SSE parser like GabCode's `sse_parser.cpp` or browser `web/llm.js`, but adapted to OpenAI Responses event types (https://github.com/gszauer/GabCode/blob/main/core/sse_parser.cpp, https://github.com/gszauer/GabCode/blob/main/web/llm.js, https://platform.openai.com/docs/guides/streaming-responses).

UI event stream:

```js
{ type: "assistant.delta", text }
{ type: "tool.start", name, args }
{ type: "tool.result", name, result }
{ type: "usage", inputTokens, outputTokens }
{ type: "turn.done" }
{ type: "turn.error", error }
{ type: "turn.cancelled" }
```

Cancellation:

- Browser direct: `AbortController`.
- Local host: client closes stream or sends cancel RPC.
- On cancel, keep user message but mark assistant partial as cancelled, or rollback the whole in-flight turn. GabCode's web runner rolls back in-flight messages/history on cancel; that is clean for a harness panel (https://github.com/gszauer/GabCode/blob/main/web/llm.js).

## Integration With Slug Text Renderer

### Draw Primitives

All visible UI should reduce to these primitive types:

```js
drawSolidRect(rect, color)              // fast path, can be Slug path later
drawPath(pathId, transform, color)      // Slug vector path
drawText(fontId, shapedRun, x, y, color, clip)
drawUnderline(x1, x2, y, thickness, color)
drawSquiggle(x1, x2, y, amplitude, color)
```

Even if `drawSolidRect` is a simple WebGL quad shader for performance, all non-rect vector UI and all text go through Slug records. Icons can be:

- an icon TTF parsed by our font parser, or
- built-in path definitions converted to Slug paths.

The first path is simpler if we already support TTF; the second avoids icon-font codepoint conventions.

### Text Rendering Contract

`DocView` should not call font parser or WebGL directly. It asks layout for visible glyph instances:

```js
layoutLine(doc, lineIndex, font, syntaxTokens, tabSize) -> LineLayout
renderer.drawGlyphRun(glyphRun, offsetX, baselineY, clipRect)
```

Slug renderer consumes:

```js
GlyphInstance {
  fontResourceId,
  glyphId,
  slugRecord,
  x,
  baselineY,
  fontPx,
  color,
  clipRect,
}
```

This matches the renderer/parser contract established in `orchestrator_synthesis.md`.

### Pixel Alignment

For editor text:

- Snap baseline Y to integer or half-pixel consistently after device scale is applied.
- Use fixed line height from font metrics.
- Use monospace advance for ASCII fast path.
- Avoid per-glyph snapping that breaks token/run spacing. Snap run origin, not every glyph, unless experiments show better quality.

For UI:

- Use integer rect coordinates at CSS pixel scale, multiplied by device pixel ratio in projection.
- Keep 1 px strokes aligned to half-pixel centers if rendered as filled path/rect.

### Clipping

Use a clip stack:

- Rectangular clips map to `gl.scissor`.
- Nested clips intersect in CPU.
- Each draw command carries final clip rect.
- Renderer groups by clip rect or flushes scissor changes.

Complex non-rect clipping is deferred.

### Batching

Renderer command sorting:

1. clip rect
2. shader/material
3. font/path album textures
4. blend mode

For editor text, preserve order within a line enough for overlapping selections/carets. Since text itself is opaque-ish alpha over background, drawing tokens in order by line is fine. Batch across lines only after correctness is proven.

## Implementation Roadmap

### Phase 0: Host/UI Skeleton

- Create WebGL2 canvas and immediate-mode app loop.
- Implement draw list with `rect`, `path`, `text` command placeholders.
- Implement theme colors and fixed layout measurements.
- Implement input normalization for keyboard, text input, mouse, wheel, composition events.

Exit criteria:

- Static side bar, editor area, tab strip, status bar, command prompt can draw and resize.

### Phase 1: Text Core

- Implement `Document`, line array, selection, cursor motions, insert/remove.
- Implement undo/redo grouping.
- Implement `DocumentStore`.
- Implement `DocView` visible-line rendering using placeholder font metrics if Slug text is not ready.

Exit criteria:

- Can type, delete, move cursor, select, undo/redo, scroll, and open multiple documents from in-memory examples.

### Phase 2: Slug Text Integration

- Connect font parser output to text layout.
- Render visible lines through Slug glyph instances.
- Implement gutter, selection rects, caret, current line highlight, scrollbars.
- Add layout cache invalidation.

Exit criteria:

- Real TTF monospace text renders crisply in editor view, with hit testing matching glyph positions.

### Phase 3: Commands And Palette

- Implement command registry and keymap.
- Implement command palette as single-line document view.
- Add save/open/new, goto line, find, replace, split, close tab, tab switching.

Exit criteria:

- Editor is usable without side bar.

### Phase 4: Syntax Highlighting

- Implement syntax registry, tokenizer, highlighter.
- Add JS/C/Lua/Python/JSON/Markdown-lite syntaxes.
- Highlight visible lines synchronously, background-tokenize wanted lines.

Exit criteria:

- Editing remains responsive while highlighter catches up.

### Phase 5: Project, Files, Search

- Implement host filesystem abstraction.
- Implement project scan and file tree.
- Implement file open/save.
- Implement document find/replace.
- Implement project search panel with incremental jobs.

Exit criteria:

- Can open a project, browse files, search project, open results.

### Phase 6: Sidebar Chat Assistant

- Implement `ChatStore`, transcript view, composer.
- Implement context attachments from active doc/selection/open tabs.
- Implement no-dependency OpenAI Responses client with SSE streaming.
- Implement basic tools: read active doc, read file, list files, search text.
- Implement tool-call limit, duplicate guard, cancel.

Exit criteria:

- User can ask about current file/selection, stream answer, run read/search tools, and open referenced files.

### Phase 7: Splits, Polish, Persistence

- Add split commands and divider resizing.
- Add tab close buttons and tab overflow.
- Persist session layout, open docs, sidebar width/mode, chat sessions.
- Add IME composition rendering.
- Add multiple cursors if still desired.
- Add edit preview/apply from assistant.

## Open Questions

- Is the app target strictly browser/WebGL2, or should we immediately introduce a local host process for filesystem and OpenAI key safety?
- Should the first editing model support multiple cursors, or should we ship a single-cursor editor first and add Lite XL-style multi-selection after core editing is stable?
- Which exact monospace and UI TTF fonts should be bundled? The font parser plan works best with TrueType `glyf` outlines.
- Should the chat assistant use OpenAI-hosted conversation state, or should all state remain local for deterministic context packing?
- Should assistant edit application be interactive-only at first, or can it directly modify dirty documents after tool approval?
- The user mentioned `pi` as another minimal harness reference. I did not find a clear local or public `pi` source matching that description, so this plan uses GabCode as the concrete harness reference.

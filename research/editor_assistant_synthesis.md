# Editor, UI, and Assistant Synthesis

This is the implementation-facing synthesis for the second research pass. It should be read after:

1. `text_editing_ui_plan.md`
2. `chat_agent_harness_plan.md`
3. `font_ui_editor_architecture.md`
4. `orchestrator_synthesis.md`
5. `browser_typescript_platform_plan.md`
6. `input_clipboard_mobile_dpi_plan.md`

## Product Shape

Build a browser-window TypeScript code editor with a Lite-style core and a VS Code-like first-party sidebar. The main UI is rendered into a single WebGL2 canvas.

The app should have:

- IndexedDB-backed virtual filesystem.
- Drag/drop file and folder import.
- Hidden native textarea input bridge for keyboard, clipboard, IME, and mobile virtual keyboard support.
- High-DPI WebGL2 canvas resizing using device-pixel backing size and CSS-pixel layout.
- Activity bar with `files`, `search`, and `chat`.
- Resizable primary sidebar.
- Central editor area with tab groups and optional splits.
- Status bar.
- Command palette.
- Multi-document tab support.
- Syntax highlighting using incremental line tokenizers.
- Chat assistant panel that can inspect editor/project context and propose actions.

Do not build a plugin host for v1. Keep files/search/chat as first-party views.

## Browser Platform Contract

The browser is the host.

Baseline storage and file behavior:

- Imported projects live in an IndexedDB virtual filesystem.
- Drag/drop copies files into the virtual workspace.
- Folder import must work through the best available browser path: File System Access handles, `webkitGetAsEntry()`, or `<input type="file" webkitdirectory>`.
- File System Access API is an optional enhancement for direct disk open/save, not a v1 requirement.
- Workspace export is required so users can recover data if browser storage is cleared.

The editor core depends on a `Vfs` interface, not directly on IndexedDB. The browser platform layer implements `IndexedVfs`.

Input is handled through an `InputBridge`, not DOM-rendered editor text:

- WebGL2 draws all visible text.
- Hidden textarea receives focus.
- `keydown` handles shortcuts/navigation.
- `beforeinput`/`input` handles text insertion/deletion.
- composition events handle IME.
- copy/cut/paste use Clipboard API plus textarea event fallbacks.
- iOS keyboard is opened by focusing the textarea synchronously inside a pointer/touch gesture.

Layout uses CSS pixels. Canvas backing and WebGL viewport use device pixels from `devicePixelRatio`; pointer hit testing stays in CSS pixels.

## Architecture Contract

Top-level ownership:

```text
EditorHarness
  AppState
  DocumentStore
  ProjectStore
  IndexedVfs
  InputBridge
  ViewportService
  SearchService
  RootView
  SidebarView
  CommandView
  StatusView
  JobQueue
  ChatHarness
  SlugRenderer
```

The editor is the harness. Chat is a child service that requests context and actions from the editor through narrow interfaces.

`EditorHarness` owns:

- workspace root and filesystem host
- IndexedDB virtual filesystem state
- keyboard/clipboard/mobile input bridge
- device-pixel-ratio and visual viewport state
- open documents and revisions
- tab/split layout
- sidebar state
- search indexes/results
- pending assistant actions
- confirmation policy
- draw invalidation

`ChatHarness` owns:

- chat threads
- per-turn state machine
- model transport
- tool call loop
- context summaries
- streamed transcript events
- local history persistence

## Lite Patterns To Copy

Copy these ideas from rxi lite/lite-xl:

- `Document` as a line array with selection, undo/redo, syntax, highlighter, dirty state.
- `DocView` renders only visible lines.
- `RootView` uses a binary split tree; leaves contain tabbed views.
- `CommandView` is an editor-like single-line prompt with suggestions.
- Cooperative `JobQueue` handles syntax highlighting, project search, file scanning, and context building.
- Immediate-mode UI draw: views draw from current state every frame; renderer batches internally.
- Polling and explicit state invalidation instead of a wide event-listener graph.

Adopt later:

- Multiple selections/carets.
- IME composition.
- Tab scrolling/dragging.
- More advanced syntax highlighter resume behavior.

## Document Model

Start with line arrays:

```ts
type Document = {
  id: string;
  path?: string;
  lines: string[];
  revision: number;
  savedRevision: number;
  lineEnding: "lf" | "crlf";
  syntaxId: string;
  selections: Selection[];
  undo: UndoStack;
  redo: UndoStack;
  highlighter: Highlighter;
};
```

Use one selection first, but keep `selections` as an array so multiple cursors can be added later.

Editing primitives:

- `insertText(doc, selection, text)`
- `deleteRange(doc, range)`
- `replaceRange(doc, range, text)`
- `applyEditGroup(doc, edits, label)`

All mutations increment `revision`, invalidate highlighter lines, invalidate layout cache, and mark the document dirty.

## Rendering Loop

Frame order:

1. Poll input.
2. Dispatch key/mouse/text events to active view.
3. Run cooperative jobs within a time budget.
4. Update layout rectangles.
5. Build draw commands from views.
6. Submit text/path/rect instances to Slug renderer.
7. Present.

`DocView` draw order:

1. background
2. gutter background and line numbers
3. current line highlight
4. selection rectangles
5. syntax token text runs
6. caret(s)
7. scrollbars

Visible-line rendering is mandatory. Never build glyph instances for the whole file.

## Syntax Highlighting

Use a Lite-style ordered-pattern tokenizer:

```ts
type Syntax = {
  id: string;
  files: RegExp[];
  symbols: Record<string, TokenType>;
  patterns: Array<{
    type: TokenType;
    pattern: RegExp | [RegExp, RegExp, string?];
  }>;
};
```

Highlighter state:

- cached tokens per line
- starting state for multiline constructs
- ending state after line
- `firstInvalidLine`
- `maxWantedLine`

Visible lines tokenize synchronously. Background tokenization catches up in chunks.

Start with built-in syntax definitions for:

- JavaScript/TypeScript
- C/C++
- Lua
- Python
- JSON
- Markdown-lite
- plain text

## Sidebar

Activity bar:

- Files
- Search
- Chat
- Settings later

Files panel:

- project tree
- lazy directory expansion
- open file on click
- refresh/rescan
- dirty/open markers

Search panel:

- query input
- include/exclude filters later
- cooperative project scan
- result groups by file
- click opens file at line/column

Chat panel:

- thread list or current thread header
- transcript
- context chips
- model/status/usage line
- composer
- stop button
- pending action cards

The sidebar is outside the editor split tree. Splits/tabs are for editable documents.

## Assistant Harness

Primary endpoint: OpenAI Responses API, `POST /v1/responses`.

Use:

- `stream: true`
- native function tools
- local state first with `store: false`
- local transcript/history as source of truth
- optional `previous_response_id` only later
- optional OpenAI Conversations only after local replay is solid

Do not expose a direct write tool in v1. The model proposes changes; the editor applies after confirmation.

Initial tools:

Read-only:

- `get_workspace_summary`
- `list_files`
- `read_file`
- `get_open_documents`
- `get_active_selection`
- `search_text`
- `get_diagnostics` placeholder

Proposal tools:

- `propose_file_edit`
- `propose_new_file`
- `propose_delete_file`
- `propose_command`

Tool policy:

- Read tools execute automatically.
- Proposal tools create `PendingAction` objects.
- Mutations require user approval.
- Commands require user approval and should start disabled.
- Closed-file edits must obey read-before-edit.
- Stale document revisions reject proposed edits.

## Turn Lifecycle

Turn state machine:

```text
idle
collecting_context
budgeting
streaming_model
executing_read_tool
waiting_for_user_approval
applying_action
resuming_after_tool
completed
cancelled
failed
```

Per turn:

1. Snapshot editor state.
2. Build `ContextBundle`.
3. Budget context.
4. Send `responses.create`.
5. Normalize SSE events into internal events.
6. Execute read tools or create pending actions.
7. Pause for approvals when needed.
8. Resume model with tool results.
9. Persist transcript, usage, context summary, and applied actions.

Guards:

- max round trips per user turn: 8
- max tool calls per user turn: 16
- duplicate tool call guard
- abort controller for cancel
- output caps on read/search tools

## Context Bundle

Default context should be explicit and visible in the UI as removable chips:

```ts
type ContextBundle = {
  workspace: WorkspaceSummary;
  activeEditor?: ActiveEditorContext;
  openTabs: OpenTabSummary[];
  visibleRanges: NumberedTextBlock[];
  selectedText?: NumberedTextBlock;
  searchResults?: SearchResultSummary[];
  recentActions: ActionSummary[];
  conversationSummary?: string;
  recentTurns: TranscriptItem[];
};
```

Default priority:

1. user prompt
2. selected text
3. active file nearby lines
4. visible ranges
5. open tab summaries
6. search panel state
7. recent assistant actions
8. lexical project snippets from search
9. conversation summary and recent turns

Do not dump the whole project by default.

## Persistence

Persist locally:

- open documents/session layout
- sidebar mode/width
- workspace file index metadata
- chat threads
- transcript items
- context summaries
- pending/applied actions
- settings

Use JSON/JSONL-like records inside IndexedDB first. A future native/local host can mirror this to project files under `.slug/`, but v1 persistence is browser storage.

## Implementation Order

Phase A: Editor Skeleton

- App loop.
- View base class.
- Root layout with activity bar/sidebar/main/status.
- Command registry and keymap.
- Fake renderer draw list.

Phase A2: Browser VFS

- IndexedDB schema and migration.
- `IndexedVfs`.
- File and folder import.
- Drag/drop import.
- Workspace export.
- Storage quota/persistence checks.

Phase A3: Input And Viewport Bridge

- Hidden textarea focus bridge.
- Shortcut normalization with `Mod` mapping.
- Copy/cut/paste for WebGL selection.
- `beforeinput` text insertion/deletion.
- IME composition preview/commit.
- iOS virtual keyboard path.
- `visualViewport` handling.
- High-DPI canvas resize and WebGL viewport contract.

Phase B: Documents And Editing

- DocumentStore.
- Line-array document.
- Cursor, selection, insert/delete.
- Undo/redo.
- Single `DocView`.
- Gutter, caret, selection, scrolling.

Phase C: Tabs, Splits, Palette

- RootView split tree.
- Tab groups.
- CommandView.
- Save/open integration with filesystem host.

Phase D: Syntax And Search

- Syntax registry.
- Incremental highlighter.
- Built-in syntax definitions.
- Project file scanner.
- Search panel and result navigation.

Phase E: Chat Harness Without Network

- Chat sidebar UI.
- ChatStore and transcript rendering.
- Fake streaming transport.
- Tool registry with mocked read/search/propose actions.
- Pending action cards.

Phase F: OpenAI Responses Transport

- Direct `fetch` client.
- SSE parser.
- Function-call event normalization.
- Tool result loop.
- Cancel handling.
- Token/usage display.

Phase G: Apply Edits And Commands

- Diff renderer for proposed edits.
- Apply/reject flow.
- Read-before-edit validation.
- Revision checks.
- Optional command execution after confirmation.

Phase H: Integration With Slug Renderer

- Replace fake draw list with Slug text/path renderer.
- Cache visible-line layouts.
- Draw sidebar/chat text with same font renderer.
- Add icons as vector paths.

## Decisions For V1

- Browser TypeScript target.
- IndexedDB virtual filesystem.
- Drag/drop import.
- Hidden native input bridge for text, clipboard, IME, and mobile keyboard.
- Device-pixel-ratio aware WebGL2 rendering.
- Local state is source of truth.
- OpenAI Responses API is primary.
- Chat Completions/tag tool fallback is not part of v1 unless local-model compatibility becomes a hard requirement.
- Single cursor first.
- No plugin system.
- No LSP in v1.
- No direct write tool in v1.
- Files/search/chat are built-in panels.
- All main UI is rendered by WebGL2/Slug, not DOM widgets.

## Remaining Questions

- Where should API keys live? A browser-only app cannot safely hold production OpenAI keys.
- Should v1 support direct disk writes through File System Access when available, or only import/export through the IndexedDB VFS?
- Should chat be allowed in a secondary right sidebar later?
- Should edits be applied to dirty open buffers only, or can assistant actions edit unopened files directly after confirmation?
- What exact syntax languages matter first for this project?

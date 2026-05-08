# Chat Assistant and Agent Harness Plan

Research date: 2026-04-29

## Executive summary

Build the editor as the harness, not as a passive text area wrapped by chat. The top-level `EditorHarness` should own workspace state, tabs, documents, search indexes, pending edits, confirmations, rendering invalidation, and the sidebar activity model. The `ChatHarness` should be a child service that asks the editor for scoped context, streams model output into the chat view, and requests actions through a narrow tool boundary.

Use the OpenAI Responses API as the primary endpoint. OpenAI's current docs recommend `gpt-5.5` for complex reasoning and coding, and say latest models are available via the Responses API and client SDKs (https://developers.openai.com/api/docs/models). The Responses API create endpoint is `POST /v1/responses`; it supports text/image input, JSON/text output, custom code tools, and built-in tools (https://developers.openai.com/api/reference/resources/responses/methods/create). For this editor, implement direct HTTP with `fetch` and a small local SSE parser, not an SDK dependency.

Do not start with a complex extension ecosystem. The useful pattern from rxi/lite is a small core with views, documents, cooperative background jobs, incremental syntax highlighting, and project search. Lite's implementation overview says documents, highlighting, layout, and cooperative threads live in the core, with background tasks yielding regularly (https://rxi.github.io/lite_an_implementation_overview.html). Its source keeps views in a binary layout tree with tabs in leaves, which maps well to a simple multi-document editor (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/rootview.lua).

Use GabCode as a useful reference, but do not copy its Chat Completions/tag protocol as the default. GabCode's strongest ideas are the portable core/thin shell split, per-turn tool loop, loop-break guard, read-before-edit discipline, JSONL history, compaction, and browser VFS/session shape (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/README.md). Native Responses API function calling should replace text-tag tools for OpenAI mode.

Recommended first architecture:

1. One workspace, one editor harness, one chat sidebar with multiple chat threads.
2. A small activity bar with `files`, `search`, and `chat`; primary sidebar shows the active view.
3. A central editor area with tabs and optional split groups.
4. Chat tools are read-only by default. File edits and commands become pending actions requiring user confirmation.
5. Context is rebuilt every turn from editor state, not blindly appended forever.
6. Conversation state is persisted locally first. OpenAI Conversations can be added later as an optional remote-state mode.

## Source findings

### rxi/lite

Lite's core design is the right complexity target. The implementation overview describes a core that handles user input, per-frame tasks, and rendering, with cooperative threads for full-document highlighting and project scanning (https://rxi.github.io/lite_an_implementation_overview.html). The source implements project scanning as a coroutine that recursively lists files, sorts dirs/files, updates `core.project_files`, and sleeps between scans (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/init.lua#L15-L74).

Important patterns to adopt:

- Documents are simple line arrays with selection, undo/redo, syntax state, and a highlighter. See `Doc:reset`, `Doc:load`, `Doc:save`, selection helpers, and raw insert/remove invalidation (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/doc/init.lua#L39-L99 and https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/doc/init.lua#L265-L305).
- Syntax highlighting is line-oriented and incremental. The highlighter tokenizes requested lines immediately and uses a coroutine to catch up through wanted lines in chunks of 40 (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/doc/highlighter.lua#L14-L37 and https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/doc/highlighter.lua#L63-L76).
- Tokenization is simple ordered-pattern scanning with range state for multiline strings/comments (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/tokenizer.lua#L41-L96).
- The UI is composed of `View`s inside a `RootView`; leaf nodes can hold multiple views as tabs, while split nodes arrange children horizontally/vertically (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/rootview.lua#L43-L54 and https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/rootview.lua#L295-L375).
- Tree view is just a locked left split with cached project entries, expandable dirs, and open-on-click behavior (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/plugins/treeview.lua#L20-L43 and https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/plugins/treeview.lua#L185-L197).
- Project search runs in a coroutine, yields every 100 lines, and renders a `ResultsView` that opens the selected match in the editor (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/plugins/projectsearch.lua#L25-L61 and https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/plugins/projectsearch.lua#L92-L103).

Takeaway: implement files/search/chat as first-class views, not plugins. Use the same lightweight view lifecycle for UI and the same cooperative job queue for search, indexing, syntax highlighting, and context assembly.

### VS Code sidebar model

VS Code's primary sidebar defaults to the left and contains Explorer, Search, and Source Control views, switchable by Activity Bar icons (https://code.visualstudio.com/docs/configure/custom-layout). VS Code also supports a secondary sidebar for showing chat beside Explorer (same page). We only need the concept, not the customization surface:

- Fixed activity rail: `files`, `search`, `chat`, maybe `settings`.
- One primary sidebar panel at a time.
- Optional later: right secondary sidebar for chat while files/search stays left.

### GabCode

GabCode is directly relevant because it already separates harness concerns from shell/UI concerns. Its README states the core is a prompt builder, tool dispatcher, skill loader, agent runner, and conversation manager, with a portable-core/thin-shell split and browser shell target (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/README.md#L3-L12).

Useful GabCode patterns:

- Tool results are stored as user-role messages after assistant tool calls to keep alternating conversation shape (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/TECHNICAL.md#L188-L238).
- The session API emits text deltas, tool starts/results, turn end, errors, and compaction events (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/TECHNICAL.md#L149-L175).
- The browser `TurnRunner` snapshots messages/history, checks compaction, streams a turn, dispatches tools, guards duplicate calls, and rolls back on cancel (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/web/llm.js#L73-L227).
- The web shell has one `Session` per chat with VFS, config, history, messages, read set, loaded skills, touched-file flag, last token count, and abort signal (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/web/gabcode.js#L17-L32).
- GabCode tools already enforce read-before-edit and unique replacement strings, which should remain part of our safety model (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/web/tools.js#L40-L92).
- GabCode subagents run with restricted tool sets and isolated session state (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/web/agents.js#L13-L37 and https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/web/agents.js#L65-L86).

Differences for our editor:

- Use native Responses API tool calls in OpenAI mode instead of `<tool>...</tool>` text parsing.
- Fold chat context into the editor harness rather than a separate VFS chat workspace.
- Keep skills and subagents optional. We need a stable assistant, not a plugin ecosystem.

### Pi / OpenClaw minimal harness reference

The relevant "Pi" appears to be the minimal coding agent behind OpenClaw. OpenClaw's Pi integration docs describe embedding Pi's `AgentSession` directly instead of spawning a subprocess, gaining lifecycle/event control, custom tool injection, prompt customization, persistence/compaction, and provider switching (https://github.com/openclaw/openclaw/blob/main/docs/pi.md). The same doc identifies the package split: `pi-ai` for LLM abstractions, `pi-agent-core` for agent loop/tool execution, and `pi-coding-agent` for the high-level session SDK (same URL).

Armin Ronacher's Pi writeup emphasizes keeping the harness small, avoiding provider-specific coupling where possible, and persisting custom session messages/state outside model-visible text (https://lucumr.pocoo.org/2026/1/31/pi/).

Takeaway: the assistant should be embeddable and event-driven. The editor should subscribe to agent events and own the display/action policy. Do not create a separate "agent app" inside the editor.

## OpenAI endpoint/API design

Primary endpoint: `POST https://api.openai.com/v1/responses`.

Reasons:

- OpenAI docs say Responses create is the endpoint for model responses and supports custom code tools and built-in tools (https://developers.openai.com/api/reference/resources/responses/methods/create).
- OpenAI docs recommend Responses for multi-turn state handling, and specifically state that Responses is stateful and easier for context across conversations (https://developers.openai.com/api/docs/guides/conversation-state).
- Current model docs recommend `gpt-5.5` for complex reasoning/coding and `gpt-5.4-mini`/`gpt-5.4-nano` for lower latency/cost workloads (https://developers.openai.com/api/docs/models).

Initial request shape, conceptually:

```json
{
  "model": "gpt-5.5",
  "instructions": "You are the editor assistant...",
  "input": [
    {"role": "user", "content": [{"type": "input_text", "text": "..."}]}
  ],
  "tools": [
    {
      "type": "function",
      "name": "read_file",
      "description": "Read a workspace file by relative path.",
      "parameters": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"], "additionalProperties": false}
    }
  ],
  "stream": true,
  "store": false,
  "reasoning": {"effort": "medium"},
  "text": {"verbosity": "low"}
}
```

Endpoint policy:

- Default model: `gpt-5.5` for full agent turns.
- Fast model option: `gpt-5.4-mini` for small summarization, title generation, and non-critical sidebar conveniences.
- Use `reasoning.effort: medium` for normal code assistance, `low` for chatty explanations, `high` only for multi-file planning.
- Use `text.verbosity: low` for sidebar responses by default; allow the prompt to ask for longer explanations.
- Use `stream: true` for all visible chat turns. OpenAI documents Responses streaming over SSE and says streaming can start processing output before the full response is complete (https://developers.openai.com/api/docs/guides/streaming-responses).
- Implement native tool calling. OpenAI documents tool calling as: request with tools, receive tool call, execute code application-side, send tool output, receive final response or more tool calls (https://developers.openai.com/api/docs/guides/function-calling).
- Use `POST /v1/responses/input_tokens` when a precise budget check is needed; the API reference exposes this as "Get input token counts" (https://developers.openai.com/api/reference/resources/responses/subresources/input_tokens).

State policy:

- Phase 1: manually manage local context with `store:false`.
- Phase 2: optionally use `previous_response_id` for short-lived OpenAI-backed continuations. The docs describe `previous_response_id` as a way to chain responses, but it cannot be used with `conversation` and prior tokens still count (https://developers.openai.com/api/reference/resources/responses/methods/create and https://developers.openai.com/api/docs/guides/conversation-state).
- Phase 3: optionally support OpenAI Conversations for remote durable state. The docs say Conversations persist conversation state with durable IDs and store messages/tool calls/tool outputs (https://developers.openai.com/api/docs/guides/conversation-state; https://developers.openai.com/api/reference/resources/conversations).

Why local state first:

- We need deterministic rollback on cancel.
- We need branch/checkpoint support around proposed file edits.
- We need local-only project context and selection snapshots.
- We need to be able to replay turns for debugging.
- We should not make the editor's core behavior depend on remote conversation storage.

## Turn lifecycle

One user-visible chat turn should be a state machine owned by `EditorHarness`, with `ChatHarness` as the model transport/tool loop.

Lifecycle:

1. User submits text from the chat sidebar or invokes an editor command such as "Ask about selection".
2. `EditorHarness` snapshots:
   - active workspace id
   - open tab ids
   - active document revision ids
   - active selection/ranges
   - pending action queue length
   - chat transcript length
3. `ContextCollector` builds a `ContextBundle`.
4. `ChatHarness` starts a `Turn` with status `collecting_context`.
5. Token budget gate runs:
   - cheap estimate first, e.g. chars / 4
   - exact `responses/input_tokens` only when estimate is near threshold
6. `ChatHarness` sends `responses.create` with tool definitions and `stream:true`.
7. SSE events are normalized into internal events:
   - `assistant_text_delta`
   - `assistant_message_done`
   - `tool_call_started`
   - `tool_args_delta`
   - `tool_call_ready`
   - `tool_result_ready`
   - `response_completed`
   - `response_failed`
8. For each tool call:
   - validate schema
   - route through `ToolRouter`
   - execute immediately if read-only
   - create `PendingAction` if mutating
   - send tool output back to the model if execution is complete
9. If a mutating action needs approval, pause the model loop and render a confirmation card.
10. If approved, apply the action through `EditorHarness`, append the tool output, and resume the model loop.
11. If rejected, append a rejection tool output and resume or end.
12. On completion, persist the turn transcript, usage, tool calls, context summary, and any applied actions.
13. On cancel, abort the network stream, restore the turn snapshot for uncommitted model output, and keep explicit applied actions only if already approved.

Turn states:

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

Loop guards:

- Max model round trips per user turn, default 8.
- Max tool calls per turn, default 16.
- Duplicate tool guard: same tool name and same normalized args twice in a row returns a tool error and ends the turn.
- Wall clock timeout for non-visible work.
- Abort signal threaded through fetch, search jobs, and long file reads.

## Context collection from editor

The editor should feed context through explicit layers. Do not dump the whole project.

Context priority:

1. User prompt.
2. Active document path, language id, dirty flag, cursor, selection, and nearby lines.
3. Selected text, if any, with line numbers.
4. Visible ranges from active editor and adjacent split editors.
5. Open tabs summary: path, dirty flag, active cursor, recent edit summary.
6. Project metadata: root path/name, file count, relevant ignored dirs.
7. Recent search results if the search panel is active.
8. Recent assistant actions and pending approvals.
9. Relevant file snippets found by lexical search over project index.
10. Conversation summary and last N turns.

Concrete `ContextBundle`:

```ts
type ContextBundle = {
  workspace: {
    id: string;
    name: string;
    rootDisplayName: string;
    fileCount: number;
  };
  activeEditor?: {
    docId: string;
    path: string;
    languageId: string;
    revision: number;
    dirty: boolean;
    cursor: Position;
    selection?: Range;
    selectedText?: string;
    nearbyText: NumberedTextBlock;
    visibleText: NumberedTextBlock[];
  };
  openTabs: Array<{
    docId: string;
    path: string;
    languageId: string;
    dirty: boolean;
    active: boolean;
  }>;
  search?: {
    query: string;
    mode: "literal" | "regex";
    results: SearchResult[];
  };
  projectHints: {
    filesMentionedByUser: string[];
    symbolsMentionedByUser: string[];
    lexicalMatches: Snippet[];
  };
  conversation: {
    summary?: string;
    recentTurns: ChatMessage[];
  };
  pendingActions: PendingActionSummary[];
};
```

Context formatting should be deterministic:

```text
<workspace name="Slug" files="123">
<active_file path="src/editor.js" language="javascript" revision="42" dirty="true">
<selection start="120:5" end="145:1">
...
</selection>
<nearby_lines start="108">
...
</nearby_lines>
</active_file>
<open_tabs>
- src/editor.js dirty active
- src/renderer.js clean
</open_tabs>
</workspace>
```

Important implementation detail: context collection should be a read-only editor service. The model cannot request arbitrary context directly except through tools, and every context item has a source path/revision so stale edits can be rejected safely.

## Tool/action boundary

Use native function tools, but make a hard distinction between tools and actions.

Tool categories:

- Safe read tools: execute automatically.
- Proposal tools: create a pending action, do not mutate.
- Mutating actions: only the editor can apply them after confirmation.

Initial tool set:

```ts
type ToolName =
  | "list_files"
  | "read_file"
  | "search_text"
  | "get_open_documents"
  | "get_active_selection"
  | "get_document_diagnostics"
  | "propose_file_edit"
  | "propose_create_file"
  | "propose_delete_file"
  | "propose_run_command";
```

Read tools:

- `list_files({glob?, max_results?})`
- `read_file({path, start_line?, end_line?})`
- `search_text({query, regex?, path_glob?, max_results?})`
- `get_open_documents({include_text?: boolean})`
- `get_active_selection({include_surrounding?: boolean})`
- `get_document_diagnostics({path?})`

Proposal tools:

- `propose_file_edit({path, expected_revision, edits})`
- `propose_create_file({path, content})`
- `propose_delete_file({path})`
- `propose_run_command({command, cwd?, reason})`

Do not expose a generic `write_file` tool initially. The model should propose edits, and the editor should render a diff. A direct write tool can exist later behind an explicit "auto apply safe edits" mode.

`propose_file_edit` should use structured ranges, not fragile old/new string replacement, when the target is an open document:

```ts
type TextEdit = {
  range: Range;
  replacement: string;
};
```

For closed files, allow unified-diff-style hunks or exact old/new replacement with read-before-edit. If the file was not read in the current turn, reject the proposal and ask the model to read it first. This borrows GabCode's read-set discipline while avoiding direct writes (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/web/tools.js#L62-L92).

## Streaming UI

Streaming is part of the core UX, not just a transport detail. OpenAI Responses streaming uses typed semantic events, including text deltas and function-call argument deltas (https://developers.openai.com/api/docs/guides/streaming-responses).

UI event model:

```ts
type ChatUiEvent =
  | {type: "turn_started"; turnId: string}
  | {type: "assistant_delta"; turnId: string; text: string}
  | {type: "assistant_message_committed"; turnId: string; messageId: string}
  | {type: "tool_call_started"; turnId: string; callId: string; name: string}
  | {type: "tool_call_args_delta"; turnId: string; callId: string; text: string}
  | {type: "tool_result"; turnId: string; callId: string; ok: boolean; summary: string}
  | {type: "approval_requested"; turnId: string; actionId: string}
  | {type: "usage"; turnId: string; inputTokens: number; outputTokens: number; totalTokens: number}
  | {type: "turn_completed"; turnId: string}
  | {type: "turn_cancelled"; turnId: string}
  | {type: "turn_failed"; turnId: string; error: string};
```

Sidebar rendering:

- User message bubble.
- Streaming assistant bubble.
- Tool call row, collapsed by default.
- Tool result row with status.
- Pending edit card with compact diff.
- Pending command card with command, cwd, reason, and environment.
- Final assistant message.

Do not show raw JSON tool arguments by default. Show a readable summary, with a details expander for debugging.

Cancellation:

- Stop button aborts `fetch`.
- If no approved action has applied, rollback the in-flight assistant/tool messages.
- If an approved action already applied, preserve an audit item and mark the turn cancelled after action.

## Conversation persistence

Persist locally in the workspace metadata directory, e.g. `.slug/chat/`.

Files:

```text
.slug/
  chat/
    threads.json
    threads/
      <thread_id>.jsonl
      <thread_id>.summary.json
      <thread_id>.actions.jsonl
  settings.json
```

JSONL record types:

```ts
type ChatRecord =
  | {t: "user"; id: string; at: number; content: string; contextId: string}
  | {t: "assistant"; id: string; at: number; content: string; phase?: "commentary" | "final_answer"}
  | {t: "tool_call"; id: string; at: number; name: string; args: unknown}
  | {t: "tool_result"; id: string; at: number; callId: string; ok: boolean; output: string}
  | {t: "approval"; id: string; at: number; actionId: string; decision: "approved" | "rejected"}
  | {t: "usage"; id: string; at: number; inputTokens: number; outputTokens: number; totalTokens: number}
  | {t: "summary"; id: string; at: number; content: string};
```

Why JSONL:

- Easy append.
- Easy replay.
- Easy truncation on rollback.
- Easy debugging.
- Matches GabCode's history approach (https://github.com/gszauer/GabCode/blob/718ff5a5d73c50e74421e4627e75e2b860e27b6d/README.md#L41-L44).

Compaction:

- Keep the last few turns verbatim.
- Keep all user messages with applied action decisions.
- Summarize older assistant/tool chatter into a local summary record.
- Keep a separate `actions.jsonl` as immutable audit history.
- When using OpenAI remote state later, preserve local history as the source of truth.

## Safety/confirmation model for file edits/commands

Default policy:

- Read operations: automatic.
- File creation/edit/delete: user confirmation required.
- Shell/commands: user confirmation required.
- Network actions outside OpenAI API: disabled until explicitly enabled.
- Destructive commands: blocked unless a setting enables command proposals and the user approves each one.

Path safety:

- Normalize relative paths against workspace root.
- Reject absolute paths unless the workspace explicitly includes that root.
- Reject `..` escape.
- Reject hidden/system dirs by default: `.git`, `.slug`, `node_modules`, build outputs.
- Reject binary file edits unless a future binary tool explicitly supports them.

Edit safety:

- Every edit proposal includes `expected_revision`.
- Reject if document revision changed since context collection.
- Show diff before apply.
- Apply through the document model, not direct file write, when a document is open.
- Save is separate from applying to an open dirty document unless user has enabled auto-save.

Command safety:

- Represent commands as arrays when possible:

```ts
type CommandProposal = {
  argv: string[];
  cwd: string;
  reason: string;
  expectedDurationMs?: number;
  env?: Record<string, string>;
};
```

- If the model supplies a shell string, display exactly what will run.
- Use a local allow/deny classifier before showing confirmation:
  - allow common read commands: `ls`, `pwd`, `git status`, `npm test` only after confirmation
  - flag destructive forms: `rm`, `mv`, `git reset`, `git checkout`, `chmod`, redirection, pipes, curl-to-shell
  - block background daemons unless launched through an explicit dev-server action
- Capture stdout/stderr and exit code as tool result.

Approval card states:

```text
pending -> applying -> applied
pending -> rejected
pending -> stale
pending -> failed
```

The assistant should not be told that an action succeeded until the editor applies it and records the result.

## Sidebar chat UI

Layout:

```text
+-------------------------------------------------------------+
| activity | sidebar                  | tabs                   |
| rail     |                          | editor group           |
|          | Files/Search/Chat        |                        |
|          |                          |                        |
|          |                          | status bar             |
+-------------------------------------------------------------+
```

Activity rail:

- Files icon: project tree.
- Search icon: project search and replace panel.
- Chat icon: assistant threads.
- Settings icon: model/key/harness settings.

Chat panel sections:

- Thread header: title, model, token/turn status, new chat, settings.
- Messages list: virtualized by message blocks.
- Pending approvals region: pinned above composer when active.
- Context chips: active file, selection, open tabs, search query.
- Composer: text input, send, stop, attach context toggles.

Composer controls:

- "Include selection" toggle, on when a selection exists.
- "Include open tabs" toggle.
- "Allow file edits this turn" toggle.
- "Allow commands this turn" toggle, off by default.
- Model selector in settings, not in primary composer unless needed.

Thread UX:

- Multiple chat threads are useful, but they should be project-scoped and lightweight.
- Do not copy GabCode's "each chat owns its own VFS" for the main editor. Threads share the same workspace; they only own conversation history and pending action state.
- A thread can be linked to a task, file, or selection snapshot.

## Integration with tabs/docs/search

Core editor structures:

- `Workspace`: root, file index, ignore rules, search cache.
- `DocumentStore`: open docs by normalized path, no duplicate document objects for the same file.
- `EditorGroup`: one tab strip and active tab.
- `EditorView`: viewport, cursor, selection, scroll.
- `Sidebar`: activity state and current panel.
- `JobQueue`: cooperative jobs for scanning, search, syntax, context summarization.

Tabs:

- Each tab references a `Document`.
- Multiple views may reference one document in splits.
- Document closes when no tabs/views reference it and it is clean, like lite's polling approach (https://github.com/rxi/lite/blob/38bd9b3326c02e43f244623f97a622b11f074415/data/core/init.lua#L392-L399).
- Dirty close prompts are editor-level, not chat-level.

Search:

- Search panel and chat `search_text` tool should share the same scanner.
- Search jobs yield frequently like lite's project search coroutine.
- Search results are stored with path, line, column, preview, and file revision.
- Chat can ask for search results without forcing the search panel visible.

Syntax highlighting:

- Use lite-style incremental line highlighting for UI.
- The chat context collector may include syntax tokens only if needed later; raw numbered text is enough initially.

Context from search:

- If the user opens chat while search panel has active results, include query and top visible results.
- If the model calls `search_text`, results should be added to conversation context as a tool result and optionally mirrored into the search panel if the call was user-visible.

## Minimal data structures

Editor:

```ts
type Position = { line: number; column: number };
type Range = { start: Position; end: Position };

type Document = {
  id: string;
  path: string | null;
  languageId: string;
  lines: string[];
  revision: number;
  savedRevision: number;
  selections: Range[];
  undo: UndoStack;
  redo: UndoStack;
  highlighter: HighlighterState;
};

type EditorTab = {
  id: string;
  docId: string;
  title: string;
  pinned: boolean;
};

type EditorGroup = {
  id: string;
  tabs: EditorTab[];
  activeTabId: string | null;
  viewStateByDoc: Map<string, EditorViewState>;
};

type Workspace = {
  id: string;
  rootPath: string;
  files: FileIndex;
  documents: Map<string, Document>;
  groups: EditorGroup[];
};
```

Chat:

```ts
type ChatThread = {
  id: string;
  workspaceId: string;
  title: string;
  createdAt: number;
  updatedAt: number;
  model: string;
  localSummary?: string;
  messages: ChatMessage[];
  pendingActions: PendingAction[];
};

type ChatMessage = {
  id: string;
  role: "user" | "assistant" | "tool";
  content: string;
  at: number;
  status?: "streaming" | "complete" | "cancelled" | "error";
  toolCallId?: string;
};

type Turn = {
  id: string;
  threadId: string;
  state: TurnState;
  abort: AbortController;
  snapshot: TurnSnapshot;
  contextId: string;
  responseId?: string;
  usage?: Usage;
};

type PendingAction = {
  id: string;
  turnId: string;
  kind: "edit" | "create_file" | "delete_file" | "run_command";
  summary: string;
  payload: unknown;
  risk: "low" | "medium" | "high";
  status: "pending" | "approved" | "rejected" | "applied" | "failed" | "stale";
  createdAt: number;
};
```

Tools:

```ts
type ToolDef = {
  name: string;
  description: string;
  parameters: JsonSchema;
  mode: "read" | "proposal";
  handler: (ctx: ToolContext, args: unknown) => Promise<ToolResult>;
};

type ToolContext = {
  workspace: Workspace;
  thread: ChatThread;
  turn: Turn;
  readSet: Set<string>;
  contextBundle: ContextBundle;
};

type ToolResult = {
  ok: boolean;
  output: string;
  pendingActionId?: string;
};
```

## Implementation roadmap

Phase 0: pin contracts

- Define `EditorHarness`, `ChatHarness`, `ToolRouter`, `ContextCollector`, and `PendingActionStore` interfaces.
- Define JSON schemas for initial function tools.
- Define local JSONL persistence.

Phase 1: editor harness without model

- Implement workspace file index.
- Implement documents/tabs/groups.
- Implement sidebar activity rail with files/search/chat panels.
- Implement project search shared by UI and future tools.
- Implement fake assistant transport that emits scripted deltas and tool requests.

Phase 2: OpenAI transport

- Implement `ResponsesClient` using `fetch`.
- Implement SSE parser for Responses semantic events.
- Implement request/response logging with secret redaction.
- Implement `responses/input_tokens` budget probe.
- Implement cancel.

Phase 3: read-only assistant

- Tools: `list_files`, `read_file`, `search_text`, `get_open_documents`, `get_active_selection`.
- Context bundle from active file/selection/open tabs.
- Stream assistant output into chat panel.
- Persist local JSONL thread.

Phase 4: edit proposals

- Tool: `propose_file_edit`.
- Diff renderer.
- Approval card.
- Apply edits through open document model.
- Stale revision rejection.
- Audit log.

Phase 5: commands

- Tool: `propose_run_command`.
- Confirmation card.
- Disabled in the browser-only v1 unless a user-configured local companion/proxy is added later.
- Output capture and tool result feedback.

Phase 6: compaction and thread management

- Thread list.
- Local summary generation.
- Automatic compaction when token budget exceeds threshold.
- Optional OpenAI Conversations mode only after local replay is solid.

Phase 7: advanced editor integration

- Chat commands: "explain selection", "fix diagnostics", "search project", "apply last suggestion".
- Inline decorations for proposed edits.
- Optional right secondary chat sidebar.
- Optional subagent for project exploration using only read tools.

## Open questions

- API key strategy: direct user-supplied key for local development, local companion/proxy, or hosted backend. A browser app cannot keep a production key secret.
- Do we want OpenAI-only at first, or an OpenAI-compatible fallback mode? Native Responses tool calling is the right OpenAI path; OpenAI-compatible fallback may require a GabCode-style tag protocol.
- Should chat be on the left activity sidebar only, or should we support a right secondary chat sidebar from the beginning?
- Should approved edits auto-save files, or only update dirty documents?
- Should command execution be entirely omitted from v1, or hidden behind a later local companion/proxy integration?
- Do we want model-generated thread titles, and if so should that use the cheap model path?
- Should project search index include ignored large files if the user explicitly asks, or keep ignore rules absolute?

# Research Agent 3 Source Index

This folder contains source notes for `research/text_editing_ui_plan.md`.

## Local Clones Inspected

- `rxi/lite`, branch `master`, short commit `38bd9b3`
  - Repo: https://github.com/rxi/lite
  - Important files:
    - `data/core/init.lua`: frame loop, project scanning, cooperative threads, document registry.
    - `data/core/doc/init.lua`: line-array document model, single selection, undo/redo.
    - `data/core/docview.lua`: visible-line rendering, gutter, selection, caret, hit testing.
    - `data/core/rootview.lua`: split tree, tabs, locked views, input routing.
    - `data/core/command.lua`, `data/core/keymap.lua`, `data/core/commandview.lua`: command registry, keybindings, command palette.
    - `data/core/doc/highlighter.lua`, `data/core/tokenizer.lua`, `data/core/syntax.lua`: incremental per-line syntax highlighting.
    - `data/plugins/treeview.lua`, `data/plugins/projectsearch.lua`: file tree and project search as small views.

- `lite-xl/lite-xl`, branch `master`, short commit `234fa09`
  - Repo: https://github.com/lite-xl/lite-xl
  - Important files:
    - `data/core/doc/init.lua`: multi-selection model, reload/save, line arrays.
    - `data/core/docview.lua`: visible rendering, syntax-font-aware metrics, IME, multiple carets.
    - `data/core/node.lua`, `data/core/rootview.lua`: modern tabs, tab scrolling, drag/split behavior, locked/resizable panes.
    - `data/core/commandview.lua`: richer command prompt options, suggestions, typeahead.
    - `data/core/doc/highlighter.lua`: incremental highlighter with resume support.
    - `src/rencache.c`, `src/renderer.c`, `src/api/renderer.c`: command-buffer/cache renderer internals.

- `gszauer/GabCode`, branch `main`, short commit `718ff5a`
  - Repo: https://github.com/gszauer/GabCode
  - Important files:
    - `README.md`, `TECHNICAL.md`: core/shell split, tool loop, history, compaction, agents.
    - `core/session.cpp`, `core/session.h`: turn pipeline, tool loop, duplicate-call guard, compaction check.
    - `core/agent_runner.cpp`: restricted sub-agent tool sets.
    - `core/sse_parser.cpp`, `core/stream_consumer.cpp`: no-dependency SSE stream parsing.
    - `web/llm.js`: browser-side turn runner, streaming, cancellation, rollback, tool dispatch.

## Web Sources

- rxi lite implementation overview: https://rxi.github.io/lite_an_implementation_overview.html
- rxi lite repository: https://github.com/rxi/lite
- Lite XL renderer internals: https://lite-xl.com/developer-guide/advanced-topics/how-renderer-works/
- Lite XL command docs: https://lite-xl.com/developer-guide/commands-and-shortcuts/commands/
- Lite XL syntax docs: https://lite-xl.com/developer-guide/syntaxes-and-themes/creating-syntaxes/
- Lite XL user introduction: https://lite-xl.com/user-guide/introduction/
- Lite XL repository: https://github.com/lite-xl/lite-xl
- GabCode repository: https://github.com/gszauer/GabCode
- OpenAI Responses API reference: https://platform.openai.com/docs/api-reference/responses
- OpenAI conversation state guide: https://platform.openai.com/docs/guides/conversation-state
- OpenAI streaming responses guide: https://platform.openai.com/docs/guides/streaming-responses
- OpenAI Responses vs Chat Completions guide: https://platform.openai.com/docs/guides/responses-vs-chat-completions


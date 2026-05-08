# Slug Renderer Research

This folder contains the working research for a no-dependency WebGL2 renderer that can draw both fonts and editor UI using a Slug-style analytic path renderer.

## Research Tracks

- `slug_algorithm_webgl2.md`: Slug algorithm internals, GPU data layout, GLSL ES 3.00 shader plan, path/UI primitive rendering, batching, clipping, and implementation risks.
- `font_ui_editor_architecture.md`: JavaScript TrueType/OpenType parser plan, text shaping/layout scope, code-editor UI architecture, and integration with the renderer.
- `text_editing_ui_plan.md`: Lite/lite-xl-inspired document, editing, rendering, syntax highlighting, tabs/splits, sidebar, files, and search plan.
- `chat_agent_harness_plan.md`: Editor-as-harness and chat assistant architecture using OpenAI Responses API, local context collection, tool/action boundaries, streaming, persistence, and safety confirmations.
- `editor_assistant_synthesis.md`: Short implementation contract reconciling the text editor, sidebar, and assistant harness plans.
- `browser_typescript_platform_plan.md`: Browser TypeScript platform plan covering drag/drop import, IndexedDB VFS, optional File System Access, storage quota, export, and API key constraints.
- `input_clipboard_mobile_dpi_plan.md`: Browser input bridge plan for WebGL text editing, including Ctrl/Cmd shortcuts, copy/cut/paste, hidden textarea, IME, iOS keyboard, visual viewport, and high-DPI WebGL2.

## Primary Source Anchors

- Eric Lengyel, "GPU-Centered Font Rendering Directly from Glyph Outlines", JCGT 2017: https://jcgt.org/published/0006/02/02/
- Eric Lengyel, "A Decade of Slug", March 17, 2026: https://terathon.com/blog/decade-slug.html
- Official Slug reference shaders: https://github.com/EricLengyel/Slug
- Slug product notes and WebGL2 support statement: https://sluglibrary.com/
- Microsoft OpenType specification: https://learn.microsoft.com/en-us/typography/opentype/spec/
- rxi lite implementation overview: https://rxi.github.io/lite_an_implementation_overview.html
- rxi lite repository: https://github.com/rxi/lite
- lite-xl renderer internals: https://lite-xl.com/developer-guide/advanced-topics/how-renderer-works/
- GabCode: https://github.com/gszauer/GabCode
- OpenAI Responses API docs: https://developers.openai.com/api/reference/resources/responses/methods/create
- OpenAI function calling guide: https://developers.openai.com/api/docs/guides/function-calling
- OpenAI streaming guide: https://developers.openai.com/api/docs/guides/streaming-responses
- MDN IndexedDB API: https://developer.mozilla.org/docs/Web/API/IndexedDB_API
- MDN drag data store: https://developer.mozilla.org/docs/Web/API/HTML_Drag_and_Drop_API/Recommended_drag_types
- MDN File System API: https://developer.mozilla.org/en-US/docs/Web/API/File_System_API
- MDN storage quotas and eviction: https://developer.mozilla.org/en-US/docs/Web/API/Storage_API/Storage_quotas_and_eviction_criteria
- MDN KeyboardEvent: https://developer.mozilla.org/en-US/docs/Web/API/KeyboardEvent/
- MDN Clipboard writeText: https://developer.mozilla.org/en-US/docs/Web/API/Clipboard/writeText
- MDN composition events: https://developer.mozilla.org/docs/Web/API/Element/compositionend_event
- MDN VisualViewport: https://developer.mozilla.org/en-US/docs/Web/API/VisualViewport
- Khronos WebGL high-DPI guide: https://www.khronos.org/webgl/wiki/HandlingHighDPI

## Current Target

Produce enough implementation detail to build a complete prototype without runtime third-party libraries:

- Parse selected TrueType/OpenType font data directly in JavaScript.
- Convert glyph outlines and UI paths into quadratic curve data.
- Build curve and band textures for WebGL2.
- Render glyphs and vector UI primitives with analytic coverage in fragment shaders.
- Keep editor architecture small, immediate, and predictable, closer to rxi lite than VS Code internals.

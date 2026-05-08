# Carrot Code

![Screenshot](assets/pwa/carrotcode.png)

* [Run Carrot Code V2](https://gabormakesgames.com/Prototypes/Carrot/index.html)
* [Run Carrot Code V2](https://gabormakesgames.com/Prototypes/CarrotV2/index.html)
* [Run Carrot Code V1](https://gabormakesgames.com/Prototypes/CarrotV1/index.html)

V1 focused on full Unicode rendering with OpenGL. V2 embeds [font8x16](https://github.com/hubenchang0515/font8x16/tree/master) for ASCII-only display with a tiled software renderer.

`carrot.code` is a browser-based code editor, inspired by [lite](https://github.com/rxi/lite). It uses a custom TrueType parser and outline renderer for text/UI, stores the workspace in IndexedDB, and can run as a PWA from a flat static build.

Text and UI are rendered with a  WebGL2 implementation of a Slug-style GPU outline algorithm. Glyphs are parsed from TrueType outlines, converted into quadratic curve data and spatial bands, then evaluated directly in shaders.

## Features

- WebGL2-rendered editor UI with Slug-style outline font/UI rendering.
- Multi-tab and dockable editor panes.
- File tree, project search, find/replace, and workspace zip import/export.
- IndexedDB-backed virtual file system.
- PWA install support with offline app-shell caching.
- Mobile/touch support, including iOS keyboard handling and text selection handles.
- Optional OpenAI-compatible chat assistant with simple browser-emulated tools.
- Configurable themes, font size, UI scale, tab behavior, whitespace display, and AI settings.

## Quick Start

```bash
npm install
npm run build
npm run serve
```

Then open `http://127.0.0.1:4173`.

To test from another machine on your network:

```bash
node scripts/serve.mjs dist 4173 0.0.0.0
```

## Development

```bash
npm run test:unit
npm run test:e2e
npm test
```

`npm run build` creates a self-contained static build in `dist/`. The build is intentionally flat: `index.html`, JavaScript, CSS, fonts, licenses, manifest, service worker, and icons are copied next to each other.

## Deployment

Upload the contents of `dist/` to any static file host. PWA install and service-worker behavior require HTTPS in production; localhost is treated as secure by browsers for development.

The editor workspace is local to the browser profile and stored in IndexedDB. It is not synced to a server unless you export or upload files yourself.

## AI Assistant

The chat panel supports OpenAI-compatible endpoints, including local servers such as LM Studio. Endpoint, model, tool-call format, system prompt, and compaction settings are configured in the Settings sidebar and stored locally.

Chat history is kept in memory for the current UI session unless explicitly exported.

## Assets And Fonts

The application icon source is `assets/pwa/carrotcode.png`; generated PWA icon sizes live beside it.

Bundled fonts keep their own licenses:

- Inter: SIL Open Font License, see `assets/fonts/Inter-LICENSE.txt`.
- Noto Emoji: see `assets/fonts/NotoEmoji-LICENSE.txt`.
- Monaspace Neon: SIL Open Font License, see `assets/fonts/Monaspace-LICENSE.txt`.

## License

The application source is licensed under the MIT License. See `LICENSE`.

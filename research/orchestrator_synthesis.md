# Orchestrator Synthesis: Implementation Contract

This file reconciles the two research tracks into a concrete starting plan for implementation.

Read these first:

1. `slug_algorithm_webgl2.md`
2. `font_ui_editor_architecture.md`
3. This synthesis

## Recommended Initial Scope

Build a browser/WebGL2 JavaScript prototype with no runtime dependencies.

Support first:

- Static TrueType/OpenType fonts with `glyf` quadratic outlines.
- `cmap` formats 4 and 12.
- `head`, `maxp`, `loca`, `glyf`, `hhea`, `hmtx`, `OS/2`, `name`, and optional `kern`.
- LTR code-editor text using direct codepoint-to-glyph mapping and horizontal advances.
- Monospace editor text plus a TrueType UI font.
- Slug-style analytic rendering for text, icons, carets, selections, underlines, squiggles, and rounded UI shapes.
- Rectangular clipping through `gl.scissor`.

Defer:

- CFF/CFF2 cubic outlines.
- Full GSUB/GPOS shaping.
- Bidi editing.
- Complex script shaping.
- Emoji/color glyph composition.
- Variable font axis interpolation.
- TrueType bytecode hinting.

This is enough to build a complete VS Code-like editor prototype for programming text while keeping the renderer and parser tractable.

## Key Decisions

### Renderer Data Formats

Start with:

- Curve texture: `RGBA32F` during validation.
- Production candidate: `RGBA16F` after visual comparison.
- Band texture: `RG16UI`.
- Texture filtering: `NEAREST`.
- Texture wrap: `CLAMP_TO_EDGE`.
- Band texture width: 4096 if `MAX_TEXTURE_SIZE >= 4096`; otherwise largest power of two available.
- Integer metadata: WebGL2 integer vertex attributes via `vertexAttribIPointer`, not bitcast float attributes.

The official Slug HLSL packs integer metadata through float attributes, but WebGL2 should use integer attributes or integer metadata textures.

### Fill And Coverage

Use the Slug fragment approach:

- Horizontal ray coverage.
- Vertical ray coverage.
- Root eligibility lookup equivalent to the official `0x2E74` shader constant.
- Weighted coverage combine.
- Nonzero fill by default.
- Optional even-odd flag later.
- Shader outputs premultiplied color: `vec4(rgb * alpha, alpha)`.
- Blend with `gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA)`.

### Bounding Geometry

Use per-glyph/per-path bounding quads first.

Each vertex carries:

```text
a_posNorm: vec4   // object x,y plus outward miter/normal x,y
a_sample:  vec2   // em/path-space sample coordinate
a_invJac:  vec4   // inverse Jacobian from object delta to sample-space delta
a_band:    vec4   // band scale x,y and offset x,y
a_color:   vec4   // straight input color; shader premultiplies
a_glyph:   uvec4  // band texture x,y, maxBandX, maxBandYAndFlags
```

Later optimization:

- Instanced glyph rendering.
- Tighter convex polygons for large glyphs/icons.
- Solid rectangle fast path only after the unified Slug path is working.

## Data Contract Between Tracks

### Font Parser Output

The font/parser layer should expose normalized glyph outlines in em units:

```js
GlyphOutline {
  glyphId: number,
  bbox: { xMin, yMin, xMax, yMax }, // em units
  advanceWidth: number,             // em units
  leftSideBearing: number,           // em units
  contours: Contour[]
}

Contour {
  segments: Segment[]
}

Segment =
  | { type: "line", p0: Vec2, p1: Vec2 }
  | { type: "quad", p0: Vec2, p1: Vec2, p2: Vec2 }
```

Rules:

- Convert font units to em units by dividing by `unitsPerEm`.
- Preserve contour direction.
- Convert TrueType implied on-curve points before emitting segments.
- Encode a line as a quadratic only in the Slug builder, not in the parser.
- Cache expanded composite glyphs.
- Space and other empty glyphs have advance metrics but no contours.

### Slug Builder Output

The Slug builder consumes `GlyphOutline` or UI `Path` objects and emits:

```js
SlugPathRecord {
  bbox: { xMin, yMin, xMax, yMax },   // path/sample space
  curveBase: { x, y },                // curve texture location
  bandBase: { x, y },                 // band texture location
  hBandCount: number,
  vBandCount: number,
  maxBandX: number,
  maxBandY: number,
  bandScale: [sx, sy],
  bandOffset: [ox, oy],
  fillRule: "nonzero" | "evenodd"
}
```

Curve packing:

```text
curveTex[loc + i].xy = p0
curveTex[loc + i].zw = p1
curveTex[loc + i + 1].xy = p2
```

Line segment conversion:

```text
p0 = start
p1 = end
p2 = end
```

Band packing:

```text
bandBase + 0..hBandCount-1:
  horizontal band headers, RG = (count, offsetFromBandBase)

bandBase + hBandCount..hBandCount+vBandCount-1:
  vertical band headers, RG = (count, offsetFromBandBase)

bandBase + offset:
  curve locations, RG = (curveX, curveY)
```

Band building rules:

- Use an overlap epsilon of `1 / 1024` em/path units initially.
- Exclude fully horizontal curves from horizontal bands.
- Exclude fully vertical curves from vertical bands.
- Sort horizontal band lists by descending max curve x.
- Sort vertical band lists by descending max curve y.
- Enforce `MAX_BAND_CURVES` in the builder, with 128 as the first shader bound.
- If a glyph exceeds the cap, rebuild with more bands before failing.

### Text Layout Output

The layout layer should emit visible glyph instances:

```js
GlyphInstance {
  fontResourceId: number,
  glyphId: number,
  slugRecord: SlugPathRecord,
  x: number,          // device or CSS pixel, depending on renderer convention
  baselineY: number,
  fontPx: number,
  color: [r, g, b, a],
  clipRect: Rect,
  tokenStyleId: number
}
```

For the first editor:

- Skip no-outline glyphs such as spaces after applying their advance.
- Cache per-line glyph positions.
- Cache per-line render instances until text, font, theme, scroll-x, or syntax tokens change.
- For monospaced ASCII, use a fast column path for hit-testing, but still render through glyph instances.

## Implementation Order

### Phase 1: Hardcoded Slug Path

Goal: prove WebGL2 shader and texture layout before touching font parsing.

Deliver:

- WebGL2 context and capability report.
- `RGBA32F` curve texture.
- `RG16UI` band texture.
- One hardcoded closed quadratic path.
- CPU band builder.
- Bounding quad rendering.
- Premultiplied alpha blending.
- Scissor clipping.
- Debug controls for scale and translation.

Done when:

- The path renders cleanly at small, normal, and large scales.
- Horizontal/vertical bands can be visualized or logged.
- Shader output matches a CPU reference for simple samples.

### Phase 2: Font Parser

Goal: extract correct glyph outlines from one permissively licensed static TTF.

Deliver:

- SFNT table directory parser.
- `head`, `maxp`, `hhea`, `hmtx`, `OS/2`, `name`, `cmap`, `loca`, `glyf`.
- `cmap` 4 and 12.
- Simple glyph decode.
- Composite glyph decode with xy offsets and transforms.
- Contour conversion with implied on-curve points.
- Debug outline renderer.

Done when:

- `.notdef`, space, `A`, `a`, `0`, punctuation, and accented composites decode correctly.
- Parser rejects malformed offsets and recursion cycles.

### Phase 3: Text Through Slug

Goal: render visible code text from a real font.

Deliver:

- Lazy glyph-to-Slug processing.
- Glyph resource cache.
- Visible-line layout using `hmtx` advances.
- Text vertex buffer generation.
- Baseline and pixel snapping.
- Token color rendering.

Done when:

- A source-code buffer renders through WebGL2 with no bitmap atlas.
- Cursor and selection rectangles align with glyph positions.
- Scaling text remains sharp.

### Phase 4: Lite-Style Editor Shell

Goal: make the actual editor experience.

Deliver:

- `Core`, `Doc`, `View`, `DocView`, `RootView`, split `Node`, `StatusView`, `CommandView`.
- Immediate redraw-on-dirty model.
- Visible-line virtualization.
- Tabs and splits.
- Scrollbars.
- Basic open/edit/save path depending on host environment.
- Incremental syntax tokenizer.

Done when:

- A file can be edited, scrolled, selected, syntax-colored, and saved.

### Phase 5: UI Vector Assets

Goal: render UI with the same path system.

Deliver:

- `PathBuilder`.
- Rounded rectangles.
- Caret, underline, squiggle.
- Code-defined icons as quadratic paths.
- Static UI vector album.
- Shape cache keyed by path parameters.

Done when:

- The editor chrome no longer depends on browser DOM text or canvas 2D.

## Test Corpus

Use these early:

- Glyphs: `I`, `l`, `H`, `O`, `o`, `e`, `A`, `V`, `W`, `@`, `&`, `%`, `.`, `,`.
- Composite glyph: `é`.
- Text: ASCII source code, tabs, long line, punctuation-heavy line.
- Unsupported markers: Arabic, Indic, emoji ZWJ, RTL sample.
- UI paths: rectangle, rounded rectangle, thin caret, underline, squiggle, chevron icon.

Render tests:

- Sizes: 8, 10, 12, 14, 16, 24, 48, 256 px.
- Fractional positions.
- Nonuniform scale.
- Rotation.
- High-DPI device pixel ratios.

## Immediate Open Questions Before Coding

- Which host is the first target: plain browser, Electron-like shell, or another JavaScript host?
- Which permissively licensed static TrueType fonts should be bundled for editor and UI?
- Is `Intl.Segmenter` acceptable as a built-in browser API for grapheme segmentation, or should v1 ship generated Unicode tables?
- Must the first editor support code ligatures, or can ligatures be disabled by default?
- Are large solid UI panels required to go through Slug from day one, or can a rectangle shader be introduced after the Slug path pipeline is proven?

## Bottom Line

Start by implementing the renderer around a hardcoded path, then add the TTF parser, then connect text layout, then build the lite-style editor shell. The research now contains enough detail to implement the full first prototype without third-party runtime libraries, provided v1 is scoped to TrueType `glyf` fonts and LTR programming text.

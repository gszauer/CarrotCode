# Research Agent 2: Font, Text Layout, and Editor UI Architecture

## Executive Summary

The viable zero-runtime-dependency path is:

1. Implement a small OpenType/TrueType parser in JavaScript focused first on `sfnt` fonts with TrueType `glyf` outlines.
2. Extract glyph outlines as contours of line and quadratic Bezier segments, including composite glyph expansion.
3. Use a deliberately limited shaping engine for the prototype: left-to-right text, direct `cmap` mapping, optional legacy `kern` or minimal GPOS pair kerning, and no full complex-script support.
4. Feed glyph outlines into the Slug-style WebGL2 renderer being researched by the other agent: CPU preprocesses glyph curves and band indices, GPU shades text directly from curves.
5. Keep the editor architecture close to rxi lite/lite-xl: immediate-mode UI, a tiny primitive set, `Doc` objects with line arrays and undo state, `View` objects for panels, a split-tree of nodes/tabs, cooperative background tasks for scanning and syntax highlighting, and rendering only visible lines.

This can produce a complete code-editor prototype without third-party libraries if the bundled/default fonts are chosen to match the supported feature set. A TrueType `glyf` monospace font such as JetBrains Mono or a similar bundled TTF is the best first font because Slug wants quadratic outlines, and TrueType stores quadratic outlines directly. CFF/CFF2 OpenType fonts, variable-font axes, full GSUB/GPOS shaping, bidirectional layout, emoji/color fonts, and all Unicode grapheme behavior should be treated as explicit later phases.

Important boundary: a code editor can be functionally complete before it is a universal text engine. Full Arabic, Indic, complex mark positioning, bidi editing, emoji ZWJ sequences, and advanced code-ligature features are a HarfBuzz-scale project. HarfBuzz itself describes shaping as the step that maps Unicode text to glyph IDs and positions using `cmap`, `GSUB`, and `GPOS`, and notes that complex scripts require script-specific rules and positioning behavior (https://harfbuzz.github.io/why-do-i-need-a-shaping-engine.html, https://harfbuzz.github.io/shaping-concepts.html). We should be honest about this scope from the start.

## Source Baseline

Primary font references:

- OpenType 1.9.1 table directory, required tables, collections, and variation table list: https://learn.microsoft.com/en-us/typography/opentype/spec/otff
- `cmap` mapping formats and subtable selection guidance: https://learn.microsoft.com/en-us/typography/opentype/spec/cmap
- TrueType `glyf` simple/composite outline format: https://learn.microsoft.com/en-us/typography/opentype/spec/glyf
- `loca` offsets into `glyf`: https://learn.microsoft.com/en-us/typography/opentype/spec/loca
- `head` global font metrics, `unitsPerEm`, `indexToLocFormat`: https://learn.microsoft.com/en-us/typography/opentype/spec/head
- `hhea` horizontal header and `numberOfHMetrics`: https://learn.microsoft.com/en-us/typography/opentype/spec/hhea
- `hmtx` advance widths and left side bearings: https://learn.microsoft.com/en-us/typography/opentype/spec/hmtx
- `maxp` glyph count and maximum profile: https://learn.microsoft.com/en-us/typography/opentype/spec/maxp
- `OS/2` typographic metrics, Unicode ranges, `USE_TYPO_METRICS`: https://learn.microsoft.com/en-us/typography/opentype/spec/os2
- `name` multilingual name strings: https://learn.microsoft.com/en-us/typography/opentype/spec/name
- legacy `kern`: https://learn.microsoft.com/en-us/typography/opentype/spec/kern
- GPOS and GSUB: https://learn.microsoft.com/en-us/typography/opentype/spec/gpos and https://learn.microsoft.com/en-us/typography/opentype/spec/gsub
- OpenType Layout common formats: https://learn.microsoft.com/en-us/typography/opentype/otspec180/chapter2
- CFF and CFF2: https://learn.microsoft.com/en-us/typography/opentype/spec/cff and https://learn.microsoft.com/en-us/typography/opentype/spec/cff2
- TrueType fundamentals and winding rule: https://learn.microsoft.com/en-us/typography/opentype/spec/ttch01

Text and Unicode references:

- Unicode grapheme segmentation UAX #29: https://www.unicode.org/reports/tr29/
- Unicode bidirectional algorithm UAX #9: https://www.unicode.org/reports/tr9/
- HarfBuzz shaping overview and limitations to learn from without depending on it: https://harfbuzz.github.io/why-do-i-need-a-shaping-engine.html

Editor and renderer simplicity references:

- rxi lite implementation overview: https://rxi.github.io/lite_an_implementation_overview.html
- lite-xl renderer design: https://lite-xl.com/developer-guide/advanced-topics/how-renderer-works/
- rxi lite source: https://github.com/rxi/lite
- lite-xl source: https://github.com/lite-xl/lite-xl

Slug-style rendering context:

- Eric Lengyel's Slug retrospective: https://terathon.com/blog/decade-slug.html
- JCGT Slug paper: https://jcgt.org/published/0006/02/02/paper-lowres.pdf
- WebGPU Slug implementation notes, useful as a public implementation-oriented reference but not a dependency: https://gabdube.github.io/articles/rust_slug/rust_slug.html
- GPU Gems vector curve rendering background: https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-25-rendering-vector-art-gpu

## Target Scope

### Prototype Font Scope

Support:

- `.ttf` and OpenType fonts with `glyf` and `loca`.
- `cmap` format 12 and format 4. Format 12 is preferred for full Unicode repertoire; format 4 is standard for BMP-only fonts. The OpenType spec says applications should use the 32-bit subtable when both 16-bit and 32-bit Unicode subtables are present (https://learn.microsoft.com/en-us/typography/opentype/spec/cmap).
- Simple and composite TrueType glyphs.
- Horizontal metrics from `hhea` and `hmtx`.
- Default line metrics from `OS/2` `sTypo*` fields when `USE_TYPO_METRICS` is set; otherwise fall back to `hhea`.
- Optional legacy `kern` format 0.
- Optional minimal GPOS pair adjustment lookup type 2 for proportional UI text.
- Optional minimal GSUB ligature lookup type 4 for simple Latin ligatures if needed.

Do not support in phase 1:

- CFF/CFF2 outlines.
- Variable-axis interpolation from `fvar`, `gvar`, `HVAR`, `MVAR`, `avar`, `cvar`, `VVAR`.
- TrueType hint bytecode execution (`fpgm`, `prep`, `cvt `, glyph instructions).
- Full GSUB/GPOS script shaping.
- Vertical text, ruby, math layout, color glyphs, bitmap glyphs, SVG glyphs.
- Unicode variation sequences (`cmap` format 14) except possibly mapping them to the base character.

This is enough for a VS Code-like editor using a bundled monospace TrueType font and a bundled UI TrueType font. If the UI font is also TrueType outline based, one renderer can handle all text.

### Editor Scope

Support:

- Canvas/WebGL2 single-window app.
- Full redraw on demand, with command-buffer batching.
- Visible-line text rendering.
- Tabs, split panes, command palette, status bar, side panel, scrollbars, selections, caret, line numbers, and syntax-highlighted token runs.
- Single cursor/selection first; multiple cursors later.
- LTR code editing first.

Keep the initial design closer to lite than VS Code. rxi's overview states that lite stores loaded files as `Doc` objects containing line tables, undo/redo, syntax, highlighter state, and caret/selection state, and that more than one `Doc` for the same filename should not exist (https://rxi.github.io/lite_an_implementation_overview.html). That pattern is the right level of simplicity for this project.

## No-Dependency Font Parser Design

### Core Data Structures

Use small, explicit classes or plain objects:

```js
class FontReader {
  constructor(arrayBuffer) { this.view = new DataView(arrayBuffer); }
  u8(o) {}
  i8(o) {}
  u16(o) {}       // big-endian
  i16(o) {}
  u32(o) {}
  i32(o) {}
  fixed16_16(o) {}
  f2dot14(o) {}
  tag(o) {}
  bytes(o, n) {}
  assertRange(o, n, label) {}
}

class SfntFont {
  tables = new Map();       // tag -> {offset, length, checksum}
  cmap;
  glyphs;
  metrics;
  names;
  unitsPerEm;
}

class GlyphOutline {
  glyphId;
  xMin; yMin; xMax; yMax;
  advanceWidth;
  leftSideBearing;
  contours = [];            // Array<Contour>
}

class Contour {
  // segments in font units. Slug preprocessing consumes these.
  segments = [];            // {type:"line", p0,p1} or {type:"quad", p0,p1,p2}
}
```

All parsing must be bounds checked. Font files are binary input from users and should be treated as hostile:

- Reject offsets outside the file.
- Reject table lengths that overflow `offset + length`.
- Cap counts before allocation.
- Reject composite glyph recursion cycles.
- Cap composite recursion depth, even though `maxp.maxComponentDepth` may provide a font-declared value.
- Treat missing optional tables as absent, not fatal.
- Treat missing required tables as fatal for the supported outline type.

The OpenType file starts with a table directory. The `sfntVersion` is `0x00010000` for TrueType outlines and `OTTO` for CFF/CFF2 outlines; the directory then lists `numTables` table records (https://learn.microsoft.com/en-us/typography/opentype/spec/otff). If the first tag is `ttcf`, parse the TrueType Collection header and let the caller select one face by index.

### Parser Module Layout

Recommended source modules:

- `font/binary.js`: big-endian reader and range checks.
- `font/sfnt.js`: table directory and collection parsing.
- `font/tables/head.js`
- `font/tables/maxp.js`
- `font/tables/hhea.js`
- `font/tables/hmtx.js`
- `font/tables/os2.js`
- `font/tables/name.js`
- `font/tables/cmap.js`
- `font/tables/loca.js`
- `font/tables/glyf.js`
- `font/tables/kern.js`
- `font/tables/otl.js`: shared Coverage/ClassDef/ScriptList/FeatureList/LookupList helpers.
- `font/layout.js`: mapping text runs to glyph runs.
- `font/font_collection.js`: fallback font manager.

Keep parsing separate from rendering. The font parser should expose normalized outlines, metrics, and shaping tables. It should not know about WebGL.

### Exact Table Parsing Plan

#### Table Directory and Collections

Read:

- `sfntVersion`
- `numTables`
- `searchRange`
- `entrySelector`
- `rangeShift`
- `TableRecord[numTables]`: `tag`, `checkSum`, `offset`, `length`

Do:

- Accept `0x00010000` for TrueType outlines.
- Accept `OTTO` only to report "CFF/CFF2 unsupported in phase 1".
- Accept `ttcf` collection header if needed:
  - `ttcTag`
  - major/minor version
  - `numFonts`
  - `tableDirectoryOffsets[numFonts]`
- Sort/deduplicate table tags defensively.
- Validate every record range.

Required tables for any OpenType font include `cmap`, `head`, `hhea`, `hmtx`, `maxp`, `name`, `OS/2`, and `post`; TrueType outlines additionally use `glyf` and `loca` (https://learn.microsoft.com/en-us/typography/opentype/spec/otff). For the prototype, `post` can be parsed minimally or checked as present.

#### `head`

Read:

- version, checksum fields, magic
- `flags`
- `unitsPerEm`
- created/modified
- font bbox
- `macStyle`
- `lowestRecPPEM`
- `fontDirectionHint`
- `indexToLocFormat`
- `glyphDataFormat`

Use:

- `unitsPerEm` for scaling font units to pixels.
- `indexToLocFormat` for `loca`.
- bbox for initial font-level bounds.

The spec defines `unitsPerEm` as the design grid granularity and `indexToLocFormat` as 0 for short offsets and 1 for long offsets (https://learn.microsoft.com/en-us/typography/opentype/spec/head).

#### `maxp`

Read:

- version
- `numGlyphs`
- for version 1.0, optional max fields.

Use:

- `numGlyphs` to size `loca`, `hmtx` derived arrays, and glyph caches.
- For defensive validation only, use max fields as soft hints, not trusted allocation targets.

OpenType says CFF/CFF2 fonts use maxp version 0.5 with only `numGlyphs`, while TrueType outline fonts use version 1.0 with full data (https://learn.microsoft.com/en-us/typography/opentype/spec/maxp).

#### `hhea`

Read:

- `ascender`
- `descender`
- `lineGap`
- `advanceWidthMax`
- side-bearing extrema
- caret slope fields
- `numberOfHMetrics`

Use:

- `numberOfHMetrics` to parse `hmtx`.
- caret slope fields only if italic caret support is desired later.
- `ascender`, `descender`, `lineGap` as fallback line metrics.

The `hhea` table defines `numberOfHMetrics`, the count of long horizontal metric records in `hmtx` (https://learn.microsoft.com/en-us/typography/opentype/spec/hhea).

#### `hmtx`

Read:

- `LongHorMetric[numberOfHMetrics]`, each `{advanceWidth:uint16, lsb:int16}`
- `leftSideBearings[numGlyphs - numberOfHMetrics]`

Use:

- For glyph ID `< numberOfHMetrics`, use its own `advanceWidth` and `lsb`.
- For glyph ID `>= numberOfHMetrics`, use the last long metric's `advanceWidth` and the extra left-side-bearing array.

The `hmtx` table provides advance widths and left side bearings; if long metrics are fewer than glyphs, the last advance applies to remaining glyph IDs (https://learn.microsoft.com/en-us/typography/opentype/spec/hmtx).

#### `OS/2`

Read minimum:

- version
- `usWeightClass`, `usWidthClass`
- `fsType`
- Unicode range fields
- `fsSelection`
- `usFirstCharIndex`, `usLastCharIndex`
- `sTypoAscender`, `sTypoDescender`, `sTypoLineGap`
- `usWinAscent`, `usWinDescent`
- codepage ranges if present
- `sxHeight`, `sCapHeight` if version supports them

Use:

- If `fsSelection & (1 << 7)` (`USE_TYPO_METRICS`) is set, line height should be `sTypoAscender - sTypoDescender + sTypoLineGap`.
- Use `usWinAscent` and `usWinDescent` for clipping bounds if needed.
- Use `fsType` only to warn about embedding/distribution restrictions if fonts are bundled or persisted.

The OS/2 spec explicitly recommends `sTypo*` fields for portable layout and discourages `usWin*` as default line spacing, while noting `usWin*` are useful for clipping regions (https://learn.microsoft.com/en-us/typography/opentype/spec/os2).

#### `name`

Read:

- version 0 or 1
- count
- storage offset
- `NameRecord[count]`: platform ID, encoding ID, language ID, name ID, length, string offset
- version 1 language tag records if present

Decode:

- Platform 0 and platform 3 Unicode strings as UTF-16BE.
- Platform 1 Macintosh Roman can be ASCII-only initially, or implement a small MacRoman table later.

Use:

- family name, subfamily/style, full name, PostScript name, version string.
- display font names in settings/debug panels.

The `name` table stores multilingual strings and platform-specific encodings; version 1 language tags are UTF-16BE and BCP 47 (https://learn.microsoft.com/en-us/typography/opentype/spec/name).

#### `cmap`

Read:

- version
- `numTables`
- encoding records: platform ID, encoding ID, subtable offset

Select:

1. Platform 0, encoding 4, format 12 if present.
2. Platform 3, encoding 10, format 12.
3. Platform 0, encoding 3, format 4.
4. Platform 3, encoding 1, format 4.
5. Platform 0, encoding 4, format 10 only if easy.
6. Platform 0, encoding 5, format 14 as supplemental variation-sequence table later.

Implement format 12:

- `format:uint16` = 12
- `reserved:uint16`
- `length:uint32`
- `language:uint32`
- `numGroups:uint32`
- groups: `{startCharCode:uint32, endCharCode:uint32, startGlyphID:uint32}`
- Binary search groups by code point. If inside group, glyph ID = `startGlyphID + codePoint - startCharCode`.

Implement format 4:

- `segCount = segCountX2 / 2`
- Read `endCode[segCount]`, reserved pad, `startCode[segCount]`, `idDelta[segCount]`, `idRangeOffset[segCount]`, trailing `glyphIdArray`.
- Binary search first `endCode >= codePoint`.
- If `codePoint < startCode[i]`, return 0.
- If `idRangeOffset[i] == 0`, glyph ID = `(codePoint + idDelta[i]) & 0xffff`.
- Else compute the glyphIdArray address using the spec's offset-from-current-`idRangeOffset` rule, then add `idDelta` if nonzero.

The spec says unmapped characters should map to glyph ID 0, `.notdef`, and that format 4 or 12 are appropriate for most new fonts depending on repertoire (https://learn.microsoft.com/en-us/typography/opentype/spec/cmap).

#### `loca`

Read:

- `numGlyphs + 1` offsets.
- If `head.indexToLocFormat == 0`, each stored uint16 is multiplied by 2.
- If `head.indexToLocFormat == 1`, each stored uint32 is the byte offset.

Use:

- Glyph data start = `glyf.offset + loca[glyphId]`.
- Glyph data length = `loca[glyphId + 1] - loca[glyphId]`.
- If length is zero, glyph has no outline, for example space.

The `loca` table stores offsets into `glyf`, must be ascending, and includes one extra offset after the last glyph (https://learn.microsoft.com/en-us/typography/opentype/spec/loca).

#### `glyf`: Simple Glyphs

Glyph header:

- `numberOfContours:int16`
- `xMin`, `yMin`, `xMax`, `yMax`

If `numberOfContours >= 0`, parse simple glyph:

- `endPtsOfContours[numberOfContours]`
- `instructionLength`
- skip `instructions[instructionLength]`
- point count = last endpoint + 1, or zero
- expanded flags:
  - `0x01` on-curve
  - `0x02` x short
  - `0x04` y short
  - `0x08` repeat
  - `0x10` x same or positive short
  - `0x20` y same or positive short
  - `0x40` overlap simple
- Decode x deltas and y deltas, accumulate to absolute font-unit points.

The spec stores simple glyph coordinates as deltas, with packed flags and optional repeat counts (https://learn.microsoft.com/en-us/typography/opentype/spec/glyf).

Convert points to contours:

- For each contour point range, walk circularly.
- TrueType contours use on-curve and off-curve points.
- If two consecutive off-curve points occur, insert an implied on-curve point at their midpoint.
- Segment rules:
  - on -> on: line
  - on -> off -> on: quadratic
  - off at contour start: synthesize an on-curve start. If previous point is off, start at midpoint(prev, first); if previous is on, start at previous.
- Drop zero-length segments.
- Preserve contour winding; Slug's winding computation can use the contour direction.

The TrueType fundamentals chapter describes outlines as contours and uses nonzero winding for inside/outside tests (https://learn.microsoft.com/en-us/typography/opentype/spec/ttch01). The `glyf` table defines point order and on/off-curve flags; implied points are a standard consequence of consecutive off-curve quadratic controls.

#### `glyf`: Composite Glyphs

If `numberOfContours < 0`, parse component records:

- component flags
- component glyph index
- args:
  - if `ARG_1_AND_2_ARE_WORDS`, two int16/uint16 values
  - else two int8/uint8 values
  - if `ARGS_ARE_XY_VALUES`, interpret as signed x/y offset
  - otherwise interpret as parent/child point numbers
- transform:
  - `WE_HAVE_A_SCALE`: one F2DOT14 scale
  - `WE_HAVE_AN_X_AND_Y_SCALE`: x and y scales
  - `WE_HAVE_A_TWO_BY_TWO`: 2x2 matrix
- loop while `MORE_COMPONENTS`
- if any component has `WE_HAVE_INSTRUCTIONS`, read and skip final instruction block

Expand:

- Recursively load child glyph outline.
- Apply transform and offset.
- For point-number alignment, align selected parent and child points after child transform. Implement this after x/y-offset composites because most Latin accents use x/y offsets; point alignment can be phase 1.5 if it appears in target fonts.
- Append transformed contours.
- Cache expanded result per glyph ID.

Component flags and transform fields are defined in the `glyf` spec, including `USE_MY_METRICS`, overlap flags, and scale/2x2 matrix behavior (https://learn.microsoft.com/en-us/typography/opentype/spec/glyf).

#### `kern`

Parse only Microsoft/OpenType version 0 first:

- version
- `nTables`
- each subtable header: version, length, coverage
- format = high bits from coverage, horizontal/minimum/cross-stream bits as applicable.

Implement:

- Format 0 pair list:
  - `nPairs`
  - search fields
  - pairs: `{left:uint16, right:uint16, value:int16}`
  - Store map key `(left << 16) | right -> value`.

Skip:

- Format 2 class kerning unless needed.
- Apple extended `kern` version 1 for first prototype.

Important rule:

- Prefer GPOS `kern` if we implement it and the font has GPOS kerning for the active script/language. The OpenType recommendations say that when GPOS has `kern` lookups for the resolved language system, GPOS should be used and legacy `kern` ignored; otherwise `kern` may be applied (https://learn.microsoft.com/en-us/typography/opentype/spec/recom).

The `kern` table's format 0 pair records are glyph-index pairs and signed FWORD values (https://learn.microsoft.com/en-us/typography/opentype/spec/kern).

#### GPOS

Reason to implement:

- UI text with proportional fonts benefits from pair kerning.
- Some modern fonts store kerning only in GPOS, not `kern`.

Minimal target:

- Parse ScriptList, FeatureList, LookupList using OpenType Layout common formats.
- Resolve script:
  - `latn` for Latin if present.
  - `DFLT` fallback.
  - default language system.
- Resolve feature `kern`.
- Implement Lookup Type 2 Pair Adjustment:
  - Format 1 glyph-pair positioning.
  - Format 2 class-pair positioning.
- Implement extension lookup type 9 enough to unwrap to type 2.
- Apply xAdvance adjustment from ValueRecord. Ignore y placement for horizontal LTR prototype.

The GPOS spec says PairPos lookup type 2 adjusts placement or advances of two glyphs and can use glyph-pair format 1 or class-pair format 2 (https://learn.microsoft.com/en-us/typography/opentype/spec/gpos).

Defer:

- Mark-to-base, mark-to-ligature, cursive, contextual positioning.
- Device/variation indexes.
- Right-to-left direction behavior.

#### GSUB

Reason to implement:

- Optional ligatures in code fonts.
- Some UI fonts have standard Latin ligatures.

Minimal target:

- Parse ScriptList, FeatureList, LookupList.
- Resolve `liga`, `clig`, optionally `calt` only if implementing context lookup.
- Implement Lookup Type 4 Ligature Substitution.
- Implement Lookup Type 1 Single Substitution for simple substitutions.
- Implement extension lookup type 7.

Defer:

- Contextual and chained contextual substitution types 5 and 6 unless code ligatures are a requirement.
- Reverse chaining.
- Script-specific shaping.

The GSUB spec lists lookup type 4 as ligature substitution and type 1 as single substitution; contextual types are separate lookup types and significantly more complex (https://learn.microsoft.com/en-us/typography/opentype/spec/gsub).

Pragmatic decision: disable ligatures by default in the first editor build. Many code editors expose `"editor.fontLigatures"` as a preference; we can add the preference before adding the full shaping behavior.

#### CFF and CFF2

Detect:

- If `CFF ` or `CFF2` table exists and no `glyf`, report unsupported in phase 1.

Why defer:

- CFF uses Type 2 CharStrings, cubic Beziers, stack operators, subroutines, private dictionaries, and width logic.
- CFF2 changes the structure for variable fonts and uses blending operations.
- Slug's easiest path is TrueType quadratic curves. CFF's cubic curves would need either a cubic-capable Slug path or robust cubic-to-quadratic conversion.

The OpenType CFF page says the table contains a Compact Font Format font and Type 2 CharStrings (https://learn.microsoft.com/en-us/typography/opentype/spec/cff). The CFF2 page says CFF2 describes glyph outlines using cubic Bezier curves and can include variation blend operations (https://learn.microsoft.com/en-us/typography/opentype/spec/cff2).

Recommended boundary:

- Phase 1: reject CFF/CFF2 with clear error.
- Phase 2: implement CFF charstring parser only if we need `.otf` fonts.
- Phase 3: implement CFF2/variations only after the renderer and editor are already useful.

#### Variation Tables

Default handling:

- If variable tables are present, render the default instance only.
- Use base `glyf` outlines and base `hmtx` metrics.
- Ignore axis selection.

Later support:

- `fvar`: axis definitions and named instances.
- `avar`: axis normalization mapping.
- `gvar`: glyph point deltas, including phantom points for metrics.
- `HVAR`: advance/side-bearing deltas.
- `MVAR`: line metrics deltas.
- `STAT`: style names for axis locations.
- `CFF2`: blend operators for CFF2 outlines.

OpenType lists variation tables separately and notes that CFF2 can also include variation data (https://learn.microsoft.com/en-us/typography/opentype/spec/otff).

#### `post`

Minimal parse:

- version
- italic angle
- underline position
- underline thickness
- fixed-pitch flag

Use:

- underline/strikethrough drawing if we want font-native underline metrics.
- monospace detection hint.

Do not rely on glyph names for rendering.

#### Ignored Hinting Tables

Ignore:

- `cvt `
- `fpgm`
- `prep`
- glyph instruction bytes
- `gasp`

Reason:

- Slug is outline-based and antialiases in the shader.
- Running TrueType bytecode is a large VM project.
- We can revisit hinting only if small text is unacceptable.

Risk:

- Small text may look softer or less grid-fit than platform text. We can mitigate with font choice, supersampling/dynamic dilation from the Slug side, and using sizes that align well with the font.

## Glyph Outline Extraction Algorithm

### Simple Glyph Pseudocode

```js
function parseSimpleGlyph(reader, glyfOffset, numberOfContours) {
  const endPts = readU16Array(numberOfContours);
  const instructionLength = reader.u16(p); p += 2;
  p += instructionLength;

  const pointCount = numberOfContours === 0 ? 0 : endPts[numberOfContours - 1] + 1;
  const flags = [];
  while (flags.length < pointCount) {
    const flag = reader.u8(p++);
    flags.push(flag);
    if (flag & 0x08) {
      const repeat = reader.u8(p++);
      for (let i = 0; i < repeat; i++) flags.push(flag);
    }
  }

  const xs = [];
  let x = 0;
  for (let i = 0; i < pointCount; i++) {
    const f = flags[i];
    let dx = 0;
    if (f & 0x02) {
      const v = reader.u8(p++);
      dx = (f & 0x10) ? v : -v;
    } else if (f & 0x10) {
      dx = 0;
    } else {
      dx = reader.i16(p); p += 2;
    }
    x += dx;
    xs.push(x);
  }

  const ys = [];
  let y = 0;
  for (let i = 0; i < pointCount; i++) {
    const f = flags[i];
    let dy = 0;
    if (f & 0x04) {
      const v = reader.u8(p++);
      dy = (f & 0x20) ? v : -v;
    } else if (f & 0x20) {
      dy = 0;
    } else {
      dy = reader.i16(p); p += 2;
    }
    y += dy;
    ys.push(y);
  }

  return buildContours(endPts, flags, xs, ys);
}
```

### Contour Conversion Pseudocode

```js
function contourToSegments(points) {
  if (points.length === 0) return [];

  const n = points.length;
  const expanded = [];

  for (let i = 0; i < n; i++) {
    const curr = points[i];
    const next = points[(i + 1) % n];
    expanded.push(curr);
    if (!curr.on && !next.on) {
      expanded.push({
        x: (curr.x + next.x) * 0.5,
        y: (curr.y + next.y) * 0.5,
        on: true,
        implied: true
      });
    }
  }

  // Rotate to an on-curve point.
  let start = expanded.findIndex(p => p.on);
  if (start < 0) return [];
  const pts = expanded.slice(start).concat(expanded.slice(0, start));

  const segments = [];
  let i = 0;
  while (i < pts.length) {
    const p0 = pts[i];
    const p1 = pts[(i + 1) % pts.length];
    if (p1.on) {
      segments.push(line(p0, p1));
      i += 1;
    } else {
      const p2 = pts[(i + 2) % pts.length];
      segments.push(quad(p0, p1, p2));
      i += 2;
    }
  }
  return segments.filter(nonDegenerate);
}
```

### Scaling

Keep outlines in font units until Slug preprocessing. For layout:

```js
const pxScale = fontSizePx / unitsPerEm;
const xPx = xFontUnits * pxScale;
const yPx = yFontUnits * pxScale;
```

Use a baseline coordinate system:

- Font coordinates: y up.
- Screen coordinates: y down.
- Glyph instance transform should include y inversion.
- Baseline y = line top + ascentPx.

## Shaping and Layout Scope

### Text Pipeline

For a line or token run:

1. Decode JS string into Unicode code points, preserving source offsets.
2. Segment into grapheme clusters for cursor movement.
3. Split into style runs from syntax highlighting.
4. Split each style run into font fallback runs.
5. Map code points to glyph IDs with selected font's `cmap`.
6. Optionally apply GSUB.
7. Apply advances and kerning/GPOS.
8. Emit positioned glyphs:
   - `glyphId`
   - `fontId`
   - x/y offset
   - advance
   - source cluster start/end
   - color/style
9. Cache the shaped result using a line version and font/style key.

### Grapheme Clusters

Use grapheme clusters for:

- cursor left/right
- backspace/delete
- selection hit testing
- avoiding cursor positions inside combining sequences or emoji sequences

UAX #29 defines extended grapheme cluster boundaries and requires property-based rules or a declared profile (https://www.unicode.org/reports/tr29/). Implementation options:

- Phase 1: use JavaScript `Intl.Segmenter` when available, with a fallback that handles ASCII, combining marks, CRLF, and surrogate pairs.
- Phase 2: generate compact Unicode property tables from UCD at build time and ship them as static source data. This keeps runtime dependencies at zero.
- Phase 3: tailor emoji ZWJ sequences and regional indicators.

Do not normalize source text automatically. A code editor should display bytes/text faithfully. Normalization can change code.

### Direction and Bidi

Phase 1:

- Treat all lines as LTR.
- Render RTL code points via fallback glyph mapping but without visual reordering.
- Cursor movement is logical order.

Phase 2:

- Implement UAX #9 for visual order and cursor hit testing. UAX #9 is the official Unicode bidirectional algorithm (https://www.unicode.org/reports/tr9/).
- Store both logical clusters and visual runs per line.

Reason to defer:

- Bidi editing is not just rendering. Selection rectangles, hit testing, cursor affinity, deletion, and mixed-direction line wrapping all need consistent mapping.

### Fallback Fonts

A WebGL renderer cannot use CSS font fallback directly because it needs the font file outlines. The app needs one of:

- Bundled fallback TTF files.
- User-selected font files.
- Optional browser local font access where available and permissioned, behind a feature flag.

Fallback algorithm:

```js
function chooseFontForCodePoint(primary, fallbacks, cp) {
  if (primary.cmap.get(cp)) return primary;
  for (const font of fallbacks) {
    if (font.cmap.get(cp)) return font;
  }
  return primary; // render glyph 0 or configured missing-box glyph.
}
```

Rules:

- Spaces and tabs use primary font metrics to preserve editor columns.
- Missing glyph uses glyph ID 0 from primary, or U+25A1 from a fallback if present.
- Line height is derived from primary editor font, but clipping should include max fallback ascent/descent among visible fallback glyphs.
- Cache fallback decisions by code point and font stack.

lite-xl's renderer uses a `FontGroup` fallback approach: it searches fonts for a glyph, uses the first font for whitespace, and falls back to a box glyph if needed in source (https://github.com/lite-xl/lite-xl/blob/master/src/renderer.c).

### Kerning

Use cases:

- Editor code font: usually monospaced; kerning can be disabled for stable columns.
- UI font: kerning should be enabled for menus, tabs, command palette, status bar labels.

Implementation:

- `fontFeatures.kerning = false` for editor text area unless user opts into proportional rendering.
- `fontFeatures.kerning = true` for UI text.
- Apply GPOS `kern` if implemented and available.
- Else apply legacy `kern` format 0 if available.

Never apply kerning across font fallback boundaries in phase 1.

### Ligatures

Use cases:

- Code ligatures (`=>`, `!=`, `===`) are optional.
- UI standard ligatures (`fi`, `fl`) are optional but nice.

Implementation boundary:

- Phase 1: no ligatures.
- Phase 2: GSUB lookup type 4 for simple `liga`.
- Phase 3: GSUB context/chained context for programming ligatures that rely on `calt`.

Store cluster mapping carefully. A ligature glyph may represent multiple source clusters. Cursor movement inside ligatures requires either:

- caret stops distributed by component advances, or
- disabling intra-ligature caret positions and treating the ligature as one cluster.

For a code editor, the simpler and more predictable choice is to disable ligatures until caret behavior is designed.

### Line Layout

Use a line cache:

```js
class LineLayout {
  lineIndex;
  docVersion;
  fontVersion;
  text;
  clusters;       // logical source ranges and x positions
  runs;           // shaped glyph runs
  width;
  tabStops;
}
```

Only layout visible lines plus a small overscan. For horizontal scrolling, still layout the full visible line unless lines are extremely long; add chunked layout later for huge minified files.

Tabs:

- Primary editor font space advance defines tab cell width.
- `tabWidthPx = spaceAdvancePx * indentSize`.
- At current x, tab advance = `tabWidthPx - ((x + tabOrigin) % tabWidthPx)`, with a minimum of one space advance. lite-xl uses this style of next-stop calculation in its renderer source (https://github.com/lite-xl/lite-xl/blob/master/src/renderer.c).

Selection:

- For same-line selection, x1 = cluster boundary at selection start, x2 = cluster boundary at selection end.
- Multi-line selection:
  - first line from start boundary to line width or viewport width
  - middle lines full text area width
  - last line from text origin to end boundary
- Draw selection rectangles before text.

Caret:

- x = cluster boundary at caret offset.
- y = line top.
- height = line height.
- width = theme caret width.
- Blink controlled by app clock; redraw only on blink transitions.

Hit testing:

- Convert pointer y to line index using line height and scroll.
- Convert x to cluster index using binary search over cluster boundaries.
- If inside a glyph/cluster, snap to previous or next boundary based on midpoint.

rxi lite's `DocView` computes visible line ranges from scroll and line height, measures text prefixes for column x offsets, draws selection rectangles before text, and draws the caret as a rectangle (https://github.com/rxi/lite/blob/master/data/core/docview.lua). That is exactly the model to copy, replacing prefix measurement with cached cluster boundaries.

## Editor UI Architecture Inspired by rxi lite

### Keep the App Immediate-Mode

lite-xl's renderer documentation says its renderer only draws rectangles and text, and the original lite overview says the Lua side behaves as if it redraws everything while a lower layer can cache/clip regions (https://lite-xl.com/developer-guide/advanced-topics/how-renderer-works/, https://rxi.github.io/lite_an_implementation_overview.html). For WebGL2, use the same mental model:

- UI code issues draw commands every frame that is dirty.
- Renderer batches commands.
- WebGL draws the command buffer.
- The app avoids retained widget trees beyond editor views and panels.

Core primitive API:

```js
renderer.beginFrame(width, height, devicePixelRatio);
renderer.pushClip(x, y, w, h);
renderer.rect(x, y, w, h, color);
renderer.text(fontStack, text, x, y, color, options);
renderer.popClip();
renderer.endFrame();
```

Avoid a general layout engine initially. Use explicit rectangles, split panes, and simple stacks.

### Core Objects

```js
class Core {
  docs = [];
  rootView;
  commandView;
  statusView;
  activeView;
  threads = [];
  redraw = true;
}

class Doc {
  filename;
  lines = [""];
  selections = [new Selection()];
  undoStack;
  redoStack;
  syntax;
  highlighter;
  version;
}

class View {
  position = {x: 0, y: 0};
  size = {w: 0, h: 0};
  scroll = {x: 0, y: 0};
  update(dt) {}
  draw(renderer) {}
  onMouseDown(e) {}
  onTextInput(text) {}
}

class DocView extends View {
  doc;
  drawLine(index, x, y) {}
}

class Node {
  type;           // "leaf", "hsplit", "vsplit"
  a; b;
  divider = 0.5;
  views = [];
  activeView;
}
```

This mirrors lite's `Doc`, `View`, `DocView`, and split `Node` structure. In rxi lite source, `Node:split` turns a leaf into an hsplit/vsplit with child nodes and views; `RootView` owns the root node (https://github.com/rxi/lite/blob/master/data/core/rootview.lua).

### Cooperative Work

Use small async tasks for:

- project file scanning
- syntax highlighting
- search
- font loading and glyph preprocessing
- diagnostics/indexing later

In lite, `core.add_thread` creates Lua coroutines and long-running work yields periodically; the overview calls out project scanning and incremental syntax highlighting as examples (https://rxi.github.io/lite_an_implementation_overview.html). In JavaScript:

```js
class TaskScheduler {
  tasks = [];
  add(generator) { this.tasks.push(generator); }
  runBudget(ms) {
    const deadline = performance.now() + ms;
    while (performance.now() < deadline && this.tasks.length) {
      const task = this.tasks.shift();
      const result = task.next();
      if (!result.done) this.tasks.push(task);
    }
  }
}
```

For CPU-heavy font preprocessing, use a Web Worker later. The no-dependency requirement does not forbid workers; workers are browser primitives. Phase 1 can run on the main thread with small batches because glyphs are lazy.

### Document Storage

Phase 1:

- Array of lines.
- Each edit mutates one or more lines.
- Undo stack stores inverse operations.
- Version per document and per line.

Phase 2:

- Piece table or rope if large-file editing requires it.

The line-array model is simple and matches lite. It is good enough for a prototype and small/medium code files.

### Syntax Highlighting

Phase 1:

- Regex/tokenizer per language.
- Incremental by line.
- Highlighter stores per-line state and invalidates from changed line forward.
- Draw token runs with different colors.

lite/lite-xl performs incremental syntax highlighting in a cooperative task; lite-xl's `Highlighter:start` processes a bounded number of lines and yields (https://github.com/lite-xl/lite-xl/blob/master/data/core/doc/highlighter.lua).

### Command Palette

Implement as a `CommandView` that reuses `DocView` logic for a single-line input. rxi lite does this with a `SingleLineDoc` subclass and `CommandView` extending `DocView` (https://github.com/rxi/lite/blob/master/data/core/commandview.lua).

Benefits:

- One text input path.
- One cursor/selection renderer.
- Same font layout code.

### UI Layout

Base regions:

- optional title bar
- activity/sidebar
- editor split area
- panel area
- status bar
- command palette overlay
- context menu overlay

No nested card UI. This is an operational tool. Use dense, restrained surfaces:

- separators are 1 px rects
- tabs are flat rectangles
- selection/current-line/caret are rectangles
- scrollbars are rectangles
- icons from vector primitives or an icon font parsed by the same TTF path

## How Font Rendering Plugs into Slug-Style WebGL2

### Data Flow

```text
ArrayBuffer font file
  -> sfnt parser
  -> cmap + metrics + glyph outline extraction
  -> GlyphOutline contours
  -> Slug glyph builder
       - normalize curves
       - compute glyph bounds
       - build horizontal/vertical band lists
       - pack curve data and band data
  -> WebGL2 textures/buffers
  -> text layout emits glyph instances
  -> fragment shader computes coverage from outline curves
```

The public Slug references describe rendering directly from Bezier curve data without rasterized atlases or distance fields (https://terathon.com/blog/decade-slug.html). The JCGT paper states that glyphs are rendered directly from Bezier data extracted from TrueType fonts, with no precomputed images or distance fields (https://jcgt.org/published/0006/02/02/paper-lowres.pdf). The WebGPU writeup usefully frames the shader inputs as outline curves, a spatial index using bands, and glyph bounds (https://gabdube.github.io/articles/rust_slug/rust_slug.html).

### Font Parser Output Needed by Slug Builder

Per font:

```js
{
  fontId,
  unitsPerEm,
  ascender,
  descender,
  lineGap,
  glyphCount,
  glyphs: LazyGlyphStore
}
```

Per glyph:

```js
{
  glyphId,
  advanceWidth,
  leftSideBearing,
  bounds: {xMin, yMin, xMax, yMax},
  contours: [
    [
      {type:"line", p0:{x,y}, p1:{x,y}},
      {type:"quad", p0:{x,y}, p1:{x,y}, p2:{x,y}}
    ]
  ]
}
```

Slug builder should be the only module that knows:

- band count and band size
- curve record encoding
- texture packing format
- shader constants
- dynamic dilation/supersampling policy

### WebGL2 Resource Strategy

Use three logical data stores:

- Glyph records:
  - bounds
  - curve offset/count
  - horizontal band offset/count
  - vertical band offset/count
  - unitsPerEm or scale metadata if needed
- Curve records:
  - quadratic control points in font units or normalized glyph units
  - flags/class data required by shader
- Band records:
  - per band: offset/count into curve index list
  - curve index list

Use texture buffers conceptually, but WebGL2 does not have OpenGL texture buffer objects. Practical WebGL2 options:

- Pack into 2D textures and fetch with `texelFetch`.
- Start with `RGBA32F` textures for simplicity. Store integer indices as floats only while counts stay below exact integer range.
- Later switch band/index data to integer textures (`usampler2D`) if compatibility testing confirms the target browsers and devices handle the formats consistently.

Instance buffer per glyph draw:

```js
struct GlyphInstance {
  float x;
  float y;
  float fontSizePx;
  float rgbaOrColorIndex;
  uint glyphRecordIndex;
  uint flags;
}
```

Because WebGL2 instancing supports vertex attributes but not arbitrary structs, pack into float attributes initially:

- `vec4 a_pos_size_color0`
- `vec4 a_color1_glyph_flags`

or use separate attributes:

- `a_origin`
- `a_size`
- `a_glyphIndex`
- `a_color`

Use one quad per glyph instance. The vertex shader expands the quad to the glyph bounds plus padding needed for antialiasing/dilation. The fragment shader maps the fragment to font/glyph space and evaluates coverage.

### Text and UI Batching

Renderer command types:

- `RectCommand`
- `TextCommand`
- `ClipCommand`
- `PathCommand` later for non-text vector UI shapes

Frame build:

1. UI emits immediate commands.
2. Text commands call layout cache and append glyph instances.
3. Renderer sorts minimally:
   - preserve order around clips and transparency.
   - batch consecutive rects.
   - batch consecutive glyph instances sharing font data textures/shader.
4. Draw rect pass.
5. Draw text pass, or interleave if ordering requires.

For editor UI, rectangles and text cover almost everything. lite-xl's renderer documentation explicitly says it only draws rectangles and text (https://lite-xl.com/developer-guide/advanced-topics/how-renderer-works/). Add arbitrary vector paths only after text is solid.

## UI Rendering Primitives Needed

Minimum:

- Solid rectangle.
- 1 px separators/borders via rectangles.
- Text runs.
- Scissor/clip rectangles.
- Caret rectangle.
- Selection rectangles.
- Scrollbar track/thumb rectangles.
- Tab backgrounds and active indicators.
- Current-line background.
- Command palette overlay.

Useful phase 2:

- Rounded rectangle with small fixed radius, either shader SDF rect or vector path.
- Underline and strikethrough using font metrics or simple rectangles.
- Wavy underline for diagnostics.
- Icon primitive set:
  - triangles/arrows
  - plus/x/check
  - chevron
  - file/folder
  - search/settings

Icon options:

1. Implement icons as code-defined vector paths. This keeps dependencies at zero and uses the same Slug/path renderer if generalized.
2. Bundle a tiny in-house icon TTF and parse it like any other font.
3. Avoid icon fonts until later and use text labels. This is less polished.

For a VS Code-like tool, option 1 is best long-term.

## Performance Notes

### Font Loading

- Parse font tables once per font file.
- Build cmap lookup structures eagerly.
- Build metrics arrays eagerly.
- Decode glyph outlines lazily on first use.
- Build Slug glyph data lazily on first draw.
- Prewarm ASCII 32-126 for the editor font and common UI labels.

### Layout

- Cache per-line layout by `(docLineVersion, fontStackVersion, tabWidth, featureFlags)`.
- For syntax-highlighted lines, cache tokenization separately from shaping.
- Layout only visible lines plus overscan.
- Monospace fast path:
  - For editor primary font, if all glyph advances are equal for ASCII range, compute x positions by columns for plain ASCII spans.
  - Still fall back to shaped positions for tabs, non-ASCII, fallback fonts, and ligatures.

### Rendering

- Append glyph instances to typed arrays.
- Grow buffers geometrically.
- Avoid per-glyph JS object allocation in hot draw paths.
- Use dirty rendering:
  - redraw on input, scroll, resize, cursor blink, highlighter progress, font load.
  - otherwise do not request animation frames.
- Use scissor for views and line clipping.
- Use device-pixel-ratio aware coordinates.

### Slug Data Cache

Cache keys:

- `fontId:glyphId` for outlines and Slug data.
- If variation axes are added later, include normalized axis tuple.
- If hinting is ever added, include ppem. Without hinting, glyph curve data is size-independent.

Eviction:

- Editor fonts use small active glyph sets; cache can be mostly permanent.
- For CJK, use LRU for Slug glyph data if memory grows too high.

### Long Lines

Long minified lines can dominate layout. Mitigations:

- Build cluster advances incrementally in chunks.
- Cache prefix advance every N clusters.
- Only emit glyph instances that overlap horizontal viewport plus padding.
- Use binary search in cluster x array to find visible glyph range.

## Risks and Decision Boundaries

### Full Unicode Shaping

Risk:

- Correct shaping for Arabic, Indic scripts, Thai, marks, emoji, and mixed-direction text is far larger than TTF outline parsing.

Boundary:

- Phase 1 is a code editor for LTR programming text.
- Add clear unsupported-script behavior.
- Do not claim universal text rendering until UAX #9, UAX #29, GSUB, GPOS, and script-specific models are implemented.

### CFF/CFF2

Risk:

- Many `.otf` fonts use CFF/CFF2 cubic outlines, not `glyf`.

Boundary:

- Bundle TrueType-outline fonts.
- Reject CFF/CFF2 initially with a diagnostic.

### Variable Fonts

Risk:

- Variable fonts may render default instance acceptably, but selected weights/widths need `gvar`, `HVAR`, and `MVAR`.

Boundary:

- Use static TTFs first.
- If variable font is loaded, render default instance only.

### Small Text Quality

Risk:

- No TrueType hinting can make small text less crisp.

Boundary:

- Choose fonts that render well unhinted.
- Use Slug antialiasing, optional supersampling/dynamic dilation from the renderer research.
- Test at 11-16 px on common DPI scales.

### WebGL2 Format Compatibility

Risk:

- Integer/float texture format support and precision vary by browser/GPU.

Boundary:

- Start with simple formats and a runtime capability test.
- Add a debug view that renders glyph bounds, band counts, and missing texture fetches.

### Security

Risk:

- Font parsing bugs can crash or hang the app.

Boundary:

- Bounds checks everywhere.
- Count caps.
- Composite recursion guard.
- No eval or dynamic code generation from font data.
- Fuzz with malformed fonts later.

### Font Licensing

Risk:

- `OS/2.fsType` embedding bits may restrict redistribution or embedding.

Boundary:

- Bundle only permissively licensed fonts.
- Treat user-loaded fonts as local user data.

## Implementation Roadmap

### Phase 0: Test Fixtures and Debug Harness

Deliver:

- Load a TTF ArrayBuffer.
- Parse table directory and required table presence.
- Dump font names, unitsPerEm, glyph count, cmap subtable choice.
- Add tests with one known bundled TTF.

Done when:

- Parser rejects malformed offsets.
- `.notdef`, space, `A`, `a`, `0`, newline handling are observable in debug output.

### Phase 1: TrueType Parser and Outlines

Deliver:

- `head`, `maxp`, `hhea`, `hmtx`, `OS/2`, `name`, `cmap`, `loca`, `glyf`.
- Format 4 and 12 cmap lookup.
- Simple glyph decode.
- Composite glyph decode with x/y offsets and scale transforms.
- Contour conversion to line/quadratic segments.
- Debug SVG/canvas path renderer for glyph outlines, used only in tests/dev.

Done when:

- ASCII glyphs from the chosen editor font render as correct outlines in a debug view.
- Accented composite glyphs render.
- Space has advance but no outline.

### Phase 2: Basic Layout

Deliver:

- Font stack with fallback.
- LTR codepoint-to-glyph layout.
- Metrics scaling.
- Tabs.
- Line height and baseline.
- Selection/caret cluster boundaries for ASCII and surrogate pairs.
- Optional legacy `kern` format 0.

Done when:

- A line of source code can be measured and hit-tested.
- Cursor x matches rendered glyph positions.
- UI labels can be measured.

### Phase 3: Slug WebGL2 Text Path

Deliver:

- CPU glyph-to-Slug data builder integration point.
- WebGL2 texture packing for glyph records, curves, bands.
- Glyph instance buffer and shader.
- Rect shader.
- Debug overlays for glyph bounds and baseline.

Done when:

- Editor font ASCII renders on the canvas without bitmap atlas.
- Text scales cleanly at multiple zoom levels.
- Selection and caret align with glyph layout.

### Phase 4: Lite-Style Editor Shell

Deliver:

- `Core`, `Doc`, `View`, `DocView`, `RootView`, `Node`, `CommandView`, `StatusView`.
- Single document open/edit/save if filesystem context allows, or browser file open/download if web-only.
- Tabs and splits.
- Scrollbars.
- Command palette.
- Theme object.

Done when:

- A file can be edited with visible-line rendering, cursor movement, selection, scrolling, and syntax coloring.

### Phase 5: Incremental Highlighting and Caches

Deliver:

- Cooperative scheduler.
- Incremental tokenizer.
- Per-line layout cache.
- Lazy glyph preprocessing.
- Dirty redraw scheduling.

Done when:

- Large files remain interactive while highlighting catches up.
- Scrolling through unseen glyphs does not cause long stalls.

### Phase 6: Typography Improvements

Deliver in this order:

1. GPOS pair kerning type 2 for UI text.
2. GSUB simple ligatures type 4, disabled for editor by default.
3. Grapheme segmentation table fallback beyond `Intl.Segmenter`.
4. Better fallback font line clipping.
5. Optional code ligatures with contextual GSUB only if required.

Done when:

- UI proportional font looks acceptable.
- Editor remains column-stable.

### Phase 7: Broader Font Support

Deliver if needed:

- CFF Type 2 CharString parser.
- Cubic-to-quadratic conversion or cubic Slug support.
- Variable font default-instance detection and selected-axis support.
- `cmap` format 14 variation sequences.
- Bidi UAX #9.

This phase should not block the prototype.

## Recommended Initial File/Module Tree

```text
src/
  font/
    binary.js
    sfnt.js
    font.js
    layout.js
    fallback.js
    tables/
      cmap.js
      glyf.js
      head.js
      hhea.js
      hmtx.js
      kern.js
      loca.js
      maxp.js
      name.js
      os2.js
      post.js
      otl.js
  renderer/
    renderer.js
    rect_batch.js
    text_batch.js
    slug_builder.js
    slug_shader.glsl
    rect_shader.glsl
  editor/
    core.js
    doc.js
    view.js
    doc_view.js
    root_view.js
    node.js
    command_view.js
    status_view.js
    scheduler.js
    highlighter.js
    tokenizer.js
  app.js
```

## Unresolved Questions

- What exact Slug data layout will Research Agent 1 choose for WebGL2 textures: float textures, integer textures, or packed normalized textures?
- Does the target app run purely in browser, Electron-like shell, or another JS host? This affects file I/O and local font access.
- Which fonts will be bundled for editor text and UI text, and are they static TrueType `glyf` fonts with permissive licenses?
- Is code ligature support required for the first complete prototype, or can it be a setting added later?
- What Unicode support level is acceptable for v1: ASCII/Latin code editing, broad BMP fallback, or correct complex scripts?
- Should the editor support proportional editor fonts, or enforce monospaced fonts for the main text area?
- Does "UI with Slug" mean only font/icons, or also arbitrary vector paths for rounded controls and icons?

## Recommended Next Research Tasks

- Coordinate with Research Agent 1 on the exact CPU output required for Slug glyph records, curve records, band records, and shader-side coordinate normalization.
- Test two candidate TrueType fonts: one monospace editor font and one proportional UI font. Verify tables present, glyph count, cmap formats, and whether kerning is in `kern` or GPOS.
- Prototype only the table directory, `cmap`, `hmtx`, `loca`, and `glyf` parsing for one bundled TTF, then compare extracted outlines against a known renderer visually.
- Decide whether `Intl.Segmenter` is acceptable as a built-in runtime API for grapheme segmentation, or whether static generated Unicode tables are required from day one.
- Build a small corpus for layout tests: ASCII code, tabs, long lines, combining marks, emoji, Cyrillic/Greek, CJK fallback, and RTL samples marked expected-unsupported for phase 1.
- Define the editor's text feature flags: kerning on/off by surface, ligatures on/off, fallback policy, and missing glyph policy.

## Files Changed by This Agent

- `research/font_ui_editor_architecture.md`

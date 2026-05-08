# Slug Algorithm in WebGL2 With Zero Runtime Dependencies

Research Agent 1 report. Scope: Slug-style glyph and vector UI rendering, GPU data structures, shader math, CPU preprocessing, WebGL2 porting constraints, batching, clipping, transforms, and implementation risks. This is written for a future browser-based code editor with a Visual Studio Code-like UI surface.

## Executive Summary

Slug renders filled shapes directly from quadratic Bezier outlines on the GPU. It does not rasterize glyphs into an image atlas, and it does not use signed distance fields. The public Slug page describes the runtime as drawing each glyph's bounding geometry and using a shader to evaluate coverage from original quadratic outline data stored in GPU textures (https://sluglibrary.com/). The 2017 JCGT paper gives the core algorithm: robust winding-number evaluation from quadratic glyph contours, antialiasing by analytic coverage, and acceleration through horizontal and vertical "bands" of curve references (https://jcgt.org/published/0006/02/02/paper.pdf). In 2026, Eric Lengyel dedicated the patent to the public domain and published reference HLSL shaders under permissive terms (https://terathon.com/blog/decade-slug.html and https://github.com/EricLengyel/Slug).

For WebGL2, the algorithm is viable with no runtime dependencies. WebGL2 exposes an OpenGL ES 3.0 style API (https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext), GLSL ES 3.00 integer arithmetic and bit operations, unsigned integer samplers, `texelFetch`, `fwidth`, `flat` varyings, and integer vertex attributes through `vertexAttribIPointer` (https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext/vertexAttribIPointer and https://www.khronos.org/files/webgl20-reference-guide.pdf). The port should not copy the reference HLSL vertex format literally because it bitcasts packed integers through float attributes. In WebGL2, use real integer attributes or metadata textures instead.

The practical design is:

- CPU parses fonts and vector paths into contours of quadratic Bezier curves in em/object space.
- CPU builds two GPU textures per font/vector album:
  - `curveTexture`: `RGBA16F` or debug `RGBA32F`, one texel per curve start, holding start and control points.
  - `bandTexture`: `RG16UI`, per-glyph band headers plus curve-location lists.
- CPU emits either bounding quads or optional tighter convex polygons for visible glyphs/UI paths.
- Vertex shader applies model transform and dynamic half-pixel dilation so all edge pixels are rasterized.
- Fragment shader maps the pixel to em/object-space sample coordinates, selects one horizontal and one vertical band, loops only over curves in those bands, solves ray/curve intersections, accumulates signed fractional coverage, combines the two axis coverages, and outputs alpha coverage.

No stencil buffer is needed for ordinary glyph/UI rendering. Stencil can still be useful for complex clipping masks, but Slug's inside/outside and antialiasing are fragment-shader coverage operations, not stencil winding operations.

For a VS Code-like editor, start with:

- TrueType-flavored OpenType `.ttf` support via a small JavaScript parser for `head`, `maxp`, `loca`, `glyf`, `cmap`, `hhea`, `hmtx`, and optionally `OS/2`.
- Monospaced Latin/code-text layout first: `cmap` character mapping, `hmtx` advances, pixel-snapped baselines, and a glyph cache containing processed curve/band data.
- Slug path rendering for text, icons, rounded rectangles, carets, selection shapes, underlines, and diagnostic squiggles. Keep a trivial solid-rectangle fast path available later if performance requires it, but implement the Slug path pipeline first so UI and font rendering share the same renderer.

Major risk areas are CPU preprocessing correctness, band-building edge cases, half-float precision, small-text quality without hinting, premultiplied-alpha handling, and full text shaping. Slug itself ignores font hinting; the reference README suggests cap-height-aligned font sizes to improve vertical grid alignment (https://github.com/EricLengyel/Slug).

## Source Map

Primary sources used:

- Eric Lengyel, "GPU-Centered Font Rendering Directly from Glyph Outlines", JCGT 2017: https://jcgt.org/published/0006/02/02/paper.pdf
- Slug public overview and rendering description: https://sluglibrary.com/
- Slug reference shaders and README: https://github.com/EricLengyel/Slug
- 2026 algorithm update, public-domain patent announcement, and dynamic dilation derivation: https://terathon.com/blog/decade-slug.html
- 2019 dynamic glyph dilation background: https://terathon.com/blog/glyph-dilation.html
- Slug User Manual, especially texture formats, vector graphics, and rendering resources: https://sluglibrary.com/SlugManual.pdf
- Microsoft OpenType specs for TTF parsing: `glyf`, `cmap`, `loca`, `hmtx`, `head`, `maxp`:
  - https://learn.microsoft.com/en-us/typography/opentype/spec/glyf
  - https://learn.microsoft.com/en-us/typography/opentype/spec/cmap
  - https://learn.microsoft.com/en-us/typography/opentype/spec/loca
  - https://learn.microsoft.com/en-us/typography/opentype/spec/hmtx
  - https://learn.microsoft.com/en-us/typography/opentype/spec/head
  - https://learn.microsoft.com/en-us/typography/opentype/spec/maxp
- WebGL2 and GLSL ES capability references:
  - https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext
  - https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext/vertexAttribIPointer
  - https://www.khronos.org/files/webgl20-reference-guide.pdf

Local copies of the JCGT paper and reference shaders are stored under `research/slug_algorithm_sources/`.

## Known Slug Algorithm Details

### What Is Publicly Known

The public Slug materials establish these concrete implementation facts:

- Shapes are stored as quadratic Bezier outlines and evaluated directly on the GPU (https://sluglibrary.com/).
- A glyph can be drawn as a single bounding quad, with an optional tighter bounding polygon optimization (https://sluglibrary.com/).
- Runtime rendering binds a vertex buffer plus two textures: one for Bezier curve data and one for spatial data structures (https://sluglibrary.com/).
- The Slug User Manual says the curve texture is either 4x16-bit or 4x32-bit float, while the band texture is 2x16-bit unsigned integer (https://sluglibrary.com/SlugManual.pdf).
- The official reference README says the curve texture packs `(x1, y1, x2, y2)` in one texel and uses the next texel's first two channels for `p3`; connected curves share an endpoint texel when possible (https://github.com/EricLengyel/Slug).
- The official reference README says bands should overlap by a small epsilon such as `1/1024` em, and curves in each horizontal/vertical band must be sorted by descending maximum coordinate for early termination (https://github.com/EricLengyel/Slug).
- The JCGT paper describes a robust winding-number method based on a three-control-point sign classification and a 16-bit lookup value `0x2E74` (https://jcgt.org/published/0006/02/02/paper.pdf).
- The modern reference pixel shader uses two rays, one horizontal and one vertical, and combines fractional coverage from both (https://github.com/EricLengyel/Slug/blob/main/SlugPixelShader.hlsl).
- The 2026 update says the old band-split optimization and supersampling path have been removed from modern Slug, and dynamic dilation has been added to the vertex shader (https://terathon.com/blog/decade-slug.html).
- Dynamic dilation expands the bounding polygon by the amount needed for a half-pixel viewport-space margin, recalculated per vertex using the MVP matrix and viewport dimensions (https://terathon.com/blog/decade-slug.html).
- The patent is now dedicated to the public domain, and the reference shaders are available in the official repository (https://terathon.com/blog/decade-slug.html and https://github.com/EricLengyel/Slug).

### Inferred Or Non-Public Gaps

The public sources are enough to implement an equivalent renderer, but not everything in the commercial Slug implementation is specified:

- Exact `.slug` binary format is not needed. We should define our own JS-native asset format and GPU packing.
- Exact band-count optimizer is not specified. The README only gives constraints and hints. We need our own heuristic.
- Exact tight bounding polygon generation is not specified. Start with quads, then add a conservative convex polygon pass later.
- Exact compression/decompression of commercial `.slug` textures is not relevant. We can upload raw typed arrays.
- Exact color emoji/layer handling evolved since the paper. The 2026 post says independent layer rendering became preferable to looping over layers in the pixel shader (https://terathon.com/blog/decade-slug.html). Implement ordinary monochrome/vector color first.
- Exact stroke expansion algorithms for arbitrary vector paths are outside the core shader. Slug can render strokes, but our CPU path builder must create filled outlines for strokes.
- Full OpenType shaping is outside the core Slug algorithm. A code editor can start with direct `cmap` mapping and `hmtx` advances for monospaced fonts, but complete Unicode text requires GSUB, GPOS, bidi, combining marks, variation selectors, and script-specific shaping.

## Core Algorithm

### Coordinate Spaces

Use these spaces explicitly:

- Font design units: integer coordinates from `glyf`.
- Em space: font design coordinates divided by `unitsPerEm`. The `head` table provides `unitsPerEm` (https://learn.microsoft.com/en-us/typography/opentype/spec/head).
- Object space: UI/editor coordinates before projection. For text, object space can equal em space after a per-run scale and translation.
- Clip space: `u_mvp * vec4(object.xy, 0, 1)`.
- Pixel/viewport space: after perspective divide and viewport scale.

The fragment shader receives `renderCoord`, the sample coordinate in em/path space. The curve data is also in that space. For text drawn in a 2D editor, the transform is usually orthographic and affine, but implementing the full projective version costs little once the dynamic dilation math is ported.

### Quadratic Curve Representation

Each filled contour is a sequence of quadratic Bezier curves:

```text
C(t) = (1 - t)^2 p0 + 2t(1 - t) p1 + t^2 p2, where t is in [0, 1].
```

The curve texture stores one texel per curve start:

```text
curveTex[start + i].xy = p0 of curve i
curveTex[start + i].zw = p1 of curve i
curveTex[start + i + 1].xy = p2 of curve i
```

For a contour with `n` curves, store `n + 1` texels. The extra final texel is needed so the last curve can read `p2` at `loc + 1`. For connected curves, `curveTex[start + i + 1].xy` is also the next curve's `p0`, matching the official packing tip in the reference README (https://github.com/EricLengyel/Slug).

Straight line segment from `a` to `b`:

```text
p0 = a
p1 = b
p2 = b
```

The reference README recommends this duplicated-endpoint representation for a line (https://github.com/EricLengyel/Slug). It is also convenient because it preserves the shared endpoint convention.

### Winding And Root Eligibility

For a horizontal ray from the sample point in the positive x direction, subtract the sample coordinate from each control point. The ray crosses a curve where the y component of the curve is zero:

```text
a = y0 - 2*y1 + y2
b = y0 - y1
c = y0
solve a*t^2 - 2*b*t + c = 0
```

Naively checking whether `t` is in `[0, 1)` is numerically fragile near endpoints, which is the core problem Slug solves. The JCGT paper replaces this with root eligibility based only on the sign class of `y0`, `y1`, and `y2`, reducing all cases to eight classes and a small lookup (https://jcgt.org/published/0006/02/02/paper.pdf).

In WebGL2, implement the modern shader's sign-bit form:

```glsl
uint rootCode(float v0, float v1, float v2) {
    uint s0 = floatBitsToUint(v0) >> 31u;
    uint s1 = floatBitsToUint(v1) >> 30u;
    uint s2 = floatBitsToUint(v2) >> 29u;

    uint shift = (s1 & 2u) | (s0 & ~2u);
    shift = (s2 & 4u) | (shift & ~4u);

    return (0x2E74u >> shift) & 0x0101u;
}
```

This is equivalent in structure to the reference shader but should be verified against CPU golden tests for all sign classes, including exact-zero cases. The low bit indicates the first root contributes. The high selected bit means the second root contributes. For the horizontal ray, the first contributing root adds coverage and the second subtracts. For the vertical ray, the signs are swapped to keep winding orientation consistent with the axis change.

The roots still need to be solved to know how far the intersection lies from the pixel center along the ray. The eligibility code decides whether each root matters; it does not replace root solving.

### Fractional Coverage Antialiasing

Slug computes coverage analytically around the pixel footprint. After a root is solved, the intersection coordinate along the ray is scaled by `pixelsPerEm`:

```glsl
vec2 emsPerPixel = fwidth(renderCoord);
vec2 pixelsPerEm = 1.0 / emsPerPixel;
```

For the horizontal ray, the root x positions become pixel-relative distances. A root at `-0.5` pixel contributes 0, a root at the sample center contributes 0.5, and a root at `+0.5` pixel contributes 1:

```glsl
float contribution = clamp(rootDistancePixels + 0.5, 0.0, 1.0);
float weight = clamp(1.0 - abs(rootDistancePixels) * 2.0, 0.0, 1.0);
```

The same happens along y for the vertical ray. The two signed coverages are combined with weights. The modern reference shader uses a weighted average guarded by a minimum fallback and then applies either nonzero or optional even-odd fill behavior (https://github.com/EricLengyel/Slug/blob/main/SlugPixelShader.hlsl).

Important detail: the output is coverage, not distance. Output alpha should be:

```glsl
alpha = pathAlpha * coverage;
rgb = pathRgb * alpha;  // premultiplied output
```

Use premultiplied blending:

```js
gl.enable(gl.BLEND);
gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
```

This avoids double-multiplying color when rendering translucent UI.

### Horizontal And Vertical Bands

A brute-force fragment shader would test every curve in the glyph for every pixel in the glyph bounding box. Slug avoids this with bands:

- Horizontal bands partition the y range. A sample in a horizontal band only needs curves whose y extent intersects that band.
- Vertical bands partition the x range. A sample in a vertical band only needs curves whose x extent intersects that band.

The JCGT paper describes band data as the acceleration structure consumed by the shader (https://jcgt.org/published/0006/02/02/paper.pdf). The modern README adds important implementation requirements: slight band overlap, sorted curve lists, and excluding straight horizontal curves from horizontal bands and straight vertical curves from vertical bands (https://github.com/EricLengyel/Slug).

Each fragment computes:

```glsl
ivec2 bandIndex = clamp(
    ivec2(renderCoord * v_bandScale + v_bandOffset),
    ivec2(0),
    ivec2(maxBandX, maxBandY)
);
```

Then it fetches:

- Horizontal band header at `glyphBandLoc + bandIndex.y`.
- Vertical band header at `glyphBandLoc + horizontalBandCount + bandIndex.x`.

Each header stores:

```text
uint16 count
uint16 offsetFromGlyphBandLoc
```

The list at that offset contains `count` curve texture coordinates as `u16 x, u16 y`.

Early-out:

- Horizontal list sorted by descending maximum curve x coordinate.
- If the curve's max x lies left of the pixel by more than half a pixel, later curves cannot contribute, so break.
- Vertical list sorted by descending maximum y coordinate with equivalent logic.

### Dynamic Dilation

The GPU rasterizer only runs the fragment shader for pixels whose centers are inside the submitted triangles. A tight glyph bbox would miss pixels partly covered by the glyph but whose centers lie just outside the bbox. The 2019 dilation article explains the problem and the need for half-pixel expansion (https://terathon.com/blog/glyph-dilation.html). The 2026 post gives the full dynamic formula and says modern Slug computes the optimal dilation in the vertex shader from the MVP matrix and viewport dimensions (https://terathon.com/blog/decade-slug.html).

For a 2D orthographic editor, CPU-side expansion by `0.5` pixel is enough. Still, implement dynamic dilation in the vertex shader so the renderer also supports scaled, rotated, and projected surfaces.

Per vertex, store:

```text
pos.xy: object-space vertex
normal.xy: object-space outward miter vector for expanding adjacent polygon edges
tex.xy: em/path-space sample coordinate
jac: inverse 2x2 Jacobian from object-space displacement to em/path-space displacement
```

The vertex shader:

1. Normalizes `normal.xy` to get the unit dilation direction.
2. Uses MVP rows 0, 1, and 3 plus viewport dimensions to solve for the object-space displacement equivalent to half a viewport pixel.
3. Moves `pos.xy` by the miter vector times the scalar displacement.
4. Offsets the sample coordinate by applying the inverse Jacobian to the same displacement so the visual shape remains the original size while only the bounding geometry expands.

If the transform is pure 2D orthographic, the formula simplifies, but keep the general path:

```glsl
vec2 dynamicDilate(vec4 posNorm, vec2 tex, vec4 invJac0_1_2_3,
                   mat4 mvpRows, vec2 viewportPx,
                   out vec2 objectPosDilated) {
    vec2 n = normalize(posNorm.zw);

    // Row-vector style values shown conceptually.
    float s = dot(mvpRow3.xy, posNorm.xy) + mvpRow3.w;
    float t = dot(mvpRow3.xy, n);

    float u = (s * dot(mvpRow0.xy, n) -
               t * (dot(mvpRow0.xy, posNorm.xy) + mvpRow0.w)) * viewportPx.x;
    float v = (s * dot(mvpRow1.xy, n) -
               t * (dot(mvpRow1.xy, posNorm.xy) + mvpRow1.w)) * viewportPx.y;

    float s2 = s * s;
    float st = s * t;
    float uv = u * u + v * v;
    float denom = max(uv - st * st, 1e-20);

    vec2 delta = posNorm.zw * (s2 * (st + sqrt(uv)) / denom);
    objectPosDilated = posNorm.xy + delta;

    mat2 invJ = mat2(invJac0_1_2_3.x, invJac0_1_2_3.z,
                     invJac0_1_2_3.y, invJac0_1_2_3.w);
    return tex + invJ * delta;
}
```

The exact row/column convention must be made consistent with our JS matrices. The official derivation assumes row variables `m00`, `m01`, etc. from the transform that maps object-space glyph positions to clip space (https://terathon.com/blog/decade-slug.html).

## Concrete WebGL2 Implementation Design

### Runtime Modules

Recommended modules:

- `font/SfntReader.js`: safe big-endian binary reader and table directory parser.
- `font/TrueTypeGlyphParser.js`: `glyf`/`loca` simple and composite outline extraction.
- `font/Cmap.js`: Unicode code point to glyph ID mapping, formats 4 and 12 first.
- `font/Metrics.js`: `head`, `hhea`, `hmtx`, `OS/2` metrics.
- `slug/PathBuilder.js`: move/line/quad/cubic/close API for font and UI paths.
- `slug/CurvePacker.js`: packs contours into `curveTexture`.
- `slug/BandBuilder.js`: creates per-path horizontal/vertical bands.
- `slug/MeshBuilder.js`: emits bbox quads first, optional tight polygons later.
- `render/SlugRenderer.js`: WebGL2 resources, shader programs, batching, blending, and clipping.
- `editor/TextRunBuilder.js`: converts visible editor lines into glyph draw instances.

All are ordinary JavaScript modules using `DataView`, typed arrays, and WebGL2 APIs. No runtime dependencies are required.

### GPU Texture Formats

Curve texture:

```js
gl.texImage2D(
  gl.TEXTURE_2D,
  0,
  gl.RGBA16F,
  width,
  height,
  0,
  gl.RGBA,
  gl.HALF_FLOAT,
  halfFloatData
);
```

Fallback/debug:

```js
gl.RGBA32F / gl.FLOAT
```

Use `RGBA32F` while validating the pipeline, then switch to `RGBA16F` and compare screenshots. The Slug manual says Slug can use 4x16-bit or 4x32-bit floating-point curve texture data (https://sluglibrary.com/SlugManual.pdf).

Band texture:

```js
gl.texImage2D(
  gl.TEXTURE_2D,
  0,
  gl.RG16UI,
  width,
  height,
  0,
  gl.RG_INTEGER,
  gl.UNSIGNED_SHORT,
  uint16Data
);
```

Fragment shader declaration:

```glsl
uniform highp sampler2D u_curveTex;
uniform highp usampler2D u_bandTex;
```

Sampler state for both:

```js
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
```

Do not generate mipmaps. Integer textures cannot be filtered like normalized color textures. The Khronos quick reference lists unsigned integer sampler types and WebGL2 GLSL integer support (https://www.khronos.org/files/webgl20-reference-guide.pdf).

Texture width:

- The official reference shader assumes 4096 texels wide and uses `log2(width) = 12` for wrapping offsets.
- WebGL2 should query `gl.MAX_TEXTURE_SIZE`.
- If `MAX_TEXTURE_SIZE >= 4096`, use 4096 for compatibility with the reference layout.
- If not, choose the largest power of two available and set `u_logBandTextureWidth`.
- Store all locations as `(x, y)` u16 pairs. With width up to 4096 and reasonable height, u16 coordinates are enough. If larger texture arrays are needed later, use two-level font pages rather than u32 band coordinates.

### Vertex Formats

Avoid the official HLSL `asuint(tex.zw)` packing in WebGL. WebGL's float vertex conversion can canonicalize NaN-like bit patterns and cannot be trusted as a raw bit transport. Use integer attributes.

Simple CPU-expanded vertex format:

```text
attribute 0: vec4 a_posNorm   // object x,y and outward normal/miter x,y
attribute 1: vec2 a_sample    // em/path sample coordinate
attribute 2: vec4 a_invJac    // inverse Jacobian entries
attribute 3: vec4 a_band      // band scale x,y and offset x,y
attribute 4: vec4 a_color     // premultiplied or straight input color
attribute 5: uvec4 a_glyph    // band x, band y, maxBandX, maxBandYAndFlags
```

Set attributes:

```js
gl.vertexAttribPointer(posNormLoc, 4, gl.FLOAT, false, stride, offPosNorm);
gl.vertexAttribPointer(sampleLoc, 2, gl.FLOAT, false, stride, offSample);
gl.vertexAttribPointer(invJacLoc, 4, gl.FLOAT, false, stride, offInvJac);
gl.vertexAttribPointer(bandLoc, 4, gl.FLOAT, false, stride, offBand);
gl.vertexAttribPointer(colorLoc, 4, gl.FLOAT, false, stride, offColor);
gl.vertexAttribIPointer(glyphLoc, 4, gl.UNSIGNED_SHORT, stride, offGlyph);
```

`vertexAttribIPointer` is the WebGL2 API that lets shader integer inputs receive integer data instead of converted floats (https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext/vertexAttribIPointer).

Use `flat out` for glyph and band data:

```glsl
flat out uvec4 v_glyph;
flat out vec4 v_band;
```

The Khronos WebGL2 guide lists `flat` as the no-interpolation qualifier in GLSL ES 3.00 (https://www.khronos.org/files/webgl20-reference-guide.pdf).

Optional later optimization: instanced glyph rendering. A static six-vertex quad supplies corner IDs, while per-instance attributes supply glyph position, scale, color, and metadata. The vertex shader derives bbox corners from glyph metrics. This reduces editor vertex upload, but it needs glyph metrics in attributes or a metadata texture available to the vertex shader. Start with CPU-expanded vertices for clarity.

### Vertex Shader Pseudocode

```glsl
#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in vec4 a_posNorm;
layout(location = 1) in vec2 a_sample;
layout(location = 2) in vec4 a_invJac;
layout(location = 3) in vec4 a_band;
layout(location = 4) in vec4 a_color;
layout(location = 5) in uvec4 a_glyph;

uniform mat4 u_mvp;
uniform vec2 u_viewportPx;

out vec2 v_sample;
out vec4 v_color;
flat out vec4 v_band;
flat out uvec4 v_glyph;

void main() {
    vec2 pDilated;
    v_sample = dynamicDilate(a_posNorm, a_sample, a_invJac,
                             u_mvp, u_viewportPx, pDilated);

    gl_Position = u_mvp * vec4(pDilated, 0.0, 1.0);

    v_color = a_color;
    v_band = a_band;
    v_glyph = a_glyph;
}
```

For an initial 2D-only build, this can be replaced by a simpler half-pixel orthographic dilation. Keep the same attributes so the general version can drop in without changing CPU data.

### Fragment Shader Pseudocode

```glsl
#version 300 es
precision highp float;
precision highp int;
precision highp usampler2D;

uniform sampler2D u_curveTex;
uniform usampler2D u_bandTex;
uniform int u_logBandTextureWidth;

in vec2 v_sample;
in vec4 v_color;
flat in vec4 v_band;
flat in uvec4 v_glyph;

out vec4 outColor;

float slugRender(vec2 sampleCoord, vec4 bandTransform, uvec4 glyph) {
    vec2 emsPerPixel = fwidth(sampleCoord);
    vec2 pixelsPerEm = 1.0 / max(emsPerPixel, vec2(1e-20));

    ivec2 maxBand = ivec2(int(glyph.z), int(glyph.w & 0x00FFu));
    ivec2 bandIndex = clamp(
        ivec2(sampleCoord * bandTransform.xy + bandTransform.zw),
        ivec2(0),
        maxBand
    );

    ivec2 glyphLoc = ivec2(int(glyph.x), int(glyph.y));

    float xCoverage = renderHorizontalBand(glyphLoc, bandIndex.y, sampleCoord, pixelsPerEm.x);
    float yCoverage = renderVerticalBand(glyphLoc, maxBand.y, bandIndex.x, sampleCoord, pixelsPerEm.y);

    float coverage = combineCoverage(xCoverage, yCoverage, flagsFromGlyph(glyph));
    return coverage;
}

void main() {
    float coverage = slugRender(v_sample, v_band, v_glyph);
    float alpha = v_color.a * coverage;
    outColor = vec4(v_color.rgb * alpha, alpha);
}
```

`renderHorizontalBand()`:

1. Fetch header `(count, offset)` from `u_bandTex`.
2. Convert `offset` to a texture location relative to `glyphLoc`.
3. Loop over curve locations.
4. Fetch `p0,p1,p2` from `u_curveTex`.
5. Subtract `sampleCoord`.
6. Early break if sorted max coordinate is left of the half-pixel footprint.
7. Get `rootCode(p0.y, p1.y, p2.y)`.
8. If any root contributes, solve roots and accumulate signed coverage.

`renderVerticalBand()` mirrors this on x/y with the sign changes used by the reference pixel shader.

Use a defensive loop bound:

```glsl
const int MAX_BAND_CURVES = 128;
for (int i = 0; i < MAX_BAND_CURVES; ++i) {
    if (i >= count) break;
    ...
}
```

The band builder should guarantee counts below this. If a glyph exceeds it, rebuild with more bands or route that glyph through a slow shader variant with a higher maximum.

### Band Texture Layout

For each glyph/path, reserve a contiguous block in `bandTexture`.

```text
base + 0 .. base + hBandCount - 1:
    horizontal band headers, indexed by y band

base + hBandCount .. base + hBandCount + vBandCount - 1:
    vertical band headers, indexed by x band

base + listOffset:
    curve location lists, each texel = u16 curveX, u16 curveY
```

Header:

```text
R = count
G = list offset from base, in texels
```

`CalcBandLoc` equivalent:

```glsl
ivec2 bandLocFromOffset(ivec2 glyphLoc, uint offset) {
    int width = 1 << u_logBandTextureWidth;
    int x = glyphLoc.x + int(offset);
    return ivec2(x & (width - 1), glyphLoc.y + (x >> u_logBandTextureWidth));
}
```

Store `glyphLoc` per draw item as `uvec2`.

### CPU Band-Count Heuristic

For each glyph/path with `C` curves:

1. If `C == 0`, no draw item.
2. Try candidate band counts per axis, for example `{4, 6, 8, 12, 16, 24, 32}`.
3. For each candidate, count horizontal and vertical list sizes.
4. Reject if any list count exceeds `MAX_BAND_CURVES`.
5. Score:

```text
score = maxListCount * 16 + averageListCount * 4 + totalListRefs * 0.05 + bandCount * 0.5
```

6. Pick the lowest score.

For code/editor fonts, 8 or 12 bands per axis should usually be enough. Very ornate fonts or vector icons may need more. The official README says a glyph can have any number of bands, and the chosen number should minimize the worst per-band curve count (https://github.com/EricLengyel/Slug).

### CPU Curve Assignment To Bands

For each curve:

```js
const minX = Math.min(p0.x, p1.x, p2.x) - EPS;
const maxX = Math.max(p0.x, p1.x, p2.x) + EPS;
const minY = Math.min(p0.y, p1.y, p2.y) - EPS;
const maxY = Math.max(p0.y, p1.y, p2.y) + EPS;
```

Use `EPS = 1 / 1024` in em space as the official README suggests (https://github.com/EricLengyel/Slug). The Bezier curve lies inside the convex hull of its control points, so the control-point min/max is conservative. Actual curve extrema can reduce band list sizes later.

Horizontal band:

```js
if (!(p0.y === p1.y && p1.y === p2.y)) {
  add curve to every y band intersecting [minY, maxY]
}
```

Vertical band:

```js
if (!(p0.x === p1.x && p1.x === p2.x)) {
  add curve to every x band intersecting [minX, maxX]
}
```

Sort:

```js
horizontal.sort((a, b) => b.maxX - a.maxX);
vertical.sort((a, b) => b.maxY - a.maxY);
```

Deduplicate identical curve-location lists after correctness is proven. The official README notes that adjacent bands with identical or subset curve lists can share data (https://github.com/EricLengyel/Slug).

### CPU Mesh Generation

Initial bbox quad per glyph/path:

```text
v0: (minX, minY), normal (-1, -1), sample (minX, minY)
v1: (maxX, minY), normal ( 1, -1), sample (maxX, minY)
v2: (maxX, maxY), normal ( 1,  1), sample (maxX, maxY)
v3: (minX, maxY), normal (-1,  1), sample (minX, maxY)
indices: 0,1,2, 0,2,3
```

The normal here is a miter vector, not a unit normal. The vertex shader normalizes it for the direction part but multiplies the final scalar by the unnormalized vector so both adjacent sides expand.

For a transformed text run:

```text
object = textRunTransform * em
jacobian = upper-left 2x2 of object-from-em transform
invJacobian = inverse(jacobian)
```

For axis-aligned editor text:

```text
object.x = glyphX + em.x * fontPx
object.y = baselineY - em.y * fontPx
invJacobian = diag(1 / fontPx, -1 / fontPx)
```

Optional tight polygon:

- Compute a convex polygon around the glyph/path control-point bbox or a coarse outline hull.
- Limit to 6 to 8 vertices for predictable vertex cost.
- For each polygon vertex, compute a miter vector from adjacent edge outward normals.
- Triangulate as a fan.
- Keep bbox quad fallback for degenerate polygons.

The Slug page says tight bounding polygons can improve speed at moderate/large font sizes (https://sluglibrary.com/). For an editor, start with quads and profile. Tight polygons matter most for large UI icons and zoomed text.

## CPU Font Preprocessing

### Minimal TTF Parser

Implement a safe `DataView` based SFNT reader:

```js
class SfntReader {
  u16(off) {}
  i16(off) {}
  u32(off) {}
  tag(off) {}
  slice(off, len) {}
}
```

Parse:

- Offset table and table records.
- `head`: `unitsPerEm`, global bbox, `indexToLocFormat`.
- `maxp`: `numGlyphs`; for TrueType, max points/contours and composite depth are useful for validation (https://learn.microsoft.com/en-us/typography/opentype/spec/maxp).
- `loca`: glyph offsets into `glyf`; short offsets are divided by 2 in storage, long offsets are actual offsets (https://learn.microsoft.com/en-us/typography/opentype/spec/loca).
- `glyf`: simple and composite outlines (https://learn.microsoft.com/en-us/typography/opentype/spec/glyf).
- `cmap`: character code to glyph ID. Support format 4 and 12 first; the spec says modern fonts commonly use 4 or 12 depending on repertoire (https://learn.microsoft.com/en-us/typography/opentype/spec/cmap).
- `hhea` and `hmtx`: horizontal metrics. `hmtx` gives advance width and left side bearing (https://learn.microsoft.com/en-us/typography/opentype/spec/hmtx).
- `OS/2`: optional `sCapHeight` for font-size snapping, following the reference README's hinting workaround (https://github.com/EricLengyel/Slug).

Do not execute TrueType hinting instructions. Slug ignores hinting. For security and simplicity, validate lengths and skip instruction bytecode.

### Simple Glyph Extraction

The `glyf` table simple glyph format has:

- Header with `numberOfContours` and bbox.
- `endPtsOfContours`.
- Instruction bytes.
- Packed flags.
- Delta-encoded x coordinates.
- Delta-encoded y coordinates.

The Microsoft spec defines the flag meanings and repeat behavior (https://learn.microsoft.com/en-us/typography/opentype/spec/glyf). Expand flags to one per point, then reconstruct absolute point coordinates from deltas.

Convert each contour into quadratic curves:

1. Split the global point array by `endPtsOfContours`.
2. For each contour, classify each point as on-curve or off-curve.
3. If two adjacent off-curve points occur, insert an implicit on-curve point halfway between them. TrueType outlines rely on this compact representation.
4. Rotate the contour so processing starts at an on-curve point. If there is no explicit on-curve point, create one halfway between the last and first off-curve points.
5. Emit:
   - on -> on: line as `{start, end, end}`
   - on -> off -> on: quadratic `{start, off, end}`
6. Close the contour.
7. Normalize by `unitsPerEm`.

### Composite Glyph Extraction

Composite glyphs reference child glyphs plus transforms. The Microsoft spec lists component flags, optional scale/matrix fields, and xy offsets (https://learn.microsoft.com/en-us/typography/opentype/spec/glyf).

Implementation:

```js
function loadGlyph(glyphId, transform, depth) {
  if (depth > maxDepth || seen.has(glyphId)) throw FontError;
  const desc = glyphDescription(glyphId);
  if (desc.simple) return transformSimpleContours(desc, transform);
  for (component of desc.components) {
    const childTransform = transform * componentTransform(component);
    append(loadGlyph(component.glyphId, childTransform, depth + 1));
  }
}
```

Support first:

- `ARGS_ARE_XY_VALUES`
- word/byte args
- uniform scale
- x/y scale
- 2x2 transform
- `MORE_COMPONENTS`

Defer point-alignment components if not needed by initial fonts, but detect and report them. For production, point alignment must be implemented.

### CFF/OTF And Cubics

Slug's core shader evaluates quadratics. The public Slug tool reads both TrueType and PostScript OpenType flavors (https://sluglibrary.com/), but implementing CFF is a separate project:

- CFF CharString parser.
- Cubic Bezier extraction.
- Cubic-to-quadratic approximation.

For the first complete editor renderer, support `.ttf` and OpenType fonts with `glyf`. For UI vector assets, author paths directly as quadratics or use an offline/dev-time converter. If runtime SVG import is required, parse SVG path commands and approximate cubics with quadratics.

Cubic-to-quadratic approximation plan:

- Recursively subdivide cubic segments until a single quadratic fit is below an em-space error tolerance.
- Fit a quadratic to a cubic subsegment by matching endpoints and choosing the quadratic control point from endpoint tangent intersection or least-squares midpoint matching.
- Use tolerance based on target maximum zoom. For editor UI, `1e-4` em is a reasonable starting point; verify visually at large scale.

### Font Size And Hinting Compensation

Slug ignores hinting, which is acceptable for scalable 2D/3D quality but can make small text less grid-fitted than platform text. The reference README suggests choosing sizes where `fontSizePx * sCapHeight` is an integer, accounting for DPI (https://github.com/EricLengyel/Slug).

For the editor:

- Snap text origin x/y to integer pixels.
- Snap baseline y consistently per line.
- Prefer monospaced fonts with clean TrueType outlines.
- Compute UI font size so cap height aligns when possible.
- Use `RGBA32F` curve texture during small-text validation, then compare `RGBA16F`.

## Vector UI Primitive Rendering

Slug can render generic vector graphics using the same technology according to the public Slug page and manual (https://sluglibrary.com/ and https://sluglibrary.com/SlugManual.pdf). For our renderer, UI primitives should enter the same path pipeline as glyphs.

### Path API

```js
path.moveTo(x, y)
path.lineTo(x, y)
path.quadTo(cx, cy, x, y)
path.cubicTo(c1x, c1y, c2x, c2y, x, y) // approximated to quadratics
path.close()
```

Each closed path becomes one or more contours. Fill rules:

- Nonzero fill by default.
- Even-odd optional via a flag, matching the reference shader's optional even-odd branch.

Coordinates:

- Use UI object units directly, or normalize per shape into a local em-like space and transform to pixels.
- For dynamic UI shapes, object units as CSS pixels are easiest. Then `fwidth(renderCoord)` directly reports object units per pixel for orthographic transforms.

### Common Editor Primitives

Rounded rectangle:

- Four straight edges.
- Four quadratic corner arcs.
- A quarter circle can be approximated by one quadratic with control point at the square corner and endpoints on the axes. Error is acceptable for small radii; use two quadratics per corner for large radii.

Selection rectangle:

- Filled rectangle path.
- If corners are square, a simple quad fast path can draw it, but Slug path keeps the pipeline unified.

Caret:

- Thin rectangle path, pixel-snapped.

Underline/strikethrough:

- Thin rectangle path.

Diagnostic squiggle:

- Stroke converted to fill, or a repeated small quadratic-wave filled strip.
- For speed, prebuild a unit squiggle tile path and instantiate it.

Icons:

- Author directly as quadratic paths where possible.
- If importing SVG, approximate cubics and arcs at build time or first load.
- Pack all static icons into a UI "album" texture pair. The commercial Slug tool has an album concept for vector graphics, but our album can be a simple array of processed path records (https://sluglibrary.com/SlugManual.pdf).

Panels, titlebars, tabs:

- Large flat rectangles are cheap to draw as normal quads. If the strict requirement is "all UI through Slug", represent them as paths. The fragment cost will be higher than a solid-color shader but acceptable for a first renderer if the visible area is modest. Profile before introducing a separate fast path.

### Strokes

The shader fills closed regions. Strokes must become filled outlines on CPU.

Initial stroke algorithm:

- Flatten quadratic/cubic source path into line segments at a pixel or em tolerance.
- Offset left/right by half stroke width.
- Join with bevel or round joins.
- Cap with butt, square, or round caps.
- Emit a filled contour.

This is enough for editor decorations and icons. Later, implement true quadratic offset approximations for high-quality stroked Beziers without excessive flattening.

## Batching And Rendering Flow

### Frame Build

For each frame:

1. Determine visible editor lines and UI surfaces.
2. Build draw items:
   - glyphs for visible text
   - UI paths for panels, selections, cursor, icons
3. Group by:
   - shader variant
   - curve/band texture pair
   - clip rectangle
   - blend mode
4. Upload dynamic vertex/index data to ring buffers.
5. Render batches with scissor clipping and premultiplied alpha.

### Texture Ownership

Use one processed resource per font face/style:

```text
SlugFontResource {
  curveTexture
  bandTexture
  glyphRecords[glyphId]
  metrics
}
```

Use one processed resource for static UI vector assets:

```text
SlugVectorAlbum {
  curveTexture
  bandTexture
  pathRecords[pathId]
}
```

Dynamic shapes have two options:

- Pack into a per-frame dynamic texture region. More upload complexity, but unified.
- Maintain a small cache keyed by path parameters, such as `roundedRect:width:height:radius`. This is better for editor UI because dimensions repeat.

### Buffer Updates

For code editor text:

- Do not create draw items for spaces with no outline.
- Cache processed glyph data by glyph ID.
- Rebuild only visible line vertices each frame.
- Use large `ARRAY_BUFFER` and `ELEMENT_ARRAY_BUFFER` ring buffers with `bufferSubData`.
- Keep index type `UNSIGNED_INT` if available in WebGL2 core; WebGL2 includes 32-bit element indices. Otherwise split batches under 65k vertices.

For very large visible text:

- Move to instanced glyph drawing.
- Per instance: glyph ID/resource page, position, scale, color, optional style flags.
- Vertex shader fetches glyph bbox/band metadata from a metadata texture.

### Clipping

Most editor clipping is rectangular:

- Use `gl.scissor` with a stack of intersected clip rectangles.
- Split batches when clip rect changes.
- For text inside scrollable panes, scissor is ideal.

For non-rectangular vector clip:

- Option 1: render clip mask into an offscreen alpha texture and sample it in the fragment shader.
- Option 2: stencil with a coverage threshold. This is less accurate for antialiased clip edges unless a mask pass is used.

Do not use stencil for glyph winding. The Slug fragment shader already computes shape coverage.

### Transforms

Each draw run has:

```text
objectFromPath: 3x3 or 4x4
clipFromObject: 4x4 MVP
inverseJacobian: inverse upper-left 2x2 objectFromPath
```

For editor 2D, use an orthographic projection from CSS pixels to clip space. Match canvas backing resolution and device pixel ratio:

```js
canvas.width = cssWidth * devicePixelRatio;
canvas.height = cssHeight * devicePixelRatio;
gl.viewport(0, 0, canvas.width, canvas.height);
```

Positions should be in device pixels if the orthographic matrix maps device pixels. This makes `0.5`-pixel behavior predictable.

## WebGL2 Limitations Versus Desktop GL/Vulkan

WebGL2 is sufficient but narrower:

- No compute shaders. All preprocessing stays on CPU, or uses fragment/transform-feedback tricks later. Do not rely on GPU band building.
- No shader storage buffer objects. Use textures and vertex attributes for structured data.
- No texture buffers. Use 2D textures for curve/band/metadata tables.
- No geometry shader. CPU emits bounding geometry, or vertex shader expands instanced quads from `gl_VertexID`.
- Uniform buffer objects exist, but small plain uniforms are enough initially.
- Integer vertex attributes require `vertexAttribIPointer`, not `vertexAttribPointer` (https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext/vertexAttribIPointer).
- Integer texture formats require matching integer sampler types. Do not sample `RG16UI` with `sampler2D`; use `usampler2D`.
- Browser implementations may vary in shader compiler tolerance. Keep shaders straightforward, use fixed loop bounds where practical, and test Chrome, Firefox, and Safari.

Useful WebGL2 features:

- `#version 300 es`
- `uint`, `uvec*`, bit shifts and bitwise operators
- `floatBitsToUint`
- `texelFetch`
- `fwidth`
- `flat` interpolation
- `drawElementsInstanced` / `vertexAttribDivisor` for later batching
- VAOs

The Khronos quick reference confirms WebGL2 is based on OpenGL ES 3.0 and lists GLSL ES 3.00 integer types, unsigned integer samplers, bitwise operators, `flat`, and precision qualifiers (https://www.khronos.org/files/webgl20-reference-guide.pdf).

## Performance And Memory Notes

### Shader Cost

Cost is roughly:

```text
fragments covered by glyph/path bounding geometry
* (horizontal band curve count + vertical band curve count)
```

For ordinary code editor text, bounding boxes are small and simple fonts have modest curve counts. It should be usable if the visible text region is virtualized and spaces are skipped. Large decorative UI paths and large zoomed glyphs may need tight polygons.

### Data Size

The JCGT paper reports processed curve/band data can be several times larger than the original TTF because implicit points become explicit and band data adds references (https://jcgt.org/published/0006/02/02/paper.pdf). This is acceptable for editor fonts if we subset/cache glyphs on demand.

On-demand glyph processing:

- At font load, parse tables and metrics only.
- When a glyph ID is first needed, extract curves, build bands, append to textures.
- Texture growth strategy:
  - allocate initial 4096 x N
  - if full, allocate larger height and re-upload, or create a new page
  - avoid moving existing glyph records if possible

Static preload option:

- For ASCII and common code symbols, process glyphs at startup.
- Lazy process non-ASCII.

### Half Float Precision

`RGBA16F` is official-compatible and saves memory/bandwidth. But half float has limited mantissa precision. With em-space coordinates around `[-2, 2]`, precision is usually good enough for UI/text, but huge zoom or extremely detailed glyphs could show errors.

Validation plan:

- Implement `RGBA32F` mode.
- Render test pages at small, normal, and huge sizes.
- Compare `RGBA16F` to `RGBA32F`.
- If differences are visible for editor fonts, keep `RGBA32F` for font resources and use `RGBA16F` for simple UI icons.

### Band Count Tradeoff

More bands:

- fewer curves per fragment
- more band headers and curve list references
- more CPU preprocessing

Fewer bands:

- smaller data
- more fragment work

Adaptive per-glyph candidates should be good enough. Track metrics:

```text
curveCount
hBandCount / vBandCount
maxCurvesPerBand
avgCurvesPerBand
bandDataTexels
```

Log worst glyphs.

### Blending And Gamma

Browser canvas output is typically sRGB-ish. WebGL blending happens in the framebuffer's numeric space. For a first editor renderer:

- Use premultiplied alpha.
- Keep UI colors in sRGB but accept approximate blending.
- Later, if color correctness matters, render to an sRGB-capable framebuffer or manually linearize colors for composition.

### Editor-Specific Expectations

Use virtualization:

- Visible lines only.
- Visible columns only if lines are very long.
- Skip whitespace draw items.
- Batch token colors through per-vertex color.
- Cache line geometry until text, scroll x, font, or colorization changes.

The algorithm is more expensive than a bitmap atlas for tiny static text. The benefit is exact scaling, transforms, vector UI unification, and no raster atlas management.

## Pitfalls

### Root Classification Edge Cases

The whole algorithm depends on classifying roots exactly as intended. Test all sign classes:

- control points above/below/on ray
- tangent at endpoints
- consecutive curves sharing endpoints
- degenerate lines
- horizontal and vertical straight lines
- overlapping contours

CPU golden tests should compare shader output to a high-precision CPU implementation over many sample positions.

### Parallel Straight Lines In Bands

Do not include straight horizontal curves in horizontal bands or straight vertical curves in vertical bands. The official README explicitly calls this out because such curves cannot change the winding number for rays parallel to them (https://github.com/EricLengyel/Slug).

### Band Epsilon

If bands do not overlap slightly, a sample near a band boundary may miss a curve needed for coverage. Use the `1/1024` em epsilon initially, and make it configurable.

### Packed Metadata

Do not bitcast arbitrary integer payloads through float vertex attributes in WebGL. Use integer attributes or integer metadata textures. This is the most important WebGL-specific change from the reference HLSL layout.

### Dynamic Dilation Denominator

The dynamic formula has a denominator. Orthographic cases are stable. Near-singular projective cases can blow up:

- Reject/cull glyphs crossing the camera plane.
- Clamp denominator away from zero.
- In 2D editor mode, use a simpler dilation path if needed.

### Small Text Without Hinting

Text at 11 to 14 px may look different from OS text. Mitigations:

- pixel-snap baseline and x origin
- cap-height-aligned font size
- choose editor fonts with clean outlines
- consider optical weight mode (`sqrt(coverage)` in reference shader) as a user-tunable option, but be careful because it changes visual weight

### Loop Bounds

A corrupt font or bad band builder could create massive per-band lists. Enforce:

- max contours
- max points
- max curves per glyph
- max curves per band
- max composite recursion
- max texture pages

Reject or simplify glyphs that exceed limits.

### Premultiplied Alpha

If output color is not premultiplied but blending uses premultiplied settings, text will be wrong. Pick one convention:

- Store straight UI colors.
- Shader outputs premultiplied `vec4(rgb * alpha, alpha)`.
- Use `ONE, ONE_MINUS_SRC_ALPHA`.

### Texture Upload Alignment

Set `gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1)` before uploads unless every row is naturally 4-byte aligned. `RG16UI` with width 4096 is aligned, but dynamic widths or metadata textures may not be.

### Coordinate Direction

Fonts are y-up; browser UI is y-down. Pick one internal convention and apply a transform. Do not flip outlines manually in one path and transforms in another, or winding/fill rules become hard to reason about. The reference coverage uses absolute values so either winding direction can work for nonzero fill, but consistent coordinates still matter for layout and UI.

## Implementation Checklist

### Phase 1: Minimal Slug Path Renderer

- [ ] WebGL2 context creation and capability checks.
- [ ] Compile GLSL ES 3.00 vertex/fragment shaders.
- [ ] Create `RGBA32F` curve texture and `RG16UI` band texture.
- [ ] Hardcode one test shape, such as a quadratic circle-like outline.
- [ ] Build bands for that shape.
- [ ] Draw bbox quad.
- [ ] Verify coverage at multiple scales.
- [ ] Add premultiplied alpha blending.
- [ ] Add scissor clipping.

### Phase 2: Shader Robustness

- [ ] Port root eligibility exactly.
- [ ] Port horizontal and vertical band loops.
- [ ] Port coverage combine with nonzero fill.
- [ ] Add optional even-odd fill.
- [ ] Add dynamic dilation.
- [ ] Add CPU test generator for degenerate curves.
- [ ] Compare shader output against CPU raster reference.

### Phase 3: TTF Parser

- [ ] Parse SFNT table directory.
- [ ] Read `head`, `maxp`, `loca`, `glyf`, `cmap`, `hhea`, `hmtx`.
- [ ] Support `cmap` format 4 and 12.
- [ ] Extract simple glyph contours.
- [ ] Insert implicit TrueType on-curve points.
- [ ] Support composite glyph xy offsets and transforms.
- [ ] Normalize to em space.
- [ ] Build per-glyph curves, bands, and bbox quads.
- [ ] Skip glyphs with no contours.

### Phase 4: Editor Text

- [ ] Load one monospaced `.ttf`.
- [ ] Map ASCII and common code punctuation to glyph IDs.
- [ ] Use `hmtx` advances for layout.
- [ ] Pixel-snap baselines and glyph origins.
- [ ] Build visible-line vertex buffers.
- [ ] Batch by font texture and clip rect.
- [ ] Add token colors through vertex color.
- [ ] Cache line geometry.

### Phase 5: UI Vector Paths

- [ ] Implement `PathBuilder`.
- [ ] Add line and quadratic commands.
- [ ] Add cubic approximation.
- [ ] Add rounded rectangle, caret, underline, squiggle, and icon paths.
- [ ] Add stroke-to-fill for simple polylines.
- [ ] Cache repeated UI shapes.
- [ ] Build a static UI vector album.

### Phase 6: Optimization

- [ ] Switch curve texture from `RGBA32F` to `RGBA16F` and compare.
- [ ] Add adaptive band count scoring.
- [ ] Add identical band-list deduplication.
- [ ] Add optional tight bounding polygons.
- [ ] Add instanced glyph rendering if vertex upload becomes a bottleneck.
- [ ] Add GPU timer queries where available.
- [ ] Add browser/device compatibility matrix.

## Recommended Test Assets

Use these glyphs/shapes:

- `I`, `l`, `H`: vertical/horizontal straight segments.
- `O`, `o`, `e`: curved contours and counters.
- `A`, `V`, `W`: sharp joins and diagonals.
- `@`, `&`, `%`: complex contour interactions.
- `.` and `,`: tiny marks.
- Composite accented glyphs such as an e-acute character.
- Degenerate synthetic contours with endpoint tangencies.
- Rounded rectangles at radii 0, 2, 4, 8, 16 px.
- Thin 1 px and 2 px strokes.
- Diagnostic squiggle path.

Render at:

- 8, 10, 12, 14, 16, 24, 48, 256 px.
- fractional x/y positions.
- nonuniform scale.
- rotation.
- perspective tilt if 3D support remains in scope.

## Unresolved Questions

- What exact font subset should the first editor support? If it is code-oriented Latin text, direct `cmap` plus `hmtx` is enough for a strong first build. If full UI internationalization is required, a no-dependency OpenType shaper is a large project.
- Should the production curve texture be `RGBA16F` or `RGBA32F`? The official format supports both, but our answer should be based on screenshots and performance on target browsers.
- What is the best adaptive band-count heuristic for editor fonts? The public guidance defines constraints but not a specific optimizer.
- How aggressive should the tight polygon optimization be? Bbox quads are simpler and likely fine for editor text. Large vector UI icons may benefit.
- Will all UI primitives truly render through Slug, or can large solid rectangles use a trivial quad shader after correctness is established?
- How much CFF/OTF support is required? TrueType `glyf` is much easier and enough for many editor fonts.
- How should gamma-correct blending be handled in the browser target?
- Should dynamic dilation always run, or should 2D editor batches use a simpler shader variant?

## Recommended Next Research Tasks

- Agent 2 should define the editor text architecture: shaping scope, Unicode policy, bidi policy, line layout, cursor hit testing, wrapping, and incremental geometry caching.
- Prototype the GLSL ES shader using one hardcoded glyph/path and compare against a CPU reference.
- Build the JS TTF parser for `glyf` fonts and validate against a small set of permissively licensed monospaced fonts.
- Investigate cap-height snapping with common editor fonts and document recommended default font sizes.
- Benchmark bbox quads versus tight polygons on large text and icon-heavy UI.
- Decide whether UI static icons should be authored as custom quadratic paths or imported from SVG through a build-time converter.
- Create a regression corpus of degenerate glyph contours to protect the root-eligibility and band-building code.

import type { Color, Rect } from "../shared/types";
import type { ViewportInfo } from "../platform/viewport";
import { GlyphOutline, pushLine, SlugCurve, TrueTypeFont } from "./truetype";

export type Point = { x: number; y: number };
export type FontName = "ui" | "uiSmall" | "code" | "title" | "mini" | "gutter";
export type FontSource = { name: string; buffer: ArrayBuffer };

type RectCommand = { type: "rect"; rect: Rect; color: Color; clip: Rect | null };
type PolygonCommand = { type: "polygon"; points: Point[]; color: Color; clip: Rect | null };
type SolidPolygonCommand = { type: "solidPolygon"; points: Point[]; color: Color; clip: Rect | null };
type LineCommand = { type: "line"; a: Point; b: Point; width: number; color: Color; clip: Rect | null };
type TextCommand = { type: "text"; text: string; x: number; y: number; color: Color; font: FontName; clip: Rect | null };
type Command = RectCommand | PolygonCommand | SolidPolygonCommand | LineCommand | TextCommand;

type FontMetrics = { sizePx: number; ascentPx: number; descentPx: number; lineHeightPx: number; monoAdvancePx: number };
type Shape = { key: string; curves: SlugCurve[]; xMin: number; yMin: number; xMax: number; yMax: number };
type ParsedFont = { name: string; font: TrueTypeFont };
type GlyphMetrics = Shape & { fontIndex: number; glyphId: number; advancePxByFont: Map<FontName, number> };
type PackedShape = {
  key: string;
  xMin: number;
  yMin: number;
  xMax: number;
  yMax: number;
  bandX: number;
  bandY: number;
  maxBandX: number;
  maxBandY: number;
  bandScaleX: number;
  bandScaleY: number;
  bandOffsetX: number;
  bandOffsetY: number;
};
type FrameSlugData = { shapes: Map<string, PackedShape> };
type CurveBounds = { minX: number; minY: number; maxX: number; maxY: number; sortX: number; sortY: number; locX: number; locY: number };

const CURVE_TEXTURE_WIDTH = 4096;
const BAND_TEXTURE_WIDTH = 4096;
const MAX_BAND_CURVES = 768;
const UI_SHAPE_MARGIN_PX = 1;
const BASE_UI_FONT_SIZE = 13;
const BASE_UI_SMALL_FONT_SIZE = BASE_UI_FONT_SIZE - 2;
const BASE_CODE_FONT_SIZE = 14;
const BASE_TITLE_FONT_SIZE = 18;
const BASE_MINI_FONT_SIZE = 8;

export class WebglRenderer {
  readonly gl: WebGL2RenderingContext;
  readonly backend = "slug-ttf";
  private viewport: ViewportInfo;
  private readonly fonts: ParsedFont[];
  private readonly primaryFont: TrueTypeFont;
  private readonly slugProgram: WebGLProgram;
  private readonly solidProgram: WebGLProgram;
  private readonly floatBuffer: WebGLBuffer;
  private readonly glyphBuffer: WebGLBuffer;
  private readonly solidBuffer: WebGLBuffer;
  private readonly curveTexture: WebGLTexture;
  private readonly bandTexture: WebGLTexture;
  private readonly commands: Command[] = [];
  private readonly clipStack: Rect[] = [];
  private readonly glyphMetrics = new Map<string, GlyphMetrics>();
  private readonly emojiFontIndex: number;
  private readonly monaspaceFontIndex: number;
  private readonly preferredFontIndex: Record<FontName, number>;
  private readonly fontMetrics: Record<FontName, FontMetrics>;

  constructor(private readonly canvas: HTMLCanvasElement, initialViewport: ViewportInfo, fontSources: FontSource[]) {
    const gl = canvas.getContext("webgl2", { alpha: false, antialias: true });
    if (!gl) throw new Error("WebGL2 is required");
    if (fontSources.length === 0) throw new Error("At least one TTF font is required");
    this.gl = gl;
    this.viewport = initialViewport;
    this.fonts = fontSources.map((source) => ({ name: source.name, font: new TrueTypeFont(source.buffer) }));
    this.primaryFont = this.fonts[0]!.font;
    const emojiIndex = this.fonts.findIndex((item) => item.name.includes("NotoEmoji"));
    const monaspaceIndex = this.fonts.findIndex((item) => item.name.includes("MonaspaceNeon"));
    this.emojiFontIndex = emojiIndex >= 0 ? emojiIndex : 0;
    this.monaspaceFontIndex = monaspaceIndex >= 0 ? monaspaceIndex : 0;
    this.preferredFontIndex = {
      ui: 0,
      uiSmall: 0,
      code: 0,
      title: 0,
      mini: 0,
      gutter: this.monaspaceFontIndex
    };
    this.fontMetrics = {
      ui: this.makeFontMetrics(BASE_UI_FONT_SIZE),
      uiSmall: this.makeFontMetrics(BASE_UI_SMALL_FONT_SIZE),
      code: this.makeFontMetrics(BASE_CODE_FONT_SIZE),
      title: this.makeFontMetrics(BASE_TITLE_FONT_SIZE),
      mini: this.makeFontMetrics(BASE_MINI_FONT_SIZE),
      gutter: this.makeFontMetrics(BASE_CODE_FONT_SIZE, this.preferredFontIndex.gutter)
    };
    this.slugProgram = createProgram(gl, SLUG_VS, SLUG_FS);
    this.solidProgram = createProgram(gl, SOLID_VS, SOLID_FS);
    this.floatBuffer = mustBuffer(gl);
    this.glyphBuffer = mustBuffer(gl);
    this.solidBuffer = mustBuffer(gl);
    this.curveTexture = mustTexture(gl);
    this.bandTexture = mustTexture(gl);
    configureFloatTexture(gl, this.curveTexture);
    configureIntegerTexture(gl, this.bandTexture);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
  }

  diagnostics(): { backend: string; font: string; unitsPerEm: number; glyphCount: number; fonts: Array<{ name: string; unitsPerEm: number; glyphCount: number }> } {
    return {
      backend: this.backend,
      font: this.fonts[0]!.name,
      unitsPerEm: this.primaryFont.unitsPerEm,
      glyphCount: this.primaryFont.glyphCount,
      fonts: this.fonts.map((item) => ({ name: item.name, unitsPerEm: item.font.unitsPerEm, glyphCount: item.font.glyphCount }))
    };
  }

  resolveCodePoint(codePoint: number, font: FontName = "ui"): { font: string; glyphId: number } {
    const match = this.findFontGlyph(codePoint, font);
    return { font: this.fonts[match.fontIndex]!.name, glyphId: match.glyphId };
  }

  setViewport(viewport: ViewportInfo): void {
    this.viewport = viewport;
  }

  configureText(codeFontSizePx: number, uiScalePercent: number, useMonospacedCodeFont = false): void {
    const uiScale = Math.max(0.01, uiScalePercent / 100);
    this.preferredFontIndex.code = useMonospacedCodeFont ? this.monaspaceFontIndex : 0;
    this.fontMetrics.ui = this.makeFontMetrics(BASE_UI_FONT_SIZE * uiScale);
    this.fontMetrics.uiSmall = this.makeFontMetrics(BASE_UI_SMALL_FONT_SIZE * uiScale);
    this.fontMetrics.title = this.makeFontMetrics(BASE_TITLE_FONT_SIZE * uiScale);
    this.fontMetrics.mini = this.makeFontMetrics(BASE_MINI_FONT_SIZE * uiScale);
    this.fontMetrics.code = this.makeFontMetrics(Math.max(1, codeFontSizePx), this.preferredFontIndex.code);
    this.fontMetrics.gutter = this.makeFontMetrics(Math.max(1, codeFontSizePx), this.preferredFontIndex.gutter);
    this.glyphMetrics.clear();
  }

  beginFrame(): void {
    this.commands.length = 0;
    this.clipStack.length = 0;
  }

  endFrame(): void {
    const gl = this.gl;
    const frame = this.buildFrameSlugData();
    gl.viewport(0, 0, this.viewport.deviceWidth, this.viewport.deviceHeight);
    gl.disable(gl.SCISSOR_TEST);
    gl.clearColor(0.12, 0.13, 0.15, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    let boundProgram: "slug" | "solid" | null = null;
    for (const command of this.commands) {
      if (command.clip) this.applyScissor(command.clip);
      else gl.disable(gl.SCISSOR_TEST);
      if (command.type === "line" || command.type === "solidPolygon") {
        if (boundProgram !== "solid") {
          this.bindSolidProgram();
          boundProgram = "solid";
        }
        if (command.type === "line") this.drawLineCommand(command);
        else this.drawSolidPolygonCommand(command);
        continue;
      }
      if (boundProgram !== "slug") {
        this.bindSlugProgram();
        boundProgram = "slug";
      }
      if (command.type === "rect") this.drawPackedShape(frame.shapes.get(rectKey(command.rect)), command.color, screenShapeTransform(command.rect));
      else if (command.type === "polygon") this.drawPackedShape(frame.shapes.get(polygonKey(command.points)), command.color, screenShapeTransform(boundsForPoints(command.points)));
      else this.drawTextCommand(command, frame);
    }
    gl.disable(gl.SCISSOR_TEST);
  }

  pushClip(rect: Rect): void {
    const top = this.clipStack[this.clipStack.length - 1];
    if (!top) {
      this.clipStack.push({ ...rect });
      return;
    }
    const x = Math.max(top.x, rect.x);
    const y = Math.max(top.y, rect.y);
    const x2 = Math.min(top.x + top.w, rect.x + rect.w);
    const y2 = Math.min(top.y + top.h, rect.y + rect.h);
    this.clipStack.push({ x, y, w: Math.max(0, x2 - x), h: Math.max(0, y2 - y) });
  }

  popClip(): void {
    this.clipStack.pop();
  }

  rect(rect: Rect, color: Color): void {
    if (rect.w <= 0 || rect.h <= 0 || color[3] <= 0) return;
    this.commands.push({ type: "rect", rect: { ...rect }, color, clip: this.currentClip() });
  }

  polygon(points: Point[], color: Color): void {
    if (points.length < 3 || color[3] <= 0) return;
    this.commands.push({ type: "polygon", points: points.map((point) => ({ ...point })), color, clip: this.currentClip() });
  }

  solidPolygon(points: Point[], color: Color): void {
    if (points.length < 3 || color[3] <= 0) return;
    this.commands.push({ type: "solidPolygon", points: points.map((point) => ({ ...point })), color, clip: this.currentClip() });
  }

  line(a: Point, b: Point, width: number, color: Color): void {
    if (width <= 0 || color[3] <= 0) return;
    if (a.x === b.x && a.y === b.y) return;
    this.commands.push({ type: "line", a: { ...a }, b: { ...b }, width, color, clip: this.currentClip() });
  }

  text(text: string, x: number, y: number, color: Color, font: FontName = "ui"): number {
    if (!text || color[3] <= 0) return 0;
    this.commands.push({ type: "text", text, x, y, color, font, clip: this.currentClip() });
    return this.measureText(text, font);
  }

  measureText(text: string, font: FontName = "ui"): number {
    let width = 0;
    for (const char of text) width += char === "\t" ? this.defaultTabAdvance(font) : this.advanceForCodePoint(char.codePointAt(0) ?? 0, font);
    return width;
  }

  visualTextBounds(text: string, font: FontName = "ui"): Rect {
    const metrics = this.fontMetrics[font];
    let penX = 0;
    let xMin = Number.POSITIVE_INFINITY;
    let yMin = Number.POSITIVE_INFINITY;
    let xMax = Number.NEGATIVE_INFINITY;
    let yMax = Number.NEGATIVE_INFINITY;
    for (const char of text) {
      if (char === "\t") {
        penX += this.defaultTabAdvance(font);
        continue;
      }
      const glyph = this.glyphForCodePoint(char.codePointAt(0) ?? 0, font);
      if (glyph.curves.length > 0) {
        xMin = Math.min(xMin, penX + glyph.xMin * metrics.sizePx);
        xMax = Math.max(xMax, penX + glyph.xMax * metrics.sizePx);
        yMin = Math.min(yMin, metrics.ascentPx - glyph.yMax * metrics.sizePx);
        yMax = Math.max(yMax, metrics.ascentPx - glyph.yMin * metrics.sizePx);
      }
      penX += this.advanceForGlyph(glyph, font);
    }
    if (!Number.isFinite(xMin)) return { x: 0, y: 0, w: this.measureText(text, font), h: this.lineHeight(font) };
    return { x: xMin, y: yMin, w: Math.max(0, xMax - xMin), h: Math.max(0, yMax - yMin) };
  }

  lineHeight(font: FontName = "ui"): number {
    return this.fontMetrics[font].lineHeightPx;
  }

  monoAdvance(font: FontName = "code"): number {
    return this.fontMetrics[font].monoAdvancePx;
  }

  private makeFontMetrics(sizePx: number, fontIndex = 0): FontMetrics {
    const font = this.fonts[fontIndex]?.font ?? this.primaryFont;
    const scale = sizePx / font.unitsPerEm;
    const ascentPx = font.ascender * scale;
    const descentPx = -font.descender * scale;
    const lineHeightPx = Math.ceil((font.ascender - font.descender + font.lineGap) * scale);
    const monoAdvancePx = font.outlineForCodePoint("M".codePointAt(0)!).advanceWidth * scale;
    return { sizePx, ascentPx, descentPx, lineHeightPx, monoAdvancePx };
  }

  private currentClip(): Rect | null {
    const rect = this.clipStack[this.clipStack.length - 1];
    return rect ? { ...rect } : null;
  }

  private applyScissor(rect: Rect): void {
    const gl = this.gl;
    const dpr = this.viewport.dpr;
    const x = Math.max(0, Math.floor(rect.x * dpr));
    const y = Math.max(0, Math.floor((this.viewport.cssHeight - rect.y - rect.h) * dpr));
    const w = Math.max(0, Math.ceil(rect.w * dpr));
    const h = Math.max(0, Math.ceil(rect.h * dpr));
    gl.enable(gl.SCISSOR_TEST);
    gl.scissor(x, y, w, h);
  }

  private buildFrameSlugData(): FrameSlugData {
    const shapes = new Map<string, Shape>();
    for (const command of this.commands) {
      if (command.type === "rect") {
        const shape = rectShape(command.rect);
        shapes.set(shape.key, shape);
      } else if (command.type === "polygon") {
        const shape = polygonShape(command.points);
        shapes.set(shape.key, shape);
      } else if (command.type === "text") {
        for (const char of command.text) {
          if (char === "\t") continue;
          const glyph = this.glyphForCodePoint(char.codePointAt(0) ?? 0, command.font);
          if (glyph.curves.length > 0) shapes.set(glyph.key, glyph);
        }
      }
    }
    return this.packShapes([...shapes.values()]);
  }

  private packShapes(shapes: Shape[]): FrameSlugData {
    const packed = new Map<string, PackedShape>();
    const curveTexels: number[] = [];
    const bandTexels: number[] = [];
    for (const shape of shapes) {
      if (shape.curves.length === 0) continue;
      const curveBounds: CurveBounds[] = [];
      for (const curve of shape.curves) {
        const startIndex = appendCurveTexel(curveTexels, curve);
        curveBounds.push({
          minX: Math.min(curve.x1, curve.x2, curve.x3),
          minY: Math.min(curve.y1, curve.y2, curve.y3),
          maxX: Math.max(curve.x1, curve.x2, curve.x3),
          maxY: Math.max(curve.y1, curve.y2, curve.y3),
          sortX: Math.max(curve.x1, curve.x2, curve.x3),
          sortY: Math.max(curve.y1, curve.y2, curve.y3),
          locX: startIndex % CURVE_TEXTURE_WIDTH,
          locY: Math.floor(startIndex / CURVE_TEXTURE_WIDTH)
        });
      }

      const width = Math.max(1 / 1024, shape.xMax - shape.xMin);
      const height = Math.max(1 / 1024, shape.yMax - shape.yMin);
      const horizontal = buildLimitedBands(shape.key, curveBounds, bandCountForShape(shape.curves.length, height), shape.yMin, shape.yMax, "horizontal");
      const vertical = buildLimitedBands(shape.key, curveBounds, bandCountForShape(shape.curves.length, width), shape.xMin, shape.xMax, "vertical");
      const horizontalBandCount = horizontal.bands.length;
      const verticalBandCount = vertical.bands.length;
      const bandStart = bandTexels.length / 4;
      const headerCount = horizontalBandCount + verticalBandCount;
      for (let i = 0; i < headerCount; i++) pushBandTexel(bandTexels, 0, 0);
      writeBandHeadersAndLists(bandTexels, bandStart, horizontal.bands, 0);
      writeBandHeadersAndLists(bandTexels, bandStart, vertical.bands, horizontalBandCount);

      packed.set(shape.key, {
        key: shape.key,
        xMin: shape.xMin,
        yMin: shape.yMin,
        xMax: shape.xMax,
        yMax: shape.yMax,
        bandX: bandStart % BAND_TEXTURE_WIDTH,
        bandY: Math.floor(bandStart / BAND_TEXTURE_WIDTH),
        maxBandX: verticalBandCount - 1,
        maxBandY: horizontalBandCount - 1,
        bandScaleX: verticalBandCount / width,
        bandScaleY: horizontalBandCount / height,
        bandOffsetX: -shape.xMin * verticalBandCount / width,
        bandOffsetY: -shape.yMin * horizontalBandCount / height
      });
    }

    this.uploadCurveTexture(curveTexels);
    this.uploadBandTexture(bandTexels);
    return { shapes: packed };
  }

  private uploadCurveTexture(texels: number[]): void {
    const gl = this.gl;
    const texelCount = Math.max(1, texels.length / 4);
    const height = Math.max(1, Math.ceil(texelCount / CURVE_TEXTURE_WIDTH));
    const data = new Float32Array(CURVE_TEXTURE_WIDTH * height * 4);
    data.set(texels);
    gl.bindTexture(gl.TEXTURE_2D, this.curveTexture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, CURVE_TEXTURE_WIDTH, height, 0, gl.RGBA, gl.FLOAT, data);
  }

  private uploadBandTexture(texels: number[]): void {
    const gl = this.gl;
    const texelCount = Math.max(1, texels.length / 4);
    const height = Math.max(1, Math.ceil(texelCount / BAND_TEXTURE_WIDTH));
    const data = new Uint32Array(BAND_TEXTURE_WIDTH * height * 4);
    data.set(texels);
    gl.bindTexture(gl.TEXTURE_2D, this.bandTexture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32UI, BAND_TEXTURE_WIDTH, height, 0, gl.RGBA_INTEGER, gl.UNSIGNED_INT, data);
  }

  private bindSlugProgram(): void {
    const gl = this.gl;
    gl.useProgram(this.slugProgram);
    gl.uniform2f(gl.getUniformLocation(this.slugProgram, "uViewport"), this.viewport.cssWidth, this.viewport.cssHeight);
    gl.uniform1i(gl.getUniformLocation(this.slugProgram, "uCurveTexture"), 0);
    gl.uniform1i(gl.getUniformLocation(this.slugProgram, "uBandTexture"), 1);
    gl.uniform1i(gl.getUniformLocation(this.slugProgram, "uCurveTextureWidth"), CURVE_TEXTURE_WIDTH);
    gl.uniform1i(gl.getUniformLocation(this.slugProgram, "uBandTextureWidth"), BAND_TEXTURE_WIDTH);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.curveTexture);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.bandTexture);
  }

  private bindSolidProgram(): void {
    const gl = this.gl;
    gl.useProgram(this.solidProgram);
    gl.uniform2f(gl.getUniformLocation(this.solidProgram, "uViewport"), this.viewport.cssWidth, this.viewport.cssHeight);
  }

  private drawTextCommand(command: TextCommand, frame: FrameSlugData): void {
    const metrics = this.fontMetrics[command.font];
    const baseline = command.y + metrics.ascentPx;
    let penX = command.x;
    for (const char of command.text) {
      if (char === "\t") {
        penX += this.defaultTabAdvance(command.font);
        continue;
      }
      const glyph = this.glyphForCodePoint(char.codePointAt(0) ?? 0, command.font);
      const packed = frame.shapes.get(glyph.key);
      if (packed) this.drawPackedShape(packed, command.color, fontGlyphTransform(penX, baseline, metrics.sizePx), 1 / metrics.sizePx);
      penX += this.advanceForGlyph(glyph, command.font);
    }
  }

  private drawPackedShape(shape: PackedShape | undefined, color: Color, transform: (x: number, y: number) => Point, pixelMargin = UI_SHAPE_MARGIN_PX): void {
    if (!shape || color[3] <= 0) return;
    const marginX = Math.max(pixelMargin, (shape.xMax - shape.xMin) * 0.002);
    const marginY = Math.max(pixelMargin, (shape.yMax - shape.yMin) * 0.002);
    const x0 = shape.xMin - marginX;
    const x1 = shape.xMax + marginX;
    const y0 = shape.yMin - marginY;
    const y1 = shape.yMax + marginY;
    const p00 = transform(x0, y0);
    const p10 = transform(x1, y0);
    const p11 = transform(x1, y1);
    const p01 = transform(x0, y1);
    const floatData = new Float32Array([
      p00.x, p00.y, x0, y0, ...color,
      p10.x, p10.y, x1, y0, ...color,
      p11.x, p11.y, x1, y1, ...color,
      p00.x, p00.y, x0, y0, ...color,
      p11.x, p11.y, x1, y1, ...color,
      p01.x, p01.y, x0, y1, ...color
    ]);
    const glyphData = new Uint32Array(6 * 4);
    for (let i = 0; i < 6; i++) {
      glyphData[i * 4] = shape.bandX;
      glyphData[i * 4 + 1] = shape.bandY;
      glyphData[i * 4 + 2] = shape.maxBandX;
      glyphData[i * 4 + 3] = shape.maxBandY;
    }
    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.floatBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, floatData, gl.STREAM_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 32, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 32, 8);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, gl.FLOAT, false, 32, 16);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.glyphBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, glyphData, gl.STREAM_DRAW);
    gl.enableVertexAttribArray(3);
    gl.vertexAttribIPointer(3, 4, gl.UNSIGNED_INT, 16, 0);
    gl.uniform4f(gl.getUniformLocation(this.slugProgram, "uBandTransform"), shape.bandScaleX, shape.bandScaleY, shape.bandOffsetX, shape.bandOffsetY);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }

  private drawLineCommand(command: LineCommand): void {
    const points = screenLineQuad(command.a, command.b, command.width);
    const floatData = new Float32Array([
      points[0]!.x, points[0]!.y, ...command.color,
      points[1]!.x, points[1]!.y, ...command.color,
      points[2]!.x, points[2]!.y, ...command.color,
      points[0]!.x, points[0]!.y, ...command.color,
      points[2]!.x, points[2]!.y, ...command.color,
      points[3]!.x, points[3]!.y, ...command.color
    ]);
    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.solidBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, floatData, gl.STREAM_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, gl.FLOAT, false, 24, 8);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }

  private drawSolidPolygonCommand(command: SolidPolygonCommand): void {
    const triangleCount = command.points.length - 2;
    if (triangleCount <= 0) return;
    const floatData = new Float32Array(triangleCount * 3 * 6);
    let offset = 0;
    const pushVertex = (point: Point) => {
      floatData[offset++] = point.x;
      floatData[offset++] = point.y;
      floatData[offset++] = command.color[0];
      floatData[offset++] = command.color[1];
      floatData[offset++] = command.color[2];
      floatData[offset++] = command.color[3];
    };
    const origin = command.points[0]!;
    for (let i = 1; i < command.points.length - 1; i++) {
      pushVertex(origin);
      pushVertex(command.points[i]!);
      pushVertex(command.points[i + 1]!);
    }
    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.solidBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, floatData, gl.STREAM_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, gl.FLOAT, false, 24, 8);
    gl.drawArrays(gl.TRIANGLES, 0, triangleCount * 3);
  }

  private glyphForCodePoint(codePoint: number, font: FontName): GlyphMetrics {
    const match = this.findFontGlyph(codePoint, font);
    const cacheKey = `${match.fontIndex}:${match.glyphId}`;
    const cached = this.glyphMetrics.get(cacheKey);
    if (cached) return cached;
    const outline = this.fonts[match.fontIndex]!.font.outlineForGlyph(match.glyphId);
    const glyph = this.makeGlyphMetrics(match.fontIndex, outline);
    this.glyphMetrics.set(cacheKey, glyph);
    return glyph;
  }

  private findFontGlyph(codePoint: number, font: FontName): { fontIndex: number; glyphId: number } {
    for (const i of this.fontOrderFor(font)) {
      const glyphId = this.fonts[i]?.font.glyphIdForCodePoint(codePoint) ?? 0;
      if (glyphId > 0) return { fontIndex: i, glyphId };
    }
    return { fontIndex: this.preferredFontIndex[font] ?? 0, glyphId: 0 };
  }

  private fontOrderFor(font: FontName): number[] {
    const preferred = this.preferredFontIndex[font] ?? 0;
    const ordered = font === "code" && preferred === this.monaspaceFontIndex
      ? [preferred, this.emojiFontIndex, 0]
      : [preferred, 0, this.emojiFontIndex, this.monaspaceFontIndex];
    for (let i = 0; i < this.fonts.length; i++) ordered.push(i);
    return [...new Set(ordered.filter((index) => index >= 0 && index < this.fonts.length))];
  }

  private makeGlyphMetrics(fontIndex: number, outline: GlyphOutline): GlyphMetrics {
    const font = this.fonts[fontIndex]!.font;
    const units = font.unitsPerEm;
    const curves = outline.curves.map((curve) => ({
      x1: curve.x1 / units,
      y1: curve.y1 / units,
      x2: curve.x2 / units,
      y2: curve.y2 / units,
      x3: curve.x3 / units,
      y3: curve.y3 / units
    }));
    const advanceWidth = outline.advanceWidth / units;
    return {
      key: `glyph:${fontIndex}:${outline.glyphId}`,
      fontIndex,
      glyphId: outline.glyphId,
      curves,
      xMin: outline.xMin / units,
      yMin: outline.yMin / units,
      xMax: outline.xMax / units,
      yMax: outline.yMax / units,
      advancePxByFont: new Map([
        ["ui", advanceWidth * this.fontMetrics.ui.sizePx],
        ["uiSmall", advanceWidth * this.fontMetrics.uiSmall.sizePx],
        ["code", advanceWidth * this.fontMetrics.code.sizePx],
        ["title", advanceWidth * this.fontMetrics.title.sizePx],
        ["mini", advanceWidth * this.fontMetrics.mini.sizePx],
        ["gutter", advanceWidth * this.fontMetrics.gutter.sizePx]
      ])
    };
  }

  private advanceForCodePoint(codePoint: number, font: FontName): number {
    return this.advanceForGlyph(this.glyphForCodePoint(codePoint, font), font);
  }

  private advanceForGlyph(glyph: GlyphMetrics, font: FontName): number {
    return glyph.advancePxByFont.get(font) ?? this.fontMetrics[font].monoAdvancePx;
  }

  private defaultTabAdvance(font: FontName): number {
    return this.advanceForCodePoint(" ".codePointAt(0)!, font) * 4;
  }
}

function rectShape(rect: Rect): Shape {
  const curves: SlugCurve[] = [];
  const p0 = { x: rect.x, y: rect.y };
  const p1 = { x: rect.x + rect.w, y: rect.y };
  const p2 = { x: rect.x + rect.w, y: rect.y + rect.h };
  const p3 = { x: rect.x, y: rect.y + rect.h };
  pushLine(curves, p0, p1);
  pushLine(curves, p1, p2);
  pushLine(curves, p2, p3);
  pushLine(curves, p3, p0);
  return { key: rectKey(rect), curves, xMin: rect.x, yMin: rect.y, xMax: rect.x + rect.w, yMax: rect.y + rect.h };
}

function polygonShape(points: Point[]): Shape {
  const curves: SlugCurve[] = [];
  for (let i = 0; i < points.length; i++) pushLine(curves, points[i]!, points[(i + 1) % points.length]!);
  const bounds = boundsForPoints(points);
  return { key: polygonKey(points), curves, xMin: bounds.x, yMin: bounds.y, xMax: bounds.x + bounds.w, yMax: bounds.y + bounds.h };
}

function rectKey(rect: Rect): string {
  return `rect:${roundKey(rect.x)},${roundKey(rect.y)},${roundKey(rect.w)},${roundKey(rect.h)}`;
}

function polygonKey(points: Point[]): string {
  return `poly:${points.map((point) => `${roundKey(point.x)},${roundKey(point.y)}`).join(";")}`;
}

function roundKey(value: number): string {
  return Math.round(value * 100) / 100 + "";
}

function boundsForPoints(points: Point[]): Rect {
  let xMin = Number.POSITIVE_INFINITY;
  let yMin = Number.POSITIVE_INFINITY;
  let xMax = Number.NEGATIVE_INFINITY;
  let yMax = Number.NEGATIVE_INFINITY;
  for (const point of points) {
    xMin = Math.min(xMin, point.x);
    yMin = Math.min(yMin, point.y);
    xMax = Math.max(xMax, point.x);
    yMax = Math.max(yMax, point.y);
  }
  return { x: xMin, y: yMin, w: Math.max(1 / 1024, xMax - xMin), h: Math.max(1 / 1024, yMax - yMin) };
}

function screenShapeTransform(_bounds: Rect): (x: number, y: number) => Point {
  return (x, y) => ({ x, y });
}

function fontGlyphTransform(penX: number, baseline: number, sizePx: number): (x: number, y: number) => Point {
  return (x, y) => ({ x: penX + x * sizePx, y: baseline - y * sizePx });
}

function screenLineQuad(a: Point, b: Point, width: number): Point[] {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const length = Math.hypot(dx, dy) || 1;
  const px = (-dy / length) * (width / 2);
  const py = (dx / length) * (width / 2);
  return [
    { x: a.x + px, y: a.y + py },
    { x: b.x + px, y: b.y + py },
    { x: b.x - px, y: b.y - py },
    { x: a.x - px, y: a.y - py }
  ];
}

function appendCurveTexel(texels: number[], curve: SlugCurve): number {
  if ((texels.length / 4) % CURVE_TEXTURE_WIDTH === CURVE_TEXTURE_WIDTH - 1) {
    texels.push(0, 0, 0, 0);
  }
  const index = texels.length / 4;
  texels.push(curve.x1, curve.y1, curve.x2, curve.y2, curve.x3, curve.y3, 0, 0);
  return index;
}

function pushBandTexel(texels: number[], x: number, y: number): void {
  texels.push(x, y, 0, 0);
}

function bandCountForShape(curveCount: number, span: number): number {
  if (curveCount <= 6 || span <= 1 / 1024) return 1;
  return Math.max(1, Math.min(24, Math.ceil(Math.sqrt(curveCount))));
}

function buildBands(curves: CurveBounds[], count: number, min: number, max: number, axis: "horizontal" | "vertical"): CurveBounds[][] {
  const span = Math.max(1 / 1024, max - min);
  const result: CurveBounds[][] = [];
  const epsilon = span / 1024;
  for (let i = 0; i < count; i++) {
    const bandMin = min + span * i / count - epsilon;
    const bandMax = min + span * (i + 1) / count + epsilon;
    const band = curves.filter((curve) => axis === "horizontal"
      ? curve.maxY >= bandMin && curve.minY <= bandMax
      : curve.maxX >= bandMin && curve.minX <= bandMax);
    band.sort((a, b) => axis === "horizontal" ? b.sortX - a.sortX : b.sortY - a.sortY);
    result.push(band);
  }
  return result;
}

function buildLimitedBands(shapeKey: string, curves: CurveBounds[], initialCount: number, min: number, max: number, axis: "horizontal" | "vertical"): { bands: CurveBounds[][] } {
  let count = initialCount;
  for (;;) {
    const bands = buildBands(curves, count, min, max, axis);
    const maxBandCurves = bands.reduce((maxCount, band) => Math.max(maxCount, band.length), 0);
    if (maxBandCurves <= MAX_BAND_CURVES) return { bands };
    if (count >= 256) throw new Error(`Slug ${axis} band for ${shapeKey} contains ${maxBandCurves} curves; shader limit is ${MAX_BAND_CURVES}`);
    count = Math.min(256, count * 2);
  }
}

function writeBandHeadersAndLists(texels: number[], bandStart: number, bands: CurveBounds[][], headerOffset: number): void {
  for (let i = 0; i < bands.length; i++) {
    const band = bands[i]!;
    const listOffset = texels.length / 4 - bandStart;
    const headerIndex = (bandStart + headerOffset + i) * 4;
    texels[headerIndex] = band.length;
    texels[headerIndex + 1] = listOffset;
    for (const curve of band) pushBandTexel(texels, curve.locX, curve.locY);
  }
}

function configureFloatTexture(gl: WebGL2RenderingContext, texture: WebGLTexture): void {
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

function configureIntegerTexture(gl: WebGL2RenderingContext, texture: WebGLTexture): void {
  configureFloatTexture(gl, texture);
}

function mustBuffer(gl: WebGL2RenderingContext): WebGLBuffer {
  const buffer = gl.createBuffer();
  if (!buffer) throw new Error("Could not create WebGL buffer");
  return buffer;
}

function mustTexture(gl: WebGL2RenderingContext): WebGLTexture {
  const texture = gl.createTexture();
  if (!texture) throw new Error("Could not create WebGL texture");
  return texture;
}

function createProgram(gl: WebGL2RenderingContext, vsSource: string, fsSource: string): WebGLProgram {
  const vs = compileShader(gl, gl.VERTEX_SHADER, vsSource);
  const fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSource);
  const program = gl.createProgram();
  if (!program) throw new Error("Could not create WebGL program");
  gl.attachShader(program, vs);
  gl.attachShader(program, fs);
  gl.bindAttribLocation(program, 0, "aPosition");
  gl.bindAttribLocation(program, 1, "aRenderCoord");
  gl.bindAttribLocation(program, 2, "aColor");
  gl.bindAttribLocation(program, 3, "aGlyph");
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program) ?? "WebGL program link failed");
  gl.deleteShader(vs);
  gl.deleteShader(fs);
  return program;
}

function compileShader(gl: WebGL2RenderingContext, type: number, source: string): WebGLShader {
  const shader = gl.createShader(type);
  if (!shader) throw new Error("Could not create WebGL shader");
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader) ?? "WebGL shader compile failed");
  return shader;
}

const SLUG_VS = `#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aRenderCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in uvec4 aGlyph;
uniform vec2 uViewport;
uniform vec4 uBandTransform;
out vec2 vRenderCoord;
out vec4 vColor;
flat out uvec4 vGlyph;
flat out vec4 vBandTransform;
void main() {
  vec2 p = aPosition / uViewport * 2.0 - 1.0;
  gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
  vRenderCoord = aRenderCoord;
  vColor = aColor;
  vGlyph = aGlyph;
  vBandTransform = uBandTransform;
}`;

const SLUG_FS = `#version 300 es
precision highp float;
precision highp int;
precision highp usampler2D;
uniform sampler2D uCurveTexture;
uniform usampler2D uBandTexture;
uniform int uCurveTextureWidth;
uniform int uBandTextureWidth;
in vec2 vRenderCoord;
in vec4 vColor;
flat in uvec4 vGlyph;
flat in vec4 vBandTransform;
out vec4 outColor;

uint calcRootCode(float y1, float y2, float y3) {
  uint i1 = floatBitsToUint(y1) >> 31u;
  uint i2 = floatBitsToUint(y2) >> 30u;
  uint i3 = floatBitsToUint(y3) >> 29u;
  uint shift = (i2 & 2u) | (i1 & ~2u);
  shift = (i3 & 4u) | (shift & ~4u);
  return (0x2E74u >> shift) & 0x0101u;
}

vec2 solveHorizPoly(vec4 p12, vec2 p3) {
  vec2 a = p12.xy - p12.zw * 2.0 + p3;
  vec2 b = p12.xy - p12.zw;
  float d = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));
  float t1 = (b.y - d) / a.y;
  float t2 = (b.y + d) / a.y;
  if (abs(a.y) < 1.0 / 65536.0) {
    float t = p12.y * (0.5 / b.y);
    t1 = t;
    t2 = t;
  }
  return vec2((a.x * t1 - b.x * 2.0) * t1 + p12.x, (a.x * t2 - b.x * 2.0) * t2 + p12.x);
}

vec2 solveVertPoly(vec4 p12, vec2 p3) {
  vec2 a = p12.xy - p12.zw * 2.0 + p3;
  vec2 b = p12.xy - p12.zw;
  float d = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));
  float t1 = (b.x - d) / a.x;
  float t2 = (b.x + d) / a.x;
  if (abs(a.x) < 1.0 / 65536.0) {
    float t = p12.x * (0.5 / b.x);
    t1 = t;
    t2 = t;
  }
  return vec2((a.y * t1 - b.y * 2.0) * t1 + p12.y, (a.y * t2 - b.y * 2.0) * t2 + p12.y);
}

ivec2 offsetLoc(ivec2 base, uint offset, int width) {
  int x = base.x + int(offset);
  return ivec2(x % width, base.y + x / width);
}

float calcCoverage(float xcov, float ycov, float xwgt, float ywgt) {
  float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));
  return clamp(coverage, 0.0, 1.0);
}

float slugRender() {
  vec2 emsPerPixel = max(fwidth(vRenderCoord), vec2(1.0 / 65536.0));
  vec2 pixelsPerEm = 1.0 / emsPerPixel;
  ivec2 bandMax = ivec2(int(vGlyph.z), int(vGlyph.w & 255u));
  ivec2 bandIndex = clamp(ivec2(vRenderCoord * vBandTransform.xy + vBandTransform.zw), ivec2(0), bandMax);
  ivec2 glyphLoc = ivec2(vGlyph.xy);

  float xcov = 0.0;
  float xwgt = 0.0;
  uvec4 hbandData = texelFetch(uBandTexture, offsetLoc(glyphLoc, uint(bandIndex.y), uBandTextureWidth), 0);
  ivec2 hbandLoc = offsetLoc(glyphLoc, hbandData.y, uBandTextureWidth);
  for (int curveIndex = 0; curveIndex < ${MAX_BAND_CURVES}; curveIndex++) {
    if (curveIndex >= int(hbandData.x)) break;
    uvec4 curveLocData = texelFetch(uBandTexture, offsetLoc(hbandLoc, uint(curveIndex), uBandTextureWidth), 0);
    ivec2 curveLoc = ivec2(curveLocData.xy);
    vec4 p12 = texelFetch(uCurveTexture, curveLoc, 0) - vec4(vRenderCoord, vRenderCoord);
    vec2 p3 = texelFetch(uCurveTexture, ivec2(curveLoc.x + 1, curveLoc.y), 0).xy - vRenderCoord;
    if (max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -0.5) break;
    uint code = calcRootCode(p12.y, p12.w, p3.y);
    if (code != 0u) {
      vec2 r = solveHorizPoly(p12, p3) * pixelsPerEm.x;
      if ((code & 1u) != 0u) {
        xcov += clamp(r.x + 0.5, 0.0, 1.0);
        xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
      }
      if (code > 1u) {
        xcov -= clamp(r.y + 0.5, 0.0, 1.0);
        xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
      }
    }
  }

  float ycov = 0.0;
  float ywgt = 0.0;
  uint verticalHeaderOffset = uint(bandMax.y + 1 + bandIndex.x);
  uvec4 vbandData = texelFetch(uBandTexture, offsetLoc(glyphLoc, verticalHeaderOffset, uBandTextureWidth), 0);
  ivec2 vbandLoc = offsetLoc(glyphLoc, vbandData.y, uBandTextureWidth);
  for (int curveIndex = 0; curveIndex < ${MAX_BAND_CURVES}; curveIndex++) {
    if (curveIndex >= int(vbandData.x)) break;
    uvec4 curveLocData = texelFetch(uBandTexture, offsetLoc(vbandLoc, uint(curveIndex), uBandTextureWidth), 0);
    ivec2 curveLoc = ivec2(curveLocData.xy);
    vec4 p12 = texelFetch(uCurveTexture, curveLoc, 0) - vec4(vRenderCoord, vRenderCoord);
    vec2 p3 = texelFetch(uCurveTexture, ivec2(curveLoc.x + 1, curveLoc.y), 0).xy - vRenderCoord;
    if (max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -0.5) break;
    uint code = calcRootCode(p12.x, p12.z, p3.x);
    if (code != 0u) {
      vec2 r = solveVertPoly(p12, p3) * pixelsPerEm.y;
      if ((code & 1u) != 0u) {
        ycov -= clamp(r.x + 0.5, 0.0, 1.0);
        ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
      }
      if (code > 1u) {
        ycov += clamp(r.y + 0.5, 0.0, 1.0);
        ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
      }
    }
  }
  return calcCoverage(xcov, ycov, xwgt, ywgt);
}

void main() {
  float coverage = slugRender();
  vec4 premul = vec4(vColor.rgb * vColor.a, vColor.a);
  outColor = premul * coverage;
}`;

const SOLID_VS = `#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 2) in vec4 aColor;
uniform vec2 uViewport;
out vec4 vColor;
void main() {
  vec2 p = aPosition / uViewport * 2.0 - 1.0;
  gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
  vColor = aColor;
}`;

const SOLID_FS = `#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 outColor;
void main() {
  outColor = vec4(vColor.rgb * vColor.a, vColor.a);
}`;

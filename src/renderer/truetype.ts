export type FontPoint = { x: number; y: number; on: boolean };

export type SlugCurve = {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  x3: number;
  y3: number;
};

export type GlyphOutline = {
  glyphId: number;
  advanceWidth: number;
  leftSideBearing: number;
  xMin: number;
  yMin: number;
  xMax: number;
  yMax: number;
  curves: SlugCurve[];
};

type TableRecord = { offset: number; length: number };
type CmapFormat4 = {
  format: 4;
  segCount: number;
  endCodes: number[];
  startCodes: number[];
  idDeltas: number[];
  idRangeOffsets: number[];
  idRangeOffsetStart: number;
  subtableOffset: number;
  length: number;
};
type CmapFormat12 = {
  format: 12;
  groups: Array<{ start: number; end: number; startGlyph: number }>;
};
type CmapSubtable = CmapFormat4 | CmapFormat12;

const ARG_1_AND_2_ARE_WORDS = 1;
const ARGS_ARE_XY_VALUES = 2;
const WE_HAVE_A_SCALE = 8;
const MORE_COMPONENTS = 32;
const WE_HAVE_AN_X_AND_Y_SCALE = 64;
const WE_HAVE_A_TWO_BY_TWO = 128;
const WE_HAVE_INSTRUCTIONS = 256;

export class TrueTypeFont {
  readonly unitsPerEm: number;
  readonly ascender: number;
  readonly descender: number;
  readonly lineGap: number;
  readonly glyphCount: number;
  private readonly view: DataView;
  private readonly tables = new Map<string, TableRecord>();
  private readonly glyphOffsets: number[] = [];
  private readonly advanceWidths: number[] = [];
  private readonly leftSideBearings: number[] = [];
  private readonly cmapSubtables: CmapSubtable[] = [];
  private readonly glyphCache = new Map<number, GlyphOutline>();
  private readonly glyphsInProgress = new Set<number>();

  constructor(buffer: ArrayBuffer) {
    this.view = new DataView(buffer);
    this.readTableDirectory();

    const head = this.requireTable("head");
    this.unitsPerEm = this.u16(head.offset + 18);
    const indexToLocFormat = this.i16(head.offset + 50);

    const maxp = this.requireTable("maxp");
    this.glyphCount = this.u16(maxp.offset + 4);

    const hhea = this.requireTable("hhea");
    this.ascender = this.i16(hhea.offset + 4);
    this.descender = this.i16(hhea.offset + 6);
    this.lineGap = this.i16(hhea.offset + 8);
    const hMetricCount = this.u16(hhea.offset + 34);

    this.readHorizontalMetrics(hMetricCount);
    this.readGlyphLocations(indexToLocFormat);
    this.readCmapSubtables();
  }

  glyphIdForCodePoint(codePoint: number): number {
    for (const subtable of this.cmapSubtables) {
      const glyphId = subtable.format === 12 ? this.glyphIdFromFormat12(subtable, codePoint) : this.glyphIdFromFormat4(subtable, codePoint);
      if (glyphId > 0) return glyphId;
    }
    return 0;
  }

  outlineForCodePoint(codePoint: number): GlyphOutline {
    return this.outlineForGlyph(this.glyphIdForCodePoint(codePoint));
  }

  outlineForGlyph(glyphId: number): GlyphOutline {
    const normalizedGlyphId = glyphId >= 0 && glyphId < this.glyphCount ? glyphId : 0;
    const cached = this.glyphCache.get(normalizedGlyphId);
    if (cached) return cached;
    if (this.glyphsInProgress.has(normalizedGlyphId)) throw new Error(`Recursive composite glyph: ${normalizedGlyphId}`);
    this.glyphsInProgress.add(normalizedGlyphId);
    const outline = this.parseGlyph(normalizedGlyphId);
    this.glyphsInProgress.delete(normalizedGlyphId);
    this.glyphCache.set(normalizedGlyphId, outline);
    return outline;
  }

  private readTableDirectory(): void {
    const tableCount = this.u16(4);
    for (let i = 0; i < tableCount; i++) {
      const offset = 12 + i * 16;
      const tag = this.tag(offset);
      this.tables.set(tag, { offset: this.u32(offset + 8), length: this.u32(offset + 12) });
    }
  }

  private readHorizontalMetrics(hMetricCount: number): void {
    const hmtx = this.requireTable("hmtx");
    let lastAdvance = 0;
    for (let i = 0; i < this.glyphCount; i++) {
      if (i < hMetricCount) {
        const offset = hmtx.offset + i * 4;
        lastAdvance = this.u16(offset);
        this.advanceWidths[i] = lastAdvance;
        this.leftSideBearings[i] = this.i16(offset + 2);
      } else {
        const offset = hmtx.offset + hMetricCount * 4 + (i - hMetricCount) * 2;
        this.advanceWidths[i] = lastAdvance;
        this.leftSideBearings[i] = this.i16(offset);
      }
    }
  }

  private readGlyphLocations(indexToLocFormat: number): void {
    const loca = this.requireTable("loca");
    for (let i = 0; i <= this.glyphCount; i++) {
      this.glyphOffsets[i] = indexToLocFormat === 0 ? this.u16(loca.offset + i * 2) * 2 : this.u32(loca.offset + i * 4);
    }
  }

  private readCmapSubtables(): void {
    const cmap = this.requireTable("cmap");
    const count = this.u16(cmap.offset + 2);
    const candidates: Array<{ platform: number; encoding: number; offset: number; format: number }> = [];
    for (let i = 0; i < count; i++) {
      const record = cmap.offset + 4 + i * 8;
      const offset = cmap.offset + this.u32(record + 4);
      candidates.push({ platform: this.u16(record), encoding: this.u16(record + 2), offset, format: this.u16(offset) });
    }
    candidates.sort((a, b) => cmapPriority(b) - cmapPriority(a));
    for (const candidate of candidates) {
      if (candidate.format === 12) this.cmapSubtables.push(this.readFormat12(candidate.offset));
      else if (candidate.format === 4) this.cmapSubtables.push(this.readFormat4(candidate.offset));
    }
    if (this.cmapSubtables.length === 0) throw new Error("TrueType font has no supported cmap subtable");
  }

  private readFormat4(offset: number): CmapFormat4 {
    const length = this.u16(offset + 2);
    const segCount = this.u16(offset + 6) / 2;
    const endCodeOffset = offset + 14;
    const startCodeOffset = endCodeOffset + segCount * 2 + 2;
    const idDeltaOffset = startCodeOffset + segCount * 2;
    const idRangeOffsetStart = idDeltaOffset + segCount * 2;
    const endCodes: number[] = [];
    const startCodes: number[] = [];
    const idDeltas: number[] = [];
    const idRangeOffsets: number[] = [];
    for (let i = 0; i < segCount; i++) {
      endCodes.push(this.u16(endCodeOffset + i * 2));
      startCodes.push(this.u16(startCodeOffset + i * 2));
      idDeltas.push(this.i16(idDeltaOffset + i * 2));
      idRangeOffsets.push(this.u16(idRangeOffsetStart + i * 2));
    }
    return { format: 4, segCount, endCodes, startCodes, idDeltas, idRangeOffsets, idRangeOffsetStart, subtableOffset: offset, length };
  }

  private readFormat12(offset: number): CmapFormat12 {
    const groupCount = this.u32(offset + 12);
    const groups: Array<{ start: number; end: number; startGlyph: number }> = [];
    for (let i = 0; i < groupCount; i++) {
      const groupOffset = offset + 16 + i * 12;
      groups.push({ start: this.u32(groupOffset), end: this.u32(groupOffset + 4), startGlyph: this.u32(groupOffset + 8) });
    }
    return { format: 12, groups };
  }

  private glyphIdFromFormat12(subtable: CmapFormat12, codePoint: number): number {
    let lo = 0;
    let hi = subtable.groups.length - 1;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      const group = subtable.groups[mid]!;
      if (codePoint < group.start) hi = mid - 1;
      else if (codePoint > group.end) lo = mid + 1;
      else return group.startGlyph + codePoint - group.start;
    }
    return 0;
  }

  private glyphIdFromFormat4(subtable: CmapFormat4, codePoint: number): number {
    if (codePoint > 0xffff) return 0;
    for (let i = 0; i < subtable.segCount; i++) {
      if (codePoint > subtable.endCodes[i]!) continue;
      if (codePoint < subtable.startCodes[i]!) return 0;
      const rangeOffset = subtable.idRangeOffsets[i]!;
      const delta = subtable.idDeltas[i]!;
      if (rangeOffset === 0) return (codePoint + delta) & 0xffff;
      const glyphOffset = subtable.idRangeOffsetStart + i * 2 + rangeOffset + (codePoint - subtable.startCodes[i]!) * 2;
      if (glyphOffset < subtable.subtableOffset || glyphOffset + 2 > subtable.subtableOffset + subtable.length) return 0;
      const rawGlyph = this.u16(glyphOffset);
      return rawGlyph === 0 ? 0 : (rawGlyph + delta) & 0xffff;
    }
    return 0;
  }

  private parseGlyph(glyphId: number): GlyphOutline {
    const glyf = this.requireTable("glyf");
    const start = glyf.offset + this.glyphOffsets[glyphId]!;
    const end = glyf.offset + this.glyphOffsets[glyphId + 1]!;
    const advanceWidth = this.advanceWidths[glyphId] ?? this.advanceWidths[0] ?? this.unitsPerEm;
    const leftSideBearing = this.leftSideBearings[glyphId] ?? 0;
    if (start >= end) {
      return { glyphId, advanceWidth, leftSideBearing, xMin: 0, yMin: 0, xMax: 0, yMax: 0, curves: [] };
    }

    const contourCount = this.i16(start);
    const xMin = this.i16(start + 2);
    const yMin = this.i16(start + 4);
    const xMax = this.i16(start + 6);
    const yMax = this.i16(start + 8);
    const curves = contourCount >= 0 ? this.parseSimpleGlyph(start + 10, contourCount) : this.parseCompositeGlyph(start + 10);
    return { glyphId, advanceWidth, leftSideBearing, xMin, yMin, xMax, yMax, curves };
  }

  private parseSimpleGlyph(offset: number, contourCount: number): SlugCurve[] {
    if (contourCount === 0) return [];
    const endPts: number[] = [];
    for (let i = 0; i < contourCount; i++) endPts.push(this.u16(offset + i * 2));
    const instructionLength = this.u16(offset + contourCount * 2);
    let cursor = offset + contourCount * 2 + 2 + instructionLength;
    const pointCount = endPts[endPts.length - 1]! + 1;
    const flags: number[] = [];
    while (flags.length < pointCount) {
      const flag = this.u8(cursor++);
      flags.push(flag);
      if (flag & 8) {
        const repeat = this.u8(cursor++);
        for (let i = 0; i < repeat; i++) flags.push(flag);
      }
    }

    const xs: number[] = [];
    let x = 0;
    for (let i = 0; i < pointCount; i++) {
      const flag = flags[i]!;
      let dx = 0;
      if (flag & 2) dx = this.u8(cursor++) * ((flag & 16) ? 1 : -1);
      else if (!(flag & 16)) {
        dx = this.i16(cursor);
        cursor += 2;
      }
      x += dx;
      xs.push(x);
    }

    const ys: number[] = [];
    let y = 0;
    for (let i = 0; i < pointCount; i++) {
      const flag = flags[i]!;
      let dy = 0;
      if (flag & 4) dy = this.u8(cursor++) * ((flag & 32) ? 1 : -1);
      else if (!(flag & 32)) {
        dy = this.i16(cursor);
        cursor += 2;
      }
      y += dy;
      ys.push(y);
    }

    const curves: SlugCurve[] = [];
    let startPoint = 0;
    for (const endPoint of endPts) {
      const contour: FontPoint[] = [];
      for (let i = startPoint; i <= endPoint; i++) contour.push({ x: xs[i]!, y: ys[i]!, on: Boolean(flags[i]! & 1) });
      curves.push(...contourToQuadraticCurves(contour));
      startPoint = endPoint + 1;
    }
    return curves;
  }

  private parseCompositeGlyph(offset: number): SlugCurve[] {
    const curves: SlugCurve[] = [];
    let cursor = offset;
    let flags = MORE_COMPONENTS;
    while (flags & MORE_COMPONENTS) {
      flags = this.u16(cursor);
      cursor += 2;
      const componentGlyphId = this.u16(cursor);
      cursor += 2;
      let arg1 = 0;
      let arg2 = 0;
      if (flags & ARG_1_AND_2_ARE_WORDS) {
        arg1 = this.i16(cursor);
        arg2 = this.i16(cursor + 2);
        cursor += 4;
      } else {
        arg1 = this.i8(cursor);
        arg2 = this.i8(cursor + 1);
        cursor += 2;
      }

      let a = 1;
      let b = 0;
      let c = 0;
      let d = 1;
      if (flags & WE_HAVE_A_SCALE) {
        a = d = this.f2dot14(cursor);
        cursor += 2;
      } else if (flags & WE_HAVE_AN_X_AND_Y_SCALE) {
        a = this.f2dot14(cursor);
        d = this.f2dot14(cursor + 2);
        cursor += 4;
      } else if (flags & WE_HAVE_A_TWO_BY_TWO) {
        a = this.f2dot14(cursor);
        b = this.f2dot14(cursor + 2);
        c = this.f2dot14(cursor + 4);
        d = this.f2dot14(cursor + 6);
        cursor += 8;
      }

      const dx = (flags & ARGS_ARE_XY_VALUES) ? arg1 : 0;
      const dy = (flags & ARGS_ARE_XY_VALUES) ? arg2 : 0;
      const component = this.outlineForGlyph(componentGlyphId);
      for (const curve of component.curves) curves.push(transformCurve(curve, a, b, c, d, dx, dy));
    }
    if (flags & WE_HAVE_INSTRUCTIONS) {
      const instructionLength = this.u16(cursor);
      cursor += 2 + instructionLength;
    }
    return curves;
  }

  private requireTable(tag: string): TableRecord {
    const table = this.tables.get(tag);
    if (!table) throw new Error(`TrueType font is missing required ${tag} table`);
    return table;
  }

  private tag(offset: number): string {
    return String.fromCharCode(this.u8(offset), this.u8(offset + 1), this.u8(offset + 2), this.u8(offset + 3));
  }

  private u8(offset: number): number { return this.view.getUint8(offset); }
  private i8(offset: number): number { return this.view.getInt8(offset); }
  private u16(offset: number): number { return this.view.getUint16(offset, false); }
  private i16(offset: number): number { return this.view.getInt16(offset, false); }
  private u32(offset: number): number { return this.view.getUint32(offset, false); }
  private f2dot14(offset: number): number { return this.i16(offset) / 16384; }
}

export function contourToQuadraticCurves(contour: FontPoint[]): SlugCurve[] {
  if (contour.length === 0) return [];
  const curves: SlugCurve[] = [];
  const last = contour[contour.length - 1]!;
  const first = contour[0]!;
  let current: FontPoint;
  let index: number;
  if (first.on) {
    current = first;
    index = 1;
  } else if (last.on) {
    current = last;
    index = 0;
  } else {
    current = midpoint(last, first);
    index = 0;
  }

  let processed = 0;
  while (processed < contour.length) {
    const point = contour[index % contour.length]!;
    if (point.on) {
      pushLine(curves, current, point);
      current = point;
      index++;
      processed++;
      continue;
    }

    const next = contour[(index + 1) % contour.length]!;
    if (next.on) {
      curves.push({ x1: current.x, y1: current.y, x2: point.x, y2: point.y, x3: next.x, y3: next.y });
      current = next;
      index += 2;
      processed += 2;
    } else {
      const implicit = midpoint(point, next);
      curves.push({ x1: current.x, y1: current.y, x2: point.x, y2: point.y, x3: implicit.x, y3: implicit.y });
      current = implicit;
      index++;
      processed++;
    }
  }

  if (current.x !== (first.on ? first.x : (last.on ? last.x : midpoint(last, first).x)) || current.y !== (first.on ? first.y : (last.on ? last.y : midpoint(last, first).y))) {
    const start = first.on ? first : last.on ? last : midpoint(last, first);
    pushLine(curves, current, start);
  }
  return curves.filter((curve) => curve.x1 !== curve.x3 || curve.y1 !== curve.y3 || curve.x1 !== curve.x2 || curve.y1 !== curve.y2);
}

export function pushLine(curves: SlugCurve[], from: { x: number; y: number }, to: { x: number; y: number }): void {
  if (from.x === to.x && from.y === to.y) return;
  curves.push({ x1: from.x, y1: from.y, x2: (from.x + to.x) / 2, y2: (from.y + to.y) / 2, x3: to.x, y3: to.y });
}

function midpoint(a: { x: number; y: number }, b: { x: number; y: number }): FontPoint {
  return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2, on: true };
}

function transformCurve(curve: SlugCurve, a: number, b: number, c: number, d: number, dx: number, dy: number): SlugCurve {
  const p1 = transformPoint(curve.x1, curve.y1, a, b, c, d, dx, dy);
  const p2 = transformPoint(curve.x2, curve.y2, a, b, c, d, dx, dy);
  const p3 = transformPoint(curve.x3, curve.y3, a, b, c, d, dx, dy);
  return { x1: p1.x, y1: p1.y, x2: p2.x, y2: p2.y, x3: p3.x, y3: p3.y };
}

function transformPoint(x: number, y: number, a: number, b: number, c: number, d: number, dx: number, dy: number): { x: number; y: number } {
  return { x: a * x + c * y + dx, y: b * x + d * y + dy };
}

function cmapPriority(candidate: { platform: number; encoding: number; format: number }): number {
  let score = candidate.format === 12 ? 100 : candidate.format === 4 ? 50 : 0;
  if (candidate.platform === 3 && candidate.encoding === 10) score += 30;
  if (candidate.platform === 3 && candidate.encoding === 1) score += 20;
  if (candidate.platform === 0) score += 10;
  return score;
}

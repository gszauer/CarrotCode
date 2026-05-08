import type { Rect } from "../shared/types";

export type ViewportInfo = {
  cssWidth: number;
  cssHeight: number;
  deviceWidth: number;
  deviceHeight: number;
  dpr: number;
  visualWidth: number;
  visualHeight: number;
  visualOffsetLeft: number;
  visualOffsetTop: number;
};

export class ViewportService {
  private readonly listeners = new Set<(info: ViewportInfo) => void>();
  private current: ViewportInfo | null = null;
  private resizeObserver: ResizeObserver | null = null;
  private visualViewportCanvasResizeEnabled = true;
  private visualViewportCanvasResizeDeferredUntil = 0;
  private visualViewportCanvasResizeDeferredTimer = 0;

  constructor(private readonly canvas: HTMLCanvasElement) {}

  start(): void {
    this.resizeObserver = new ResizeObserver(() => this.update());
    this.resizeObserver.observe(this.canvas);
    window.addEventListener("resize", this.update);
    window.visualViewport?.addEventListener("resize", this.update);
    window.visualViewport?.addEventListener("scroll", this.update);
    this.update();
  }

  stop(): void {
    this.resizeObserver?.disconnect();
    window.removeEventListener("resize", this.update);
    window.visualViewport?.removeEventListener("resize", this.update);
    window.visualViewport?.removeEventListener("scroll", this.update);
    if (this.visualViewportCanvasResizeDeferredTimer) window.clearTimeout(this.visualViewportCanvasResizeDeferredTimer);
    this.visualViewportCanvasResizeDeferredTimer = 0;
  }

  get(): ViewportInfo {
    if (!this.current) this.current = this.compute();
    return this.current;
  }

  onChange(listener: (info: ViewportInfo) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  resizeCanvas(gl: WebGL2RenderingContext): boolean {
    this.applyVisualViewportSize();
    const info = this.compute();
    const changed = this.canvas.width !== info.deviceWidth || this.canvas.height !== info.deviceHeight;
    if (changed) {
      this.canvas.width = info.deviceWidth;
      this.canvas.height = info.deviceHeight;
      gl.viewport(0, 0, info.deviceWidth, info.deviceHeight);
    }
    this.current = info;
    return changed;
  }

  setVisualViewportCanvasResizeEnabled(enabled: boolean): void {
    if (this.visualViewportCanvasResizeEnabled === enabled) return;
    this.visualViewportCanvasResizeEnabled = enabled;
    this.update();
  }

  deferVisualViewportCanvasResize(ms: number): void {
    const duration = Math.max(0, ms);
    if (duration <= 0) return;
    const until = performance.now() + duration;
    this.visualViewportCanvasResizeDeferredUntil = Math.max(this.visualViewportCanvasResizeDeferredUntil, until);
    if (this.visualViewportCanvasResizeDeferredTimer) window.clearTimeout(this.visualViewportCanvasResizeDeferredTimer);
    this.visualViewportCanvasResizeDeferredTimer = window.setTimeout(() => {
      this.visualViewportCanvasResizeDeferredTimer = 0;
      this.update();
    }, Math.max(16, Math.ceil(this.visualViewportCanvasResizeDeferredUntil - performance.now()) + 16));
    this.update();
  }

  isVisualViewportCanvasResizeDeferred(): boolean {
    return performance.now() < this.visualViewportCanvasResizeDeferredUntil;
  }

  pointerToCanvasCss(e: PointerEvent): { x: number; y: number } {
    const rect = this.canvas.getBoundingClientRect();
    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
  }

  snapCss(value: number): number {
    const dpr = this.get().dpr;
    return Math.round(value * dpr) / dpr;
  }

  private readonly update = (): void => {
    this.applyVisualViewportSize();
    this.current = this.compute();
    for (const listener of this.listeners) listener(this.current);
  };

  private applyVisualViewportSize(): void {
    if (!this.visualViewportCanvasResizeEnabled) return;
    if (this.isVisualViewportCanvasResizeDeferred()) return;
    const vv = window.visualViewport;
    if (!vv) {
      this.canvas.style.width = "";
      this.canvas.style.height = "";
      return;
    }
    this.canvas.style.width = `${Math.max(1, vv.width)}px`;
    this.canvas.style.height = `${Math.max(1, vv.height)}px`;
  }

  private compute(): ViewportInfo {
    const rect = this.canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const vv = window.visualViewport;
    return {
      cssWidth: Math.max(1, rect.width),
      cssHeight: Math.max(1, rect.height),
      deviceWidth: Math.max(1, Math.round(rect.width * dpr)),
      deviceHeight: Math.max(1, Math.round(rect.height * dpr)),
      dpr,
      visualWidth: vv?.width ?? window.innerWidth,
      visualHeight: vv?.height ?? window.innerHeight,
      visualOffsetLeft: vv?.offsetLeft ?? 0,
      visualOffsetTop: vv?.offsetTop ?? 0
    };
  }
}

export function placeRectInViewport(rect: Rect, viewport: ViewportInfo): Rect {
  return {
    x: Math.max(0, Math.min(rect.x - viewport.visualOffsetLeft, viewport.visualWidth - rect.w)),
    y: Math.max(0, Math.min(rect.y - viewport.visualOffsetTop, viewport.visualHeight - rect.h)),
    w: rect.w,
    h: rect.h
  };
}

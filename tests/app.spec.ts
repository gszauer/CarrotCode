import { expect, test } from "@playwright/test";
import JSZip from "jszip";
import { readFile } from "node:fs/promises";

const TOUCH_KEYBOARD_SETTLE_TEST_MS = 1050;

const SAMPLE_WORKSPACE_FILES: Array<{ path: string; text: string; mime: string }> = [
  { path: "/README.md", text: "# Carrot Editor\n\nThis workspace is stored in IndexedDB.\n\n- Open files from the left sidebar.\n- Edit text in the WebGL2 editor.\n- Use Search to scan files.\n- Use Chat for local assistant turns.\n", mime: "text/plain" },
  { path: "/src/main.ts", text: "export function greet(name: string): string {\n  return `hello ${name}`;\n}\n\nconsole.log(greet('Carrot'));\n", mime: "text/plain" },
  { path: "/notes/shortcuts.txt", text: "Ctrl/Cmd+C copy\nCtrl/Cmd+X cut\nCtrl/Cmd+V paste\nCtrl/Cmd+S save\nCtrl/Cmd+Shift+F project search\n", mime: "text/plain" }
];

async function resetAndLoad(page: import("@playwright/test").Page, testInfo: import("@playwright/test").TestInfo, options: { seed?: boolean } = {}) {
  const dbName = testDatabaseName(testInfo);
  await page.goto("/");
  await page.evaluate(async (db) => {
    localStorage.removeItem("slug.settings");
    localStorage.removeItem("slug.aiProvider");
    localStorage.removeItem("slug.aiEndpointConfig");
    localStorage.removeItem("slug.aiSystemPrompt");
    localStorage.removeItem("slug.aiCompactPrompt");
    localStorage.removeItem("slug.aiTagToolPrompt");
    localStorage.removeItem("slug.aiHarmonyToolPrompt");
    localStorage.removeItem("slug.session");
    for (const key of Object.keys(localStorage)) {
      if (key.startsWith("slug.session:")) localStorage.removeItem(key);
    }
    const name = `slug-editor-${db}`;
    await new Promise<void>((resolve) => {
      const req = indexedDB.deleteDatabase(name);
      req.onsuccess = () => resolve();
      req.onerror = () => resolve();
      req.onblocked = () => resolve();
    });
  }, dbName);
  await page.goto(`/?db=${encodeURIComponent(dbName)}`);
  await expect.poll(() => page.evaluate(() => Boolean(window.__slugApp))).toBe(true);
  if (options.seed !== false) await seedSampleWorkspace(page);
}

async function seedSampleWorkspace(page: import("@playwright/test").Page): Promise<void> {
  await page.evaluate(async (files) => {
    const app = window.__slugApp! as any;
    for (const file of files) await app.vfs.writeFile(file.path, file.text, file.mime);
    await app.refreshFiles();
    app.draw();
  }, SAMPLE_WORKSPACE_FILES);
}

async function appState<T = any>(page: import("@playwright/test").Page): Promise<T> {
  return page.evaluate(() => window.__slugApp!.getStateForTests() as T);
}

async function readTextIfExists(page: import("@playwright/test").Page, path: string): Promise<string | null> {
  return page.evaluate(async (filePath) => {
    const app = window.__slugApp!;
    return await app.vfs.stat(filePath) ? await app.vfs.readText(filePath) : null;
  }, path);
}

test.beforeEach(async ({ page, context, browserName }, testInfo) => {
  if (browserName === "chromium") {
    await context.grantPermissions(["clipboard-read", "clipboard-write"], { origin: "http://127.0.0.1:4173" });
  }
  await resetAndLoad(page, testInfo);
});

test("renders nonblank WebGL editor and sizes canvas for the device pixel ratio", async ({ page }, testInfo) => {
  const canvas = page.locator("#editor-canvas");
  await expect(canvas).toBeVisible();

  const state = await appState<{ renderer: RendererDiagnostics; canvas: { width: number; height: number; cssWidth: number; cssHeight: number } }>(page);
  expect(state.renderer.backend).toBe("slug-ttf");
  expect(state.renderer.font).toBe("Inter-Regular.ttf");
  expect(state.renderer.unitsPerEm).toBeGreaterThan(0);
  expect(state.renderer.glyphCount).toBeGreaterThan(1000);
  expect(state.renderer.fonts.map((font) => font.name)).toEqual(["Inter-Regular.ttf", "NotoEmoji-Regular.ttf", "MonaspaceNeon-Regular.ttf"]);
  expect(state.renderer.fonts.find((font) => font.name === "NotoEmoji-Regular.ttf")?.glyphCount).toBeGreaterThan(100);
  expect(state.renderer.fonts.find((font) => font.name === "MonaspaceNeon-Regular.ttf")?.glyphCount).toBeGreaterThan(1000);
  const resolvedFonts = await page.evaluate(() => ({
    latin: window.__slugApp!.renderer.resolveCodePoint("A".codePointAt(0)!).font,
    codeLatin: window.__slugApp!.renderer.resolveCodePoint("A".codePointAt(0)!, "code").font,
    emoji: window.__slugApp!.renderer.resolveCodePoint("😀".codePointAt(0)!).font,
    gutterDigit: window.__slugApp!.renderer.resolveCodePoint("1".codePointAt(0)!, "gutter").font,
    gutterOnes: window.__slugApp!.renderer.measureText("111", "gutter"),
    gutterEights: window.__slugApp!.renderer.measureText("888", "gutter")
  }));
  expect(resolvedFonts.latin).toBe("Inter-Regular.ttf");
  expect(resolvedFonts.codeLatin).toBe("Inter-Regular.ttf");
  expect(resolvedFonts.emoji).toBe("NotoEmoji-Regular.ttf");
  expect(resolvedFonts.gutterDigit).toBe("MonaspaceNeon-Regular.ttf");
  expect(resolvedFonts.gutterOnes).toBeCloseTo(resolvedFonts.gutterEights, 4);
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    const doc = window.__slugApp!.activeDoc();
    doc?.selectAll();
    doc?.replaceSelection("Emoji fallback 😀\n");
    app.draw();
  });
  expect(state.canvas.width).toBeGreaterThanOrEqual(Math.floor(state.canvas.cssWidth));
  expect(state.canvas.height).toBeGreaterThanOrEqual(Math.floor(state.canvas.cssHeight));

  const pixelStats = await page.evaluate(() => {
    const canvas = document.querySelector("#editor-canvas") as HTMLCanvasElement;
    const gl = canvas.getContext("webgl2")!;
    const pixels = new Uint8Array(canvas.width * canvas.height * 4);
    gl.readPixels(0, 0, canvas.width, canvas.height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    let nonBackground = 0;
    for (let i = 0; i < pixels.length; i += 4) {
      if (pixels[i] !== 31 || pixels[i + 1] !== 33 || pixels[i + 2] !== 38) nonBackground++;
    }
    return { nonBackground, total: pixels.length / 4 };
  });
  expect(pixelStats.nonBackground).toBeGreaterThan(pixelStats.total * 0.01);

  await page.screenshot({ path: testInfo.outputPath("editor-render.png"), fullPage: true });
});

test("publishes installable PWA assets", async ({ page, browserName }) => {
  await expect(page).toHaveTitle("carrot.code");
  await expect(page.locator('link[rel="manifest"]')).toHaveAttribute("href", "./manifest.webmanifest");
  await expect(page.locator('link[rel="apple-touch-icon"]')).toHaveAttribute("href", "./icon-180.png");
  await expect(page.locator('link[rel="icon"]')).toHaveAttribute("href", "./icon-192.png");
  await expect(page.locator('meta[name="apple-mobile-web-app-capable"]')).toHaveAttribute("content", "yes");
  await expect(page.locator('meta[name="apple-mobile-web-app-title"]')).toHaveAttribute("content", "carrot.code");

  const manifest = await page.evaluate(async () => {
    const response = await fetch("./manifest.webmanifest");
    return response.json();
  });
  expect(manifest.name).toBe("carrot.code");
  expect(manifest.short_name).toBe("carrot.code");
  expect(manifest.display).toBe("standalone");
  expect(manifest.icons.map((icon: { src: string }) => icon.src)).toEqual(["./icon-192.png", "./icon-512.png", "./icon-maskable-512.png"]);

  const serviceWorkerText = await page.evaluate(async () => {
    const response = await fetch("./sw.js");
    return response.text();
  });
  expect(serviceWorkerText).toContain("carrot-app-shell");

  if (browserName !== "chromium") return;
  await expect.poll(() => page.evaluate(async () => {
    if (!("serviceWorker" in navigator)) return false;
    const registration = await navigator.serviceWorker.ready;
    return Boolean(registration.active?.scriptURL.endsWith("/sw.js"));
  })).toBe(true);
  await page.context().setOffline(true);
  try {
    await page.reload({ waitUntil: "domcontentloaded" });
    await expect(page.locator("#editor-canvas")).toBeVisible();
    await expect.poll(() => page.evaluate(() => Boolean(window.__slugApp))).toBe(true);
  } finally {
    await page.context().setOffline(false);
  }
});

test("starts without auto-opening a workspace file", async ({ page }) => {
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBeUndefined();
});

test("fresh workspaces start without default files", async ({ page }, testInfo) => {
  await resetAndLoad(page, testInfo, { seed: false });
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.listAllFiles().then((files) => files.map((file) => file.path)))).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).filePanelEmptyHint).toBe("right click or double tap header to create file");
});

test("restores open files and dock layout on reload", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Dock restore is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual(["/README.md", "/src/main.ts"]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toEqual(expect.arrayContaining(["/README.md", "/src/main.ts"]));
  let state = await appState<CanvasTargets>(page);
  const mainTab = targetFor(state.tabTargets, "/src/main.ts");
  const group = state.editorGroups[0]!;
  await drag(page, center(mainTab), { x: group.frameRect.x + group.frameRect.w - 24, y: group.frameRect.y + group.frameRect.h / 2 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(2);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.session"))).not.toBeNull();
  await expect.poll(() => page.evaluate(() => Object.keys(localStorage).some((key) => key.startsWith("slug.session:")))).toBe(false);

  await page.reload();
  await expect.poll(() => page.evaluate(() => Boolean(window.__slugApp))).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual(["/README.md", "/src/main.ts"]);
  state = await appState<CanvasTargets>(page);
  expect(groupsByX(state.editorGroups).map((item) => item.activePath)).toEqual(["/README.md", "/src/main.ts"]);
});

test("can disable restoring open files", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Session restore is covered in desktop Chromium.");
  await openReadme(page);
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settings = { ...app.settings, rememberOpenFiles: false };
    app.saveAndApplySettings();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.rememberOpenFiles).toBe(false);
  await page.reload();
  await expect.poll(() => page.evaluate(() => Boolean(window.__slugApp))).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual([]);
});

test("imports files into the IndexedDB VFS and opens them from app state", async ({ page }) => {
  await page.evaluate(async () => {
    const file = new File(["alpha\nbeta\n"], "dragged.txt", { type: "text/plain" });
    await window.__slugImportFiles!([file]);
  });

  const state = await appState<{ openTabs: string[] }>(page);
  expect(state.openTabs).toEqual([]);

  await page.evaluate(async () => {
    await window.__slugApp!.openFile("/dragged.txt");
  });
  const opened = await appState<{ activePath: string; activeText: string }>(page);
  expect(opened.activePath).toBe("/dragged.txt");
  expect(opened.activeText).toContain("alpha");
});

test("unsupported workspace files open read-only without overwriting bytes", async ({ page }) => {
  const original = [137, 80, 78, 71, 13, 10];
  await page.evaluate(async (bytes) => {
    const app = window.__slugApp!;
    await app.vfs.writeFile("/assets/logo.png", new Uint8Array(bytes), "image/png");
    await app.refreshFiles();
    await app.openFile("/assets/logo.png");
  }, original);

  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/assets/logo.png");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("File type not supported");
  await expect.poll(() => page.evaluate(() => ({
    dirty: window.__slugApp!.activeDoc()?.dirty ?? true,
    readOnly: window.__slugApp!.activeDoc()?.readOnly ?? false
  }))).toEqual({ dirty: false, readOnly: true });

  await page.keyboard.type("should not edit");
  await page.keyboard.press(`${desktopShortcutModifier()}+S`);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("File type not supported");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? true)).toBe(false);
  await expect.poll(() => page.evaluate(async () => Array.from(await window.__slugApp!.vfs.readFile("/assets/logo.png")))).toEqual(original);
});

test("dragging files over the canvas shows workspace upload feedback", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Synthetic drag/drop behavior is covered in desktop Chromium.");
  await dispatchCanvasDrag(page, "dragenter", [{ name: "upload.txt", text: "hello", type: "text/plain" }]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileDragActive).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileDragLabel).toBe("Drop to open in memory");

  const zipData = await zipBytes({ "src/app.ts": "export const zip = true;\n" });
  await dispatchCanvasDrag(page, "dragover", [{ name: "workspace.zip", bytes: zipData, type: "application/zip" }]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileDragLabel).toBe("Drop to import workspace zip");

  await dispatchCanvasDrag(page, "dragleave", [], { x: -8, y: -8 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileDragActive).toBe(false);
});

test("dropping regular files opens dirty memory tabs and saves with preferred root names", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Synthetic drag/drop behavior is covered in desktop Chromium.");
  const mod = desktopShortcutModifier();
  await dispatchCanvasDrag(page, "drop", [{ name: "upload.txt", text: "uploaded text\n", type: "text/plain" }]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("upload.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("uploaded text\n");
  await expect.poll(() => page.evaluate(() => ({ path: window.__slugApp!.activeDoc()?.path ?? null, dirty: window.__slugApp!.activeDoc()?.dirty ?? false }))).toEqual({ path: null, dirty: true });

  await page.keyboard.press(`${mod}+S`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.path)).toBe("/upload.txt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.readText("/upload.txt"))).toBe("uploaded text\n");

  await dispatchCanvasDrag(page, "drop", [{ name: "README.md", text: "collision upload\n", type: "text/plain" }]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("README.md");
  await page.keyboard.press(`${mod}+S`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.path ?? "")).toMatch(/^\/[0-9a-f]{8}\.txt$/);
  const fallbackPath = await page.evaluate(() => window.__slugApp!.activeDoc()?.path ?? "");
  expect(fallbackPath).toMatch(/^\/[0-9a-f]{8}\.txt$/);
  await expect.poll(() => page.evaluate((path) => window.__slugApp!.vfs.readText(path), fallbackPath)).toBe("collision upload\n");
});

test("unsupported dropped files open read-only placeholder tabs", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Synthetic drag/drop behavior is covered in desktop Chromium.");
  await dispatchCanvasDrag(page, "drop", [{ name: "logo.png", bytes: [137, 80, 78, 71], type: "image/png" }]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("logo.png");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("File type not supported");
  await expect.poll(() => page.evaluate(() => ({
    path: window.__slugApp!.activeDoc()?.path ?? null,
    dirty: window.__slugApp!.activeDoc()?.dirty ?? false,
    readOnly: window.__slugApp!.activeDoc()?.readOnly ?? false
  }))).toEqual({ path: null, dirty: false, readOnly: true });

  await page.keyboard.type("should not edit");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("File type not supported");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? true)).toBe(false);
});

test("dropping zip files can append to or replace the workspace", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Synthetic zip drag/drop behavior is covered in desktop Chromium.");
  const appendZip = await zipBytes({ "extras/appended.txt": "appended from zip\n" });
  await dispatchCanvasDrag(page, "drop", [{ name: "append.zip", bytes: appendZip, type: "application/zip" }]);
  let state = await waitForModal(page);
  expect(state.modal?.kind).toBe("zipImport");
  expect(state.modal?.buttons.map((button) => button.action)).toEqual(["replace", "append", "cancel"]);
  await clickModalButton(page, state, "append");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.stat("/extras/appended.txt").then(Boolean))).toBe(true);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.readText("/extras/appended.txt"))).toBe("appended from zip\n");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.stat("/README.md").then(Boolean))).toBe(true);

  const replaceZip = await zipBytes({ "only/file.txt": "replacement zip\n" });
  await dispatchCanvasDrag(page, "drop", [{ name: "replace.zip", bytes: replaceZip, type: "application/zip" }]);
  state = await waitForModal(page);
  await clickModalButton(page, state, "replace");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.stat("/README.md").then(Boolean))).toBe(false);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.stat("/only/file.txt").then(Boolean))).toBe(true);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.readText("/only/file.txt"))).toBe("replacement zip\n");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual([]);
});

test("files sidebar renders collapsible folders", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas tree hit geometry is covered in desktop Chromium.");
  let state = await appState<CanvasTargets>(page);
  expect(state.folderTargets.map((item) => item.path)).toEqual(["/notes", "/src"]);
  expect(state.fileTargets.map((item) => item.path)).toContain("/notes/shortcuts.txt");

  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/notes"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).folderTargets.find((item) => item.path === "/notes")?.expanded).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).not.toContain("/notes/shortcuts.txt");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/notes"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).folderTargets.find((item) => item.path === "/notes")?.expanded).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/notes/shortcuts.txt");
});

test("files sidebar rows hover and folders can be selected", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas tree hover state is covered in desktop Chromium.");
  let state = await appState<CanvasTargets>(page);
  const notes = targetFor(state.folderTargets, "/notes");
  const sample = { x: notes.x + notes.w - 4, y: notes.y + notes.h / 2 };
  const before = await canvasPixel(page, sample.x, sample.y);
  await page.mouse.move(...pointArgs(center(notes)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).hoveredFileTreePath).toBe("/notes");
  await expect.poll(async () => {
    const after = await canvasPixel(page, sample.x, sample.y);
    return Math.abs(after[0]! - before[0]!) + Math.abs(after[1]! - before[1]!) + Math.abs(after[2]! - before[2]!);
  }).toBeGreaterThan(10);

  await page.mouse.click(...pointArgs(center(notes)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).selectedFileTreePath).toBe("/notes");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/notes"))));
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.fileTargets, "/notes/shortcuts.txt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).selectedFileTreePath).toBe("/notes/shortcuts.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/notes/shortcuts.txt");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/src"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).selectedFileTreePath).toBe("/src");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.editorGroups[0]!.editorRect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).selectedFileTreePath).toBe("/notes/shortcuts.txt");
});

test("files sidebar scrolls overflowing trees", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas sidebar wheel behavior is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    for (let i = 0; i < 50; i++) {
      await app.vfs.writeFile(`/zz-${String(i).padStart(2, "0")}.txt`, `file ${i}\n`, "text/plain");
    }
    await app.refreshFiles();
    app.scheduleDraw();
  });

  let state = await appState<CanvasTargets>(page);
  expect(state.sidebarScrollbars.some((item) => item.panel === "files")).toBe(true);
  expect(state.fileTargets.map((item) => item.path)).not.toContain("/zz-49.txt");
  const body = state.filesRootTarget!;
  await page.mouse.move(body.x + body.w / 2, body.y + body.h - 12);
  await page.mouse.wheel(0, 2400);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).filesScrollY).toBeGreaterThan(0);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/zz-49.txt");
});

test("files header opens the root context menu", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas context menu geometry is covered in desktop Chromium.");
  const state = await appState<CanvasTargets>(page);
  const body = state.filesRootTarget!;
  await page.mouse.click(body.x + 24, body.y - 16, { button: "right" });
  const menuState = await waitForContextMenu(page);
  expect(menuState.contextMenu?.scope).toEqual({ type: "root", path: "/" });
  expect(menuState.contextMenu?.items.map((item) => item.command)).toEqual(["createFile", "createFolder", "uploadFile"]);
});

test("folder and root context menus create, rename, and delete tree items", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas context menu geometry is covered in desktop Chromium.");
  const mod = desktopShortcutModifier();

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/notes"))), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "folder", path: "/notes" });
  expect(menuItem(state, "rename").enabled).toBe(true);
  expect(menuItem(state, "delete").enabled).toBe(true);
  expect(menuItem(state, "createFile").enabled).toBe(true);
  expect(menuItem(state, "createFolder").enabled).toBe(true);
  expect(menuItem(state, "uploadFile").enabled).toBe(true);
  expect(state.contextMenu?.items.some((item) => item.command === "duplicate")).toBe(false);

  await clickMenuItem(page, state, "createFolder");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toMatch(/^\/notes\/[0-9a-f]{8}$/);
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("drafts");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).folderTargets.map((item) => item.path)).toContain("/notes/drafts");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/notes/drafts"))), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "createFile");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toMatch(/^\/notes\/drafts\/[0-9a-f]{8}\.txt$/);
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("idea.txt");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/notes/drafts/idea.txt");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/notes/drafts"))), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "rename");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBe("/notes/drafts");
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("renamed-drafts");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).folderTargets.map((item) => item.path)).toContain("/notes/renamed-drafts");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/notes/renamed-drafts/idea.txt");

  state = await appState<CanvasTargets>(page);
  const rootPoint = { x: state.filesRootTarget!.x + 24, y: state.filesRootTarget!.y + state.filesRootTarget!.h - 24 };
  await page.mouse.click(rootPoint.x, rootPoint.y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "root", path: "/" });
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["createFile", "createFolder", "uploadFile"]);
  await clickMenuItem(page, state, "createFile");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toMatch(/^\/[0-9a-f]{8}\.txt$/);
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("root-note.txt");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/root-note.txt");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/notes/renamed-drafts"))), { button: "right" });
  state = await waitForContextMenu(page);
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/notes/renamed-drafts/idea.txt");
    app.activeDoc()?.replaceSelection("unsaved ");
    app.scheduleDraw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toContain("/notes/renamed-drafts/idea.txt");
  await clickMenuItem(page, state, "delete");
  state = await waitForModal(page);
  expect(state.modal?.kind).toBe("deleteFolder");
  expect(state.modal?.title).toBe("Delete non-empty folder?");
  expect(state.modal?.message).toContain("/notes/renamed-drafts");
  expect(state.modal?.buttons.map((item) => item.action)).toEqual(["delete", "cancel"]);
  await clickModalButton(page, state, "delete");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).folderTargets.map((item) => item.path)).not.toContain("/notes/renamed-drafts");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).not.toContain("/notes/renamed-drafts/idea.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).not.toContain("/notes/renamed-drafts/idea.txt");
});

test("root and folder context menus upload files with conflict-safe names", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Native file picker upload is covered in desktop Chromium.");

  let state = await appState<CanvasTargets>(page);
  const rootPoint = { x: state.filesRootTarget!.x + 24, y: state.filesRootTarget!.y + state.filesRootTarget!.h - 24 };
  await page.mouse.click(rootPoint.x, rootPoint.y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "root", path: "/" });
  const rootChooserPromise = page.waitForEvent("filechooser");
  await clickMenuItem(page, state, "uploadFile");
  const rootChooser = await rootChooserPromise;
  await rootChooser.setFiles({ name: "README.md", mimeType: "text/markdown", buffer: Buffer.from("uploaded root readme") });
  await expect.poll(() => readTextIfExists(page, "/README 2.md")).toBe("uploaded root readme");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/README 2.md");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.folderTargets, "/notes"))), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "folder", path: "/notes" });
  const folderChooserPromise = page.waitForEvent("filechooser");
  await clickMenuItem(page, state, "uploadFile");
  const folderChooser = await folderChooserPromise;
  await folderChooser.setFiles([
    { name: "shortcuts.txt", mimeType: "text/plain", buffer: Buffer.from("uploaded shortcuts") },
    { name: "bad:name?.txt", mimeType: "text/plain", buffer: Buffer.from("sanitized") }
  ]);
  await expect.poll(() => readTextIfExists(page, "/notes/shortcuts 2.txt")).toBe("uploaded shortcuts");
  await expect.poll(() => readTextIfExists(page, "/notes/bad_name_.txt")).toBe("sanitized");
});

test("sidebar files support double click rename and context menu file actions", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas context menu geometry is covered in desktop Chromium.");
  const mod = desktopShortcutModifier();

  let state = await appState<CanvasTargets>(page);
  await page.mouse.dblclick(...pointArgs(center(targetFor(state.fileTargets, "/notes/shortcuts.txt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBe("/notes/shortcuts.txt");
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("renamed.txt");
  await page.mouse.click(92, 158);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/notes/renamed.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).not.toContain("/notes/shortcuts.txt");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.fileTargets, "/notes/renamed.txt"))), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "file", path: "/notes/renamed.txt" });
  expect(menuItem(state, "rename").enabled).toBe(true);
  expect(menuItem(state, "duplicate").enabled).toBe(true);
  expect(menuItem(state, "delete").enabled).toBe(true);
  await clickMenuItem(page, state, "duplicate");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/notes/renamed copy.txt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.readText("/notes/renamed copy.txt"))).toContain("Ctrl/Cmd+C copy");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.fileTargets, "/notes/renamed.txt"))), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "rename");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBe("/notes/renamed.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameInputRect).not.toBeNull();
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("from-menu.txt");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/notes/from-menu.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).not.toContain("/notes/renamed.txt");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.fileTargets, "/notes/renamed copy.txt"))), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "delete");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).not.toContain("/notes/renamed copy.txt");
});

test("rename field supports text context menu and double click word selection", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas context menu and clipboard behavior is covered in desktop Chromium.");
  let state = await appState<CanvasTargets>(page);
  await page.mouse.dblclick(...pointArgs(center(targetFor(state.fileTargets, "/notes/shortcuts.txt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBe("/notes/shortcuts.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameInputRect).not.toBeNull();

  state = await appState<CanvasTargets>(page);
  const input = state.renameInputRect!;
  const y = center(input).y;
  await page.mouse.click(input.x + 108, y);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameSelectedText).toBe("");
  await page.mouse.dblclick(input.x + 16, y);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameSelectedText).toBe("shortcuts");
  await page.mouse.click(input.x + 16, y, { clickCount: 3 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameSelectedText).toBe("shortcuts.txt");
  await page.mouse.dblclick(input.x + 16, y);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameSelectedText).toBe("shortcuts");

  await page.mouse.click(input.x + 16, y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "rename", path: "/notes/shortcuts.txt" });
  expect(menuItem(state, "cut").enabled).toBe(true);
  expect(menuItem(state, "copy").enabled).toBe(true);
  expect(menuItem(state, "paste").enabled).toBe(true);
  expect(state.contextMenu?.items.some((item) => item.command === "duplicate")).toBe(false);
  await clickMenuItem(page, state, "cut");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameText).toBe(".txt");
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe("shortcuts");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(input.x + 4, y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(menuItem(state, "cut").enabled).toBe(false);
  expect(menuItem(state, "copy").enabled).toBe(false);
  await clickMenuItem(page, state, "paste");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameText).toBe("shortcuts.txt");
});

test("rename and search text boxes clip long text and keep the caret visible", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas text input clipping is covered in desktop Chromium.");
  const mod = desktopShortcutModifier();

  let state = await appState<CanvasTargets>(page);
  await page.mouse.dblclick(...pointArgs(center(targetFor(state.fileTargets, "/notes/shortcuts.txt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameInputRect).not.toBeNull();
  await page.keyboard.press(`${mod}+A`);
  const longName = `${"very-long-file-name-".repeat(10)}final.txt`;
  await page.keyboard.type(longName);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameText).toBe(longName);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameScrollX).toBeGreaterThan(20);
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(state.renameInputRect!.x + state.renameInputRect!.w - 8, center(state.renameInputRect!).y);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameScrollX).toBeGreaterThan(20);
  await page.keyboard.press("Escape");

  await page.mouse.click(24, 74);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchInputRect).not.toBeNull();
  const longQuery = "search ".repeat(40);
  await page.keyboard.type(longQuery);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchQuery).toBe(longQuery);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchScrollX).toBeGreaterThan(20);
  await page.keyboard.press("Home");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchScrollX).toBeLessThan(30);
});

test("rename field flags invalid filename characters", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas rename validation is covered in desktop Chromium.");
  const mod = desktopShortcutModifier();
  let state = await appState<CanvasTargets>(page);
  await page.mouse.dblclick(...pointArgs(center(targetFor(state.fileTargets, "/notes/shortcuts.txt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBe("/notes/shortcuts.txt");

  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("bad:name?.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameInvalid).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameInputRect).not.toBeNull();
  state = await appState<CanvasTargets>(page);
  expect(state.renameInvalidCharacters.map((item) => item.text)).toEqual([":", "?"]);
  const borderPixel = await canvasPixel(page, state.renameInputRect!.x + 0.5, center(state.renameInputRect!).y);
  expect(borderPixel[0]).toBeGreaterThan(180);
  expect(borderPixel[0]).toBeGreaterThan(borderPixel[2]! + 70);

  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBe("/notes/shortcuts.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).statusText).toBe("File name contains invalid characters");

  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("valid-name.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameInvalid).toBe(false);
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/notes/valid-name.txt");
});

test("rename mode owns the blinking caret", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas caret timing is covered in desktop Chromium.");
  await openReadme(page);
  let state = await appState<CanvasTargets>(page);
  expect(state.visibleCarets).toHaveLength(1);

  await page.mouse.dblclick(...pointArgs(center(targetFor(state.fileTargets, "/notes/shortcuts.txt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBe("/notes/shortcuts.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).visibleCarets).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.some((group) => group.caretVisible)).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameCaretVisible).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameCaretVisible).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renameCaretVisible).toBe(true);

  await page.keyboard.press("Escape");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).visibleCarets).toHaveLength(1);
});

test("tab clicks activate tabs without starting dock drag", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas tab pointer behavior is covered in desktop Chromium.");
  await openReadme(page);
  await page.evaluate(() => window.__slugApp!.openFile("/src/main.ts"));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/src/main.ts");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toEqual(expect.arrayContaining(["/README.md", "/src/main.ts"]));
  let state = await appState<CanvasTargets>(page);
  const readme = targetFor(state.tabTargets, "/README.md");
  const readmeCenter = center(readme);

  await page.mouse.move(readmeCenter.x, readmeCenter.y);
  await page.mouse.down();
  await page.mouse.move(readmeCenter.x + 2, readmeCenter.y + 1);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).dragGhost).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual(expect.arrayContaining(["/README.md", "/src/main.ts"]));
  await page.mouse.up();

  state = await appState<CanvasTargets>(page);
  expect(state.activePath).toBe("/README.md");
  expect(state.editorGroups).toHaveLength(1);
  expect(state.dragGhost).toBeNull();
});

test("middle clicking a tab closes it on desktop", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Desktop pointer button behavior is covered in Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/src/main.ts");
    await app.openFile("/notes/shortcuts.txt");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toContain("/src/main.ts");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toContain("/src/main.ts");

  const state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/src/main.ts"))), { button: "middle" });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).not.toContain("/src/main.ts");
});

test("tab close buttons show a hover state", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas pixel hover state is covered in desktop Chromium.");
  await openReadme(page);
  const state = await appState<CanvasTargets>(page);
  const close = targetFor(state.tabCloseTargets, "/README.md");
  const sample = { x: close.x + 1.5, y: close.y + 1.5 };
  const before = await canvasPixel(page, sample.x, sample.y);
  await page.mouse.move(close.x + close.w / 2, close.y + close.h / 2);
  await expect.poll(async () => {
    const after = await canvasPixel(page, sample.x, sample.y);
    return Math.abs(after[0]! - before[0]!) + Math.abs(after[1]! - before[1]!) + Math.abs(after[2]! - before[2]!);
  }).toBeGreaterThan(30);
});

test("tab context menu closes tabs and closes other tabs", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Desktop tab context menu behavior is covered in Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
    await app.openFile("/notes/shortcuts.txt");
    const doc = app.docs.getByPath("/src/main.ts")!;
    doc.setSelection({ line: 0, col: 0 });
    doc.replaceSelection("// saved from tab menu\n");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual(["/README.md", "/src/main.ts", "/notes/shortcuts.txt"]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toEqual(expect.arrayContaining(["/src/main.ts", "/notes/shortcuts.txt"]));
  await expect.poll(() => page.evaluate(() => window.__slugApp!.docs.getByPath("/src/main.ts")?.dirty)).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/notes/shortcuts.txt");

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/src/main.ts"))), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope.type).toBe("tab");
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["save", "close", "closeOthers", "findInFile"]);
  await clickMenuItem(page, state, "save");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.docs.getByPath("/src/main.ts")?.dirty)).toBe(false);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.readText("/src/main.ts"))).toContain("// saved from tab menu");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/notes/shortcuts.txt");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/src/main.ts"))), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "findInFile");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/src/main.ts");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findOpen).toBe(true);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/src/main.ts"))), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "close");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual(["/README.md", "/notes/shortcuts.txt"]);

  await page.evaluate(async () => {
    await window.__slugApp!.openFile("/src/main.ts");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toContain("/src/main.ts");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/README.md"))), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["save", "close", "closeOthers", "findInFile"]);
  await clickMenuItem(page, state, "closeOthers");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual(["/README.md"]);
});

test("tab overflow menu selects hidden tabs and scrolls them into view", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Desktop tab overflow behavior is covered in Chromium.");
  await page.setViewportSize({ width: 720, height: 520 });
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    for (let i = 1; i <= 9; i++) {
      const path = `/tabs/file-${String(i).padStart(2, "0")}.ts`;
      await app.vfs.writeFile(path, `export const file${i} = ${i};\n`, "text/plain");
      await app.openFile(path);
    }
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs.length).toBe(9);

  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabOverflowTargets.length).toBeGreaterThanOrEqual(1);
  let state = await appState<CanvasTargets>(page);

  await page.mouse.click(...pointArgs(center(state.tabOverflowTargets[0]!.rect)));
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope.type).toBe("tabOverflow");
  const hidden = state.contextMenu!.items[0]!;
  const hiddenPath = hidden.label;
  expect(state.tabTargets.map((item) => item.path)).not.toContain(hiddenPath);
  await page.mouse.click(...pointArgs(center(hidden.rect)));

  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe(hiddenPath);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toContain(hiddenPath);

  state = await appState<CanvasTargets>(page);
  const visibleBeforeWheel = state.tabTargets.map((item) => item.path);
  const wheelTarget = visibleBeforeWheel.includes("/tabs/file-01.ts") ? "/tabs/file-09.ts" : "/tabs/file-01.ts";
  const wheelDelta = wheelTarget === "/tabs/file-09.ts" ? 3000 : -3000;
  await page.mouse.move(...pointArgs(center(state.tabTargets[0]!.rect)));
  for (let i = 0; i < 4; i++) await page.mouse.wheel(0, wheelDelta);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toContain(wheelTarget);
});

test("dragging over tab bars shows an insertion line and can autoscroll overflowed tabs", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Desktop tab drag behavior is covered in Chromium.");
  await page.setViewportSize({ width: 720, height: 520 });
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    for (let i = 1; i <= 9; i++) {
      const path = `/tabs/reorder-${String(i).padStart(2, "0")}.ts`;
      await app.vfs.writeFile(path, `export const reorder${i} = ${i};\n`, "text/plain");
      await app.openFile(path);
    }
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabOverflowTargets.length).toBeGreaterThanOrEqual(1);
  for (let i = 0; i < 4; i++) {
    const current = await appState<CanvasTargets>(page);
    if (current.tabTargets.some((item) => item.path === "/tabs/reorder-09.ts")) break;
    await page.mouse.move(...pointArgs(center(current.tabTargets.at(-1)!.rect)));
    await page.mouse.wheel(0, 3000);
  }
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toContain("/tabs/reorder-09.ts");

  let state = await appState<CanvasTargets>(page);
  const lastTab = targetFor(state.tabTargets, "/tabs/reorder-09.ts");
  const group = state.editorGroups[0]!;
  const leftEdge = { x: group.frameRect.x + 6, y: group.frameRect.y + 16 };
  await page.mouse.move(...pointArgs(center(lastTab)));
  await page.mouse.down();
  for (let i = 0; i < 18; i++) {
    await page.mouse.move(leftEdge.x, leftEdge.y, { steps: 2 });
  }
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabInsertionPreview).not.toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabInsertionPreview?.index ?? -1).toBe(0);
  await expect.poll(async () => new Set((await appState<CanvasTargets>(page)).dockOverlayTargets.map((target) => target.zone))).toEqual(new Set(["center", "left", "right", "top", "bottom"]));
  await page.mouse.up();

  await expect.poll(async () => (await appState<{ openTabs: string[] }>(page)).openTabs[0]).toBe("/tabs/reorder-09.ts");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toContain("/tabs/reorder-09.ts");
});

test("double clicking empty tab bar creates untitled documents and close save writes a root file", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Desktop tab-bar double-click behavior is covered in Chromium.");
  let state = await appState<CanvasTargets>(page);
  await page.mouse.dblclick(...pointArgs(center(state.tabBarTargets[0]!.rect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("Untitled-1");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.path ?? null)).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabCloseTargets.map((item) => item.path)).toContain("Untitled-1");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabCloseTargets, "Untitled-1"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).not.toContain("Untitled-1");

  state = await appState<CanvasTargets>(page);
  await page.mouse.dblclick(...pointArgs(center(state.tabBarTargets[0]!.rect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("Untitled-2");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabCloseTargets.map((item) => item.path)).toContain("Untitled-2");
  await page.evaluate(() => {
    const app = window.__slugApp!;
    app.activeDoc()?.replaceSelection("memory close save\n");
    app.scheduleDraw();
  });

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabCloseTargets, "Untitled-2"))));
  state = await waitForModal(page);
  expect(state.modal?.kind).toBe("dirtyClose");
  expect(state.modal?.message).toContain("Untitled-2");
  const savePath = state.modal!.detail.match(/\/[0-9a-f]{8}\.txt/)?.[0];
  expect(savePath).toBeTruthy();
  await clickModalButton(page, state, "save");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).not.toContain("Untitled-2");
  await expect.poll(() => page.evaluate((path) => window.__slugApp!.vfs.readText(path), savePath!)).toBe("memory close save\n");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain(savePath);
});

test("empty tab bar context menu creates memory files, uploads files, and closes all tabs", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Desktop tab-bar context menu behavior is covered in Chromium.");
  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.tabBarTargets[0]!.rect)), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope.type).toBe("tabBar");
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["newFile", "uploadFile", "closeAll"]);
  expect(menuItem(state, "newFile").enabled).toBe(true);
  expect(menuItem(state, "uploadFile").enabled).toBe(true);
  expect(menuItem(state, "closeAll").enabled).toBe(false);

  await clickMenuItem(page, state, "newFile");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("Untitled-1");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.path ?? null)).toBeNull();

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.tabBarTargets[0]!.rect)), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "closeAll");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual([]);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.tabBarTargets[0]!.rect)), { button: "right" });
  state = await waitForContextMenu(page);
  const chooserPromise = page.waitForEvent("filechooser");
  await clickMenuItem(page, state, "uploadFile");
  const chooser = await chooserPromise;
  await chooser.setFiles({ name: "tabbar-upload.txt", mimeType: "text/plain", buffer: Buffer.from("uploaded from tab bar") });
  await expect.poll(() => readTextIfExists(page, "/tabbar-upload.txt")).toBe("uploaded from tab bar");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/tabbar-upload.txt");
});

test("workspace download can skip dirty untitled documents", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Browser download and zip inspection are covered in desktop Chromium.");
  let state = await appState<CanvasTargets>(page);
  await page.mouse.dblclick(...pointArgs(center(state.tabBarTargets[0]!.rect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("Untitled-1");
  await page.evaluate(() => {
    const app = window.__slugApp!;
    app.activeDoc()?.replaceSelection("memory only download sentinel\n");
    app.scheduleDraw();
  });

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.downloadActivityTarget!)));
  state = await waitForModal(page);
  expect(state.modal?.kind).toBe("dirtyDownload");
  expect(state.modal?.message).toContain("Untitled-1");
  expect(state.modal?.detail).toContain("omit this memory-only file");
  await clickModalButton(page, state, "cancel");

  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal?.kind).toBe("downloadReady");
  state = await waitForModal(page);
  const downloadPromise = page.waitForEvent("download");
  await clickModalButton(page, state, "download");
  const download = await downloadPromise;
  const path = await download.path();
  expect(path).toBeTruthy();
  const zip = await JSZip.loadAsync(await readFile(path!));
  const fileNames = Object.keys(zip.files).filter((name) => !zip.files[name]!.dir);
  const fileTexts = await Promise.all(fileNames.map((name) => zip.file(name)!.async("string")));
  expect(fileTexts.some((text) => text.includes("memory only download sentinel"))).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toContain("Untitled-1");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.path ?? null)).toBeNull();
});

test("footer exposes line position, whitespace toggle, and per-document highlight selector", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas footer controls are covered in desktop Chromium.");
  await openReadme(page);
  let state = await appState<CanvasTargets>(page);
  expect(state.activeSyntaxId).toBe("markdown");
  expect(state.statusWhitespaceTarget).not.toBeNull();
  expect(state.statusHighlightTarget).not.toBeNull();

  await page.mouse.click(...pointArgs(center(state.statusWhitespaceTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.showWhitespace).toBe(true);

  state = await appState<CanvasTargets>(page);
  const highlight = state.statusHighlightTarget!;
  await page.mouse.click(...pointArgs(center(highlight)));
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope.type).toBe("highlightDropdown");
  const menu = state.contextMenu!;
  expect(menu.rect.y + menu.rect.h).toBeLessThanOrEqual(highlight.y + 1);
  expect(menu.rect.w).toBeGreaterThanOrEqual(140);
  expect(menu.items.map((item) => item.command)).toContain("highlight:python");
  await clickMenuItem(page, state, "highlight:python");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeSyntaxId).toBe("python");
});

test("line number gutter context menu toggles line numbers", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Gutter context menu is covered in desktop Chromium.");
  await openReadme(page);
  let state = await appState<CanvasTargets>(page);
  const visibleGutter = state.editorGutterTargets.find((target) => target.path === "/README.md")!;
  expect(visibleGutter.rect.w).toBeGreaterThan(20);

  await page.mouse.click(...pointArgs(center(visibleGutter.rect)), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope.type).toBe("gutter");
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["toggleLineNumbers"]);
  expect(menuItem(state, "toggleLineNumbers").label).toBe("Hide Line Numbers");
  await clickMenuItem(page, state, "toggleLineNumbers");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.showLineNumbers).toBe(false);
  await expect.poll(async () => (await activeGroupState(page, "/README.md")).gutterWidth).toBe(0);

  state = await appState<CanvasTargets>(page);
  const hiddenGutter = state.editorGutterTargets.find((target) => target.path === "/README.md")!;
  expect(hiddenGutter.rect.w).toBeLessThan(visibleGutter.rect.w);
  await page.mouse.click(...pointArgs(center(hiddenGutter.rect)), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope.type).toBe("gutter");
  expect(menuItem(state, "toggleLineNumbers").label).toBe("Show Line Numbers");
  await clickMenuItem(page, state, "toggleLineNumbers");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.showLineNumbers).toBe(true);
  await expect.poll(async () => (await activeGroupState(page, "/README.md")).gutterWidth).toBeGreaterThan(20);
});

test("settings sidebar updates theme, editor font size, ui scale, and interface toggles", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Settings canvas controls are covered in desktop Chromium.");
  const mod = desktopShortcutModifier();

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarVisible).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).not.toContain("Settings");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsTargets.some((item) => item.type === "settingsHeader" && item.key === "ai.provider")).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsTargets.some((item) => item.type === "settingsButton" && item.key === "clearFileSystem")).toBe(false);

  state = await appState<CanvasTargets>(page);
  const showLineNumbersTarget = settingTarget(state.settingsTargets, "settingsCheckbox", "showLineNumbers");
  const showWhitespaceTarget = settingTarget(state.settingsTargets, "settingsCheckbox", "showWhitespace");
  const rememberOpenFilesTarget = settingTarget(state.settingsTargets, "settingsCheckbox", "rememberOpenFiles");
  expect(showWhitespaceTarget.y).toBeGreaterThan(showLineNumbersTarget.y);
  expect(showWhitespaceTarget.y).toBeLessThan(rememberOpenFilesTarget.y);

  const themeDropdown = settingTarget(state.settingsTargets, "settingsDropdown", "theme");
  await page.mouse.click(...pointArgs(center(themeDropdown)));
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "settingsDropdown", key: "theme" });
  expect(state.contextMenu?.rect.x).toBeCloseTo(themeDropdown.x, 0);
  expect(state.contextMenu?.rect.y).toBeCloseTo(themeDropdown.y + themeDropdown.h, 0);
  expect(state.contextMenu?.rect.w).toBeCloseTo(themeDropdown.w, 0);
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["themeDark", "themeLight"]);
  await clickMenuItem(page, state, "themeLight");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.theme).toBe("light");
  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    const point = { x: current.editorGroups[0]!.editorRect.x + 18, y: current.editorGroups[0]!.editorRect.y + 18 };
    return (await canvasPixel(page, point.x, point.y))[0];
  }).toBeGreaterThan(200);

  const codeLineHeight = await page.evaluate(() => window.__slugApp!.renderer.lineHeight("code"));
  state = await appState<CanvasTargets>(page);
  let fontSizeInput = settingTarget(state.settingsTargets, "settingsNumber", "fontSize");
  await page.mouse.dblclick(...pointArgs(center(fontSizeInput)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsNumberSelectedText).toBe("14");
  state = await appState<CanvasTargets>(page);
  fontSizeInput = settingTarget(state.settingsTargets, "settingsNumber", "fontSize");
  await page.mouse.click(...pointArgs(center(fontSizeInput)), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "settingsNumber", key: "fontSize" });
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["cut", "copy", "paste"]);
  expect(menuItem(state, "copy").enabled).toBe(true);
  await clickMenuItem(page, state, "copy");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsNumber", "fontSize"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeSettingsNumber).toBe("fontSize");
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("18");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.fontSize).toBe(18);
  await expect.poll(async () => page.evaluate(() => window.__slugApp!.renderer.lineHeight("code"))).toBeGreaterThan(codeLineHeight);

  const uiLineHeight = await page.evaluate(() => window.__slugApp!.renderer.lineHeight("ui"));
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsNumber", "uiScale"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("125");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.uiScale).toBe(125);
  await expect.poll(async () => page.evaluate(() => window.__slugApp!.renderer.lineHeight("ui"))).toBeGreaterThan(uiLineHeight);
  await page.evaluate(() => (window.__slugApp! as any).draw());

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsNumber", "uiScale"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("75");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.uiScale).toBe(75);
  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    const visualHeader = settingTarget(current.settingsTargets, "settingsHeader", "visual");
    return current.settingsRootTarget!.x + current.settingsRootTarget!.w - (visualHeader.x + visualHeader.w);
  }).toBeCloseTo(7.5, 1);

  await expect.poll(async () => page.evaluate(() => window.__slugApp!.renderer.resolveCodePoint("A".codePointAt(0)!, "code").font)).toBe("Inter-Regular.ttf");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsNumber", "tabSpaces"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("2");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.tabSpaces).toBe(2);
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "useTabStops"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.useTabStops).toBe(false);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "monospacedFont"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.monospacedFont).toBe(true);
  await expect.poll(async () => page.evaluate(() => window.__slugApp!.renderer.resolveCodePoint("A".codePointAt(0)!, "code").font)).toBe("MonaspaceNeon-Regular.ttf");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "showWhitespace"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.showWhitespace).toBe(true);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "showLineNumbers"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.showLineNumbers).toBe(false);
  await openReadme(page);
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/README.md"))));
  await expect.poll(async () => (await activeGroupState(page, "/README.md")).gutterWidth).toBe(0);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "renameOnDoubleClick"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.renameOnDoubleClick).toBe(false);
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "rememberOpenFiles"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.rememberOpenFiles).toBe(false);
  await page.mouse.click(24, 24);
  state = await appState<CanvasTargets>(page);
  await page.mouse.dblclick(...pointArgs(center(targetFor(state.fileTargets, "/notes/shortcuts.txt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath).toBeNull();

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.settingsRootTarget!)), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "settingsRoot" });
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["resetSettings"]);
  await clickMenuItem(page, state, "resetSettings");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings).toEqual({
    theme: "dark",
    fontSize: 14,
    uiScale: 100,
    monospacedFont: false,
    tabSpaces: 4,
    useTabStops: true,
    showWhitespace: false,
    showThinking: true,
    renameOnDoubleClick: true,
    showLineNumbers: true,
    rememberOpenFiles: true,
    aiProvider: "openai",
    aiModelManual: false,
    aiMaxToolCalls: 50,
    aiDetectDuplicateToolCalls: true,
    aiToolCallFormat: "tag",
    aiCompactFreePercent: 10,
    aiInsertEditorContext: true
  });
});

test("clear file system confirms and removes all workspace files", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Danger settings controls are covered in desktop Chromium.");
  await openReadme(page);
  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");

  state = await appState<CanvasTargets>(page);
  await page.mouse.move(state.settingsRootTarget!.x + state.settingsRootTarget!.w / 2, state.settingsRootTarget!.y + 90);
  await page.mouse.wheel(0, 1600);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsTargets.some((target) => target.type === "settingsHeader" && target.key === "danger")).toBe(true);
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsHeader", "danger"))));
  await page.mouse.move(state.settingsRootTarget!.x + state.settingsRootTarget!.w / 2, state.settingsRootTarget!.y + 90);
  await page.mouse.wheel(0, 600);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsTargets.some((target) => target.type === "settingsButton" && target.key === "clearFileSystem")).toBe(true);
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "clearFileSystem"))));
  state = await waitForModal(page);
  expect(state.modal?.kind).toBe("clearFileSystem");
  expect(state.modal?.title).toBe("Clear file system?");
  expect(state.modal?.buttons.map((button) => [button.action, button.label])).toEqual([["delete", "Clear"], ["cancel", "Cancel"]]);

  await clickModalButton(page, state, "cancel");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.stat("/README.md").then(Boolean))).toBe(true);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "clearFileSystem"))));
  state = await waitForModal(page);
  await clickModalButton(page, state, "delete");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.listAllFiles().then((files) => files.map((file) => file.path)))).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBeUndefined();
});

test("settings panel scrolls when scaled", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Settings canvas controls are covered in desktop Chromium.");
  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settings = { ...app.settings, uiScale: 240, monospacedFont: true };
    app.saveAndApplySettings();
    app.draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.uiScale).toBe(240);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsScrollbar).not.toBeNull();

  state = await appState<CanvasTargets>(page);
  const settingsRect = state.settingsScrollbar!.viewportRect;
  await page.mouse.move(settingsRect.x + settingsRect.w / 2, settingsRect.y + settingsRect.h / 2);
  await page.mouse.wheel(0, 800);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsScrollY).toBeGreaterThan(0);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settingsExpanded.add("danger");
    app.settingsScrollY = Number.MAX_SAFE_INTEGER;
    app.draw();
  });
  state = await appState<CanvasTargets>(page);
  expect(state.settingsTargets.some((target) => target.type === "settingsButton" && target.key === "clearFileSystem")).toBe(true);
});

test("focused settings text fields stay visible after viewport shrink", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Viewport resize focus behavior is covered in desktop Chromium.");
  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");

  state = await appState<CanvasTargets>(page);
  const maxContext = settingTarget(state.settingsTargets, "textField", "aiMaxContextTokens");
  await page.mouse.click(...pointArgs(center(maxContext)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("aiMaxContextTokens");

  await page.setViewportSize({ width: 1280, height: 430 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsScrollY).toBeGreaterThan(0);

  state = await appState<CanvasTargets>(page);
  const viewport = state.settingsScrollbar!.viewportRect;
  const visibleMaxContext = settingTarget(state.settingsTargets, "textField", "aiMaxContextTokens");
  expect(visibleMaxContext.y).toBeGreaterThanOrEqual(viewport.y);
  expect(visibleMaxContext.y + visibleMaxContext.h).toBeLessThanOrEqual(viewport.y + viewport.h + 1);
});

test("settings dropdown closes when the settings panel scrolls", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas wheel/dropdown behavior is covered in desktop Chromium.");
  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");
  state = await appState<CanvasTargets>(page);
  const themeDropdown = settingTarget(state.settingsTargets, "settingsDropdown", "theme");
  await page.mouse.click(...pointArgs(center(themeDropdown)));
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "settingsDropdown", key: "theme" });

  await page.mouse.move(themeDropdown.x + themeDropdown.w / 2, themeDropdown.y + themeDropdown.h + 80);
  await page.mouse.wheel(0, 700);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsScrollY).toBeGreaterThan(0);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).contextMenu).toBeNull();
});

test("mobile touch drag scrolls the settings panel content", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile settings touch scrolling is covered in mobile WebKit.");
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settings = { ...app.settings, uiScale: 220 };
    app.saveAndApplySettings();
    app.openSettingsTab();
    app.draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsScrollbar).not.toBeNull();
  const state = await appState<CanvasTargets>(page);
  const viewport = state.settingsScrollbar!.viewportRect;
  const from = { x: viewport.x + 10, y: viewport.y + viewport.h * 0.76 };
  const to = { x: from.x, y: viewport.y + viewport.h * 0.24 };
  await touchDrag(page, from, to);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsScrollY).toBeGreaterThan(60);
});

test("mobile settings activity toggles the settings sidebar", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "iOS activity bar behavior is covered in mobile WebKit.");
  let state = await appState<CanvasTargets>(page);
  expect(state.sidebarVisible).toBe(true);
  await page.touchscreen.tap(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarVisible).toBe(true);
  state = await appState<CanvasTargets>(page);
  await page.touchscreen.tap(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarVisible).toBe(false);
  state = await appState<CanvasTargets>(page);
  expect(state.editorGroups[0]!.editorRect.x).toBeCloseTo(48, 0);
});

test("mobile navigation taps do not focus the hidden text input", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "iOS focus behavior is covered in mobile WebKit.");
  await page.evaluate(async () => {
    const app = window.__slugApp! as any;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
    app.sidebarWidth = 0;
    app.draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("editor");

  let state = await appState<CanvasTargets>(page);
  const readmeTab = targetFor(state.tabTargets, "/README.md");
  await page.touchscreen.tap(...pointArgs(center(readmeTab)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeTab).toBe("/README.md");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBeNull();

  state = await appState<CanvasTargets>(page);
  await page.touchscreen.tap(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBeNull();
});

test("mobile chat input tap focuses without double-tap context menu", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "iOS chat input focus behavior is covered in mobile WebKit.");
  await page.touchscreen.tap(24, 124);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("chat");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBeNull();

  let state = await appState<CanvasTargets>(page);
  let point = center(state.chatInputRect!);
  await page.touchscreen.tap(point.x, point.y);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("chat");
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).touchKeyboardStabilizing).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).visualViewportResizeDeferred).toBe(true);

  await page.setViewportSize({ width: 390, height: 520 });
  await expect.poll(() => page.evaluate(() => {
    const area = document.querySelector(".input-bridge") as HTMLTextAreaElement | null;
    const state = window.__slugApp!.getStateForTests() as CanvasTargets;
    const top = area ? Number.parseFloat(getComputedStyle(area).top) : Number.NaN;
    return Boolean(area && document.activeElement === area && state.activeInputKind === "chat" && top >= 0 && top < state.canvas.cssHeight);
  })).toBe(true);

  state = await appState<CanvasTargets>(page);
  point = center(state.chatInputRect!);
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await appState<CanvasTargets>(page);
  expect(state.contextMenu).toBeNull();
  expect(state.activeInputKind).toBe("chat");
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);
  await page.waitForTimeout(TOUCH_KEYBOARD_SETTLE_TEST_MS);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).touchKeyboardStabilizing).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).visualViewportResizeDeferred).toBe(false);
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);
});

test("mobile double tap on settings header opens reset settings menu", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile settings double-tap behavior is covered in mobile WebKit.");
  let state = await appState<CanvasTargets>(page);
  await page.touchscreen.tap(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");
  state = await appState<CanvasTargets>(page);
  const point = center(state.settingsRootTarget!);
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "settingsRoot" });
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["resetSettings"]);
});

test("mobile double tap on chat header opens chat menu", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile chat double-tap behavior is covered in mobile WebKit.");
  await page.touchscreen.tap(24, 124);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("chat");
  const state = await appState<CanvasTargets>(page);
  const point = center(state.chatRootTarget!);
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  const menuState = await waitForContextMenu(page);
  expect(menuState.contextMenu?.scope).toEqual({ type: "chatRoot" });
  expect(menuState.contextMenu?.items.map((item) => item.command)).toEqual(["exportChat", "debugChat", "clearChat", "compactChat"]);
});

test("mobile double tap on chat bubble opens bubble menu with system copy", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile chat bubble context menu is covered in mobile WebKit.");
  await page.touchscreen.tap(24, 124);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("chat");
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.chat.clear();
    app.chat.messages.push(
      { id: "msg-mobile-user", role: "user", text: "mobile bubble", at: 1710000000000 },
      { id: "msg-mobile-assistant", role: "assistant", text: "mobile reply", at: 1710000001000 }
    );
    app.draw();
  });

  let state = await appState<CanvasTargets>(page);
  const bubble = state.chatBubbleTargets.find((target) => target.id === "msg-mobile-assistant");
  if (!bubble) throw new Error("Missing mobile chat bubble");
  const point = center(bubble.rect);
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "chatBubble", messageId: "msg-mobile-assistant" });
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["copyBubble", "copyChat", "clearChat", "systemCopyBubble", "systemCopyChat"]);

  await page.touchscreen.tap(...pointArgs(center(menuItem(state, "systemCopyBubble").rect)));
  const copyDialog = page.locator(".system-clipboard-dialog");
  await expect(copyDialog).toBeVisible();
  await expect(copyDialog.locator(".system-clipboard-field")).toHaveValue("mobile reply");
});

test("download activity prompts for dirty files and exports a workspace zip", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Browser download and zip inspection are covered in desktop Chromium.");
  await page.evaluate(() => window.__slugApp!.openFile("/README.md"));
  await page.evaluate(() => {
    const app = window.__slugApp!;
    const doc = app.activeDoc();
    doc?.selectAll();
    doc?.replaceSelection("download saved test\n");
    app.scheduleDraw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("download saved test\n");

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.downloadActivityTarget!)));
  state = await waitForModal(page);
  expect(state.modal?.kind).toBe("dirtyDownload");
  await clickModalButton(page, state, "save");

  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal?.kind).toBe("downloadReady");
  state = await waitForModal(page);
  expect(state.modal?.detail).toContain("file");
  const downloadPromise = page.waitForEvent("download");
  await clickModalButton(page, state, "download");
  const download = await downloadPromise;
  expect(download.suggestedFilename()).toMatch(/^workspace-.*\.zip$/);
  const path = await download.path();
  expect(path).toBeTruthy();

  const zip = await JSZip.loadAsync(await readFile(path!));
  expect(zip.file("README.md")).not.toBeNull();
  expect(zip.file("src/main.ts")).not.toBeNull();
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.readText("/README.md"))).toBe("download saved test\n");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).statusText).toContain("Downloaded workspace-");
  await expect.poll(async () => zip.file("README.md")!.async("string")).toBe("download saved test\n");
});

test("mobile double tap on file opens context menu instead of rename", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile touch double-tap behavior is covered in mobile WebKit.");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/notes/shortcuts.txt");
  const state = await appState<CanvasTargets>(page);
  const point = center(targetFor(state.fileTargets, "/notes/shortcuts.txt"));
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  const menuState = await waitForContextMenu(page);
  expect(menuState.contextMenu?.scope).toEqual({ type: "file", path: "/notes/shortcuts.txt" });
  expect(menuState.renamePath).toBeNull();
});

test("mobile double tap on files header opens root context menu", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile touch double-tap behavior is covered in mobile WebKit.");
  const state = await appState<CanvasTargets>(page);
  const body = state.filesRootTarget!;
  const point = { x: body.x + 24, y: body.y - 16 };
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  const menuState = await waitForContextMenu(page);
  expect(menuState.contextMenu?.scope).toEqual({ type: "root", path: "/" });
  expect(menuState.contextMenu?.items.map((item) => item.command)).toEqual(["createFile", "createFolder", "uploadFile"]);
});

test("mobile root create file enters rename mode with keyboard focus", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile iOS text focus behavior is covered in mobile WebKit.");
  const state = await appState<CanvasTargets>(page);
  const body = state.filesRootTarget!;
  const point = { x: body.x + 24, y: body.y - 16 };
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  let menuState = await waitForContextMenu(page);
  await page.touchscreen.tap(...pointArgs(center(menuItem(menuState, "createFile").rect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).renamePath ?? "").toMatch(/^\/[0-9a-f]+\.txt$/);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("command");
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);
  await page.waitForTimeout(250);
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);
  menuState = await appState<CanvasTargets>(page);
  expect(menuState.renameInputRect).not.toBeNull();
});

test("mobile double tap on editor text selects word and opens context menu", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile touch double-tap behavior is covered in mobile WebKit.");
  await page.evaluate(() => window.__slugApp!.openFile("/README.md"));
  await page.touchscreen.tap(24, 24);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarVisible).toBe(false);

  let state = await appState<CanvasTargets>(page);
  const editor = state.editorGroups[0]!.editorRect;
  const point = { x: editor.x + 86, y: editor.y + 10 };
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope.type).toBe("editor");
  expect(state.activeInputKind).toBe("editor");
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);
  expect(state.selectedText).toBe("Carrot");
  expect(menuItem(state, "copy").enabled).toBe(true);
  expect(menuItem(state, "systemCopy").enabled).toBe(true);
  expect(menuItem(state, "systemPaste").enabled).toBe(true);
});

test("mobile text context menus expose undo and redo when available", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile text undo context menus are covered in mobile WebKit.");
  await page.evaluate(async () => {
    const app = window.__slugApp! as any;
    app.sidebarWidth = 0;
    await app.vfs.writeFile("/mobile-undo.txt", "", "text/plain");
    await app.refreshFiles();
    await app.openFile("/mobile-undo.txt");
    app.activeDoc().replaceSelection("alpha beta");
    app.afterDocumentMutated(app.activeDoc());
    app.draw();
  });

  let state = await appState<CanvasTargets>(page);
  let editor = state.editorGroups[0]!.editorRect;
  let point = { x: editor.x + state.editorGroups[0]!.gutterWidth + 16, y: editor.y + 10 };
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await waitForContextMenu(page);
  expect(menuItem(state, "undo").enabled).toBe(true);
  await page.touchscreen.tap(...pointArgs(center(menuItem(state, "undo").rect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("");

  state = await appState<CanvasTargets>(page);
  editor = state.editorGroups[0]!.editorRect;
  point = { x: editor.x + state.editorGroups[0]!.gutterWidth + 16, y: editor.y + 10 };
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await waitForContextMenu(page);
  expect(menuItem(state, "redo").enabled).toBe(true);
  await page.touchscreen.tap(...pointArgs(center(menuItem(state, "redo").rect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("alpha beta");

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.sidebarMode = "search";
    app.sidebarWidth = app.lastSidebarWidth || 280;
    app.searchBuffer.replaceSelection("needle");
    app.draw();
  });
  state = await appState<CanvasTargets>(page);
  point = center(state.searchInputRect!);
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope.type).toBe("search");
  expect(menuItem(state, "undo").enabled).toBe(true);
  await page.touchscreen.tap(...pointArgs(center(menuItem(state, "undo").rect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchQuery).toBe("");
});

test("mobile double tap on line number gutter opens line number menu", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile gutter double-tap behavior is covered in mobile WebKit.");
  await page.evaluate(() => window.__slugApp!.openFile("/README.md"));
  await page.touchscreen.tap(24, 24);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarVisible).toBe(false);

  const state = await appState<CanvasTargets>(page);
  const gutter = state.editorGutterTargets.find((target) => target.path === "/README.md")!;
  const point = center(gutter.rect);
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  const menuState = await waitForContextMenu(page);
  expect(menuState.contextMenu?.scope.type).toBe("gutter");
  expect(menuState.contextMenu?.items.map((item) => item.command)).toEqual(["toggleLineNumbers"]);
});

test("mobile double tap on a tab opens the tab context menu", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile touch double-tap behavior is covered in mobile WebKit.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
    (app as any).draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toContain("/src/main.ts");
  const state = await appState<CanvasTargets>(page);
  const mainTab = targetFor(state.tabTargets, "/src/main.ts");
  const point = { x: mainTab.x + 24, y: mainTab.y + mainTab.h / 2 };
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  const menuState = await waitForContextMenu(page);
  expect(menuState.contextMenu?.scope.type).toBe("tab");
  expect(menuState.contextMenu?.items.map((item) => item.command)).toEqual(["save", "close", "closeOthers", "findInFile"]);
});

test("mobile double tap on empty tab bar opens the tab-bar context menu", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile touch double-tap behavior is covered in mobile WebKit.");
  await page.evaluate(async () => {
    const app = window.__slugApp! as any;
    await app.openFile("/README.md");
    await app.requestCloseTab(app.activeDoc().id);
    app.draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toEqual([]);
  const state = await appState<CanvasTargets>(page);
  const point = center(state.tabBarTargets[0]!.rect);
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  const menuState = await waitForContextMenu(page);
  expect(menuState.contextMenu?.scope.type).toBe("tabBar");
  expect(menuState.contextMenu?.items.map((item) => item.command)).toEqual(["newFile", "uploadFile", "closeAll"]);
});

test("mobile upload file uses a visible HTML picker dialog", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile upload fallback is covered in mobile WebKit.");
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.sidebarMode = "files";
    app.sidebarWidth = app.lastSidebarWidth || 280;
    app.draw();
  });
  const state = await appState<CanvasTargets>(page);
  const body = state.filesRootTarget!;
  const point = { x: body.x + 24, y: body.y - 16 };
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  const menuState = await waitForContextMenu(page);
  await page.touchscreen.tap(...pointArgs(center(menuItem(menuState, "uploadFile").rect)));

  const dialog = page.locator(".system-file-upload-dialog");
  await expect(dialog).toBeVisible();
  await expect(dialog.locator(".system-file-upload-field")).toBeVisible();
  await expect(dialog.getByRole("button", { name: "OK" })).toBeDisabled();
  await dialog.locator(".system-file-upload-field").setInputFiles({
    name: "mobile-upload.txt",
    mimeType: "text/plain",
    buffer: Buffer.from("uploaded from mobile")
  });
  await expect(dialog.locator(".system-file-upload-status")).toHaveText("mobile-upload.txt");
  await expect(dialog.getByRole("button", { name: "OK" })).toBeEnabled();
  await dialog.getByRole("button", { name: "OK" }).click();
  await expect(dialog).toHaveCount(0);
  await expect.poll(() => readTextIfExists(page, "/mobile-upload.txt")).toBe("uploaded from mobile");
});

test("mobile editor context menu uses local clipboard when system clipboard is unavailable", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile local clipboard behavior is covered in mobile WebKit.");
  await page.evaluate(() => window.__slugApp!.openFile("/README.md"));
  await page.touchscreen.tap(24, 24);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarVisible).toBe(false);

  await page.evaluate(() => {
    const app = window.__slugApp!;
    const doc = app.activeDoc();
    doc?.selectAll();
    doc?.replaceSelection("alpha beta");
    app.scheduleDraw();
  });

  let state = await appState<CanvasTargets>(page);
  const editor = state.editorGroups[0]!.editorRect;
  const lineH = await page.evaluate(() => window.__slugApp!.renderer.lineHeight("code"));
  const alpha = { x: editor.x + state.editorGroups[0]!.gutterWidth + 16, y: editor.y + lineH / 2 };
  const betaOffset = await page.evaluate(() => window.__slugApp!.renderer.measureText("alpha ", "code"));
  const beta = { x: alpha.x + betaOffset, y: alpha.y };

  await page.touchscreen.tap(alpha.x, alpha.y);
  await page.touchscreen.tap(alpha.x, alpha.y);
  state = await waitForContextMenu(page);
  await page.touchscreen.tap(...pointArgs(center(menuItem(state, "copy").rect)));

  await page.touchscreen.tap(beta.x, beta.y);
  await page.touchscreen.tap(beta.x, beta.y);
  state = await waitForContextMenu(page);
  await page.touchscreen.tap(...pointArgs(center(menuItem(state, "paste").rect)));

  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("alpha alpha");
});

test("mobile editor context menu exposes system clipboard dialogs", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile system clipboard fallback is covered in mobile WebKit.");
  await page.evaluate(async () => {
    const app = window.__slugApp! as any;
    await app.openFile("/README.md");
    app.settings.theme = "light";
    app.saveAndApplySettings();
    app.draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.theme).toBe("light");
  await page.touchscreen.tap(24, 24);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarVisible).toBe(false);

  let state = await appState<CanvasTargets>(page);
  const editor = state.editorGroups[0]!.editorRect;
  const point = { x: editor.x + 86, y: editor.y + 10 };
  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await waitForContextMenu(page);
  const canvasBeforeCopy = state.canvas;
  await page.touchscreen.tap(...pointArgs(center(menuItem(state, "systemCopy").rect)));

  const overlay = page.locator(".system-clipboard-overlay");
  const copyDialog = page.locator(".system-clipboard-dialog");
  await expect(copyDialog).toBeVisible();
  await expect(copyDialog.locator(".system-clipboard-field")).toHaveValue("Carrot");
  await expect(copyDialog.locator(".system-clipboard-field")).toHaveCSS("font-size", "16px");
  const dialogTheme = await copyDialog.evaluate((node) => {
    const dialog = getComputedStyle(node);
    const field = getComputedStyle(node.querySelector(".system-clipboard-field")!);
    return { dialogBackground: dialog.backgroundColor, dialogColor: dialog.color, fieldBackground: field.backgroundColor };
  });
  expect(dialogTheme.dialogBackground).toContain("219, 224, 232");
  expect(dialogTheme.dialogColor).toContain("20, 26, 33");
  expect(dialogTheme.fieldBackground).toContain("209, 214, 222");
  await expect(overlay).toHaveCSS("align-items", "flex-start");
  const overlayBox = await overlay.boundingBox();
  const viewport = page.viewportSize()!;
  expect(Math.round(overlayBox?.width ?? 0)).toBe(viewport.width);
  expect(Math.round(overlayBox?.height ?? 0)).toBe(viewport.height);
  const canvasDuringCopy = (await appState<CanvasTargets>(page)).canvas;
  expect(canvasDuringCopy.cssWidth).toBe(canvasBeforeCopy.cssWidth);
  expect(canvasDuringCopy.cssHeight).toBe(canvasBeforeCopy.cssHeight);
  await copyDialog.getByRole("button", { name: "Cancel" }).click();
  await expect(copyDialog).toHaveCount(0);

  await page.touchscreen.tap(point.x, point.y);
  await page.touchscreen.tap(point.x, point.y);
  state = await waitForContextMenu(page);
  await page.touchscreen.tap(...pointArgs(center(menuItem(state, "systemPaste").rect)));
  const pasteDialog = page.locator(".system-clipboard-dialog");
  await expect(pasteDialog).toBeVisible();
  await expect(pasteDialog.locator(".system-clipboard-field")).toHaveCSS("font-size", "16px");
  await pasteDialog.locator(".system-clipboard-field").fill("ios system paste");
  await pasteDialog.getByRole("button", { name: "OK" }).click();
  await expect(pasteDialog).toHaveCount(0);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toContain("ios system paste");
});

test("closing a dirty tab prompts to save, discard, or cancel", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas modal geometry is covered in desktop Chromium.");
  await openReadme(page);
  await page.evaluate(() => {
    const app = window.__slugApp!;
    const doc = app.activeDoc();
    doc?.replaceSelection("dirty close test\n");
    app.scheduleDraw();
  });

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabCloseTargets, "/README.md"))));
  state = await waitForModal(page);
  expect(state.modal?.kind).toBe("dirtyClose");
  expect(state.modal?.title).toBe("Save before closing?");
  expect(state.modal?.message).toContain("/README.md");
  expect(state.modal?.buttons.map((item) => item.action)).toEqual(["save", "discard", "cancel"]);

  await clickModalButton(page, state, "cancel");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toContain("/README.md");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabCloseTargets, "/README.md"))));
  state = await waitForModal(page);
  await clickModalButton(page, state, "discard");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).not.toContain("/README.md");
  await page.evaluate(async () => {
    await window.__slugApp!.openFile("/README.md");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabCloseTargets.map((item) => item.path)).toContain("/README.md");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.getText().startsWith("dirty close test"))).toBe(false);

  await page.evaluate(() => {
    const app = window.__slugApp!;
    const doc = app.activeDoc();
    doc?.replaceSelection("saved close test\n");
    app.scheduleDraw();
  });
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabCloseTargets, "/README.md"))));
  state = await waitForModal(page);
  await clickModalButton(page, state, "save");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).not.toContain("/README.md");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.readText("/README.md"))).toContain("saved close test");
});

test("only the active docked editor shows a caret while cursor positions persist", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas dock and caret geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
    await app.openFile("/notes/shortcuts.txt");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(3);

  let state = await appState<CanvasTargets>(page);
  let rootGroup = state.editorGroups[0]!;
  await drag(page, center(targetFor(state.tabTargets, "/src/main.ts")), { x: rootGroup.frameRect.x + rootGroup.frameRect.w - 24, y: rootGroup.frameRect.y + rootGroup.frameRect.h / 2 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(2);

  state = await appState<CanvasTargets>(page);
  rootGroup = state.editorGroups.find((group) => group.tabs.includes("/README.md") && group.tabs.includes("/notes/shortcuts.txt"))!;
  await drag(page, center(targetFor(state.tabTargets, "/notes/shortcuts.txt")), { x: rootGroup.frameRect.x + rootGroup.frameRect.w / 2, y: rootGroup.frameRect.y + rootGroup.frameRect.h - 24 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(3);

  await placeCursorInDocument(page, "/README.md", 1);
  await expectVisibleCaret(page, "/README.md", 1);
  await placeCursorInDocument(page, "/src/main.ts", 2);
  await expectVisibleCaret(page, "/src/main.ts", 2);
  await placeCursorInDocument(page, "/notes/shortcuts.txt", 3);
  await expectVisibleCaret(page, "/notes/shortcuts.txt", 3);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/README.md"))));
  await expectVisibleCaret(page, "/README.md", 1);
  state = await appState<CanvasTargets>(page);
  expect(state.visibleCarets).toHaveLength(1);
  expect(state.editorGroups.filter((group) => group.caretVisible)).toHaveLength(1);
});

test("active editor caret blinks", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Caret timing is covered in desktop Chromium.");
  await page.mouse.click(420, 96);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).caretBlinkOn).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).caretBlinkOn).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).caretBlinkOn).toBe(true);
});

test("document find widget navigates and replaces matches", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas find overlay controls are covered in desktop Chromium.");
  const mod = desktopShortcutModifier();
  await openReadme(page);
  await page.evaluate(() => {
    const app = window.__slugApp!;
    const doc = app.activeDoc();
    doc?.selectAll();
    doc?.replaceSelection("alpha beta alpha\nalpha");
    app.scheduleDraw();
  });
  const stateBefore = await appState<CanvasTargets>(page);
  const editor = stateBefore.editorGroups[0]!.editorRect;
  await page.mouse.click(editor.x + stateBefore.editorGroups[0]!.gutterWidth + 12, editor.y + 8);
  await page.keyboard.press(`${mod}+F`);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findOpen).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("find");
  await page.keyboard.type("alpha");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).selectedText).toBe("alpha");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findTargets.some((item) => item.type === "findNext")).toBe(true);

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.findTargets, "findNext", "findNext"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).selectedText).toBe("alpha");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.findTargets, "findToggle", "findToggle"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findReplaceExpanded).toBe(true);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.findTargets, "textField", "findReplace"))));
  await page.keyboard.type("omega");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findReplaceText).toBe("omega");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.findTargets, "findReplace", "findReplace"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toContain("omega");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.findTargets, "findReplaceAll", "findReplaceAll"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("omega beta omega\nomega");
});

test("document find widget state is per document", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas find overlay controls are covered in desktop Chromium.");
  const mod = desktopShortcutModifier();
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    app.activeDoc()?.selectAll();
    app.activeDoc()?.replaceSelection("alpha alpha\n");
    await app.openFile("/src/main.ts");
    app.activeDoc()?.selectAll();
    app.activeDoc()?.replaceSelection("beta beta\n");
    await app.openFile("/README.md");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/README.md");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toContain("/src/main.ts");

  await page.keyboard.press(`${mod}+F`);
  await page.keyboard.type("alpha");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findOpen).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findQuery).toBe("alpha");

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/src/main.ts"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/src/main.ts");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findOpen).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findQuery).toBe("");

  await page.keyboard.press(`${mod}+F`);
  await page.keyboard.type("beta");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findOpen).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findQuery).toBe("beta");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/README.md"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/README.md");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findOpen).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findQuery).toBe("alpha");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/src/main.ts"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/src/main.ts");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findOpen).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).findQuery).toBe("beta");
});

test("search sidebar scans virtual workspace files", async ({ page }) => {
  await page.mouse.click(24, 74);
  await expect.poll(async () => (await appState<{ sidebarMode: string }>(page)).sidebarMode).toBe("search");
  await expect.poll(async () => (await appState<{ activeInputKind: string | null }>(page)).activeInputKind).toBe("search");
  await page.keyboard.type("hello");
  await expect.poll(async () => (await appState<{ searchResults: unknown[] }>(page)).searchResults.length).toBeGreaterThan(0);
  let state = await appState<CanvasTargets>(page);
  expect(state.sidebarMode).toBe("search");
  expect(state.searchQuery).toBe("hello");
  expect(state.searchResults.some((item) => item.path.endsWith("main.ts"))).toBe(true);
  expect(state.searchTargets.some((item) => item.type === "searchRefresh")).toBe(true);

  await page.evaluate(async () => {
    const app = window.__slugApp!;
    const doc = await app.docs.open("/src/main.ts");
    doc.selectAll();
    doc.replaceSelection("export const value = 42;\n");
    app.scheduleDraw();
  });
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.searchTargets, "searchRefresh", "searchRefresh"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchResults.some((item) => item.path.endsWith("main.ts"))).toBe(false);
});

test("search sidebar scrolls overflowing results", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas sidebar wheel behavior is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    for (let i = 0; i < 60; i++) {
      await app.vfs.writeFile(`/needle-${String(i).padStart(3, "0")}.txt`, `needle result ${i}\n`, "text/plain");
    }
    await app.refreshFiles();
    app.scheduleDraw();
  });

  await page.mouse.click(24, 74);
  await page.keyboard.type("needle");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchResults.length).toBeGreaterThan(50);
  let state = await appState<CanvasTargets>(page);
  const scrollbar = state.sidebarScrollbars.find((item) => item.panel === "search");
  expect(scrollbar).toBeTruthy();
  expect(state.searchResultTargets.map((item) => item.path)).not.toContain("/needle-059.txt");

  await page.mouse.move(scrollbar!.rect.x - 20, scrollbar!.rect.y + scrollbar!.rect.h / 2);
  await page.mouse.wheel(0, 3000);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchScrollY).toBeGreaterThan(0);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchResultTargets.map((item) => item.path)).toContain("/needle-059.txt");
});

test("search sidebar expands project replace and replaces all workspace matches", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas replace controls are covered in desktop Chromium.");
  await page.mouse.click(24, 74);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchInputRect).not.toBeNull();
  await page.keyboard.type("hello");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchResults.length).toBeGreaterThan(0);

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.searchTargets, "searchReplaceToggle", "searchReplaceToggle"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchReplaceExpanded).toBe(true);
  state = await appState<CanvasTargets>(page);
  expect(settingTarget(state.searchTargets, "textField", "projectReplace").x).toBeCloseTo(settingTarget(state.searchTargets, "searchReplaceToggle", "searchReplaceToggle").x, 0);
  await page.mouse.click(...pointArgs(center(settingTarget(state.searchTargets, "textField", "projectReplace"))));
  await page.keyboard.type("hola");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).projectReplaceText).toBe("hola");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.searchTargets, "searchReplaceAll", "searchReplaceAll"))));

  await expect.poll(async () => (await appState<CanvasTargets>(page)).openTabs).toContain("/src/main.ts");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.docs.getByPath("/src/main.ts")?.getText() ?? "")).toContain("hola");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.docs.getByPath("/src/main.ts")?.dirty ?? false)).toBe(true);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.readText("/src/main.ts"))).toContain("hello");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchResults.length).toBe(0);
});

test("search box supports caret selection and text context menu", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas text input geometry and clipboard behavior are covered in desktop Chromium.");
  await page.mouse.click(24, 74);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchInputRect).not.toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).visibleCarets).toEqual([]);

  let state = await appState<CanvasTargets>(page);
  const input = state.searchInputRect!;
  await page.keyboard.type("hello world");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchQuery).toBe("hello world");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchCaretVisible).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchCaretVisible).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchCaretVisible).toBe(true);

  await page.mouse.dblclick(input.x + 16, center(input).y);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchSelectedText).toBe("hello");
  await page.mouse.click(input.x + 16, center(input).y, { clickCount: 3 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchSelectedText).toBe("hello world");
  await page.mouse.dblclick(input.x + 16, center(input).y);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchSelectedText).toBe("hello");
  await page.mouse.click(input.x + 16, center(input).y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "search" });
  expect(menuItem(state, "cut").enabled).toBe(true);
  expect(menuItem(state, "copy").enabled).toBe(true);
  expect(menuItem(state, "paste").enabled).toBe(true);

  await clickMenuItem(page, state, "cut");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchQuery).toBe(" world");
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe("hello");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(state.searchInputRect!.x + 4, center(state.searchInputRect!).y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(menuItem(state, "undo").enabled).toBe(true);
  await clickMenuItem(page, state, "undo");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchQuery).toBe("hello world");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(state.searchInputRect!.x + 4, center(state.searchInputRect!).y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(menuItem(state, "redo").enabled).toBe(true);
  await clickMenuItem(page, state, "redo");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchQuery).toBe(" world");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(state.searchInputRect!.x + 4, center(state.searchInputRect!).y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(menuItem(state, "cut").enabled).toBe(false);
  expect(menuItem(state, "copy").enabled).toBe(false);
  await clickMenuItem(page, state, "paste");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchQuery).toBe("hello world");
});

test("typing in a text box closes the open context menu", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas text context-menu behavior is covered in desktop Chromium.");
  await page.mouse.click(24, 74);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchInputRect).not.toBeNull();
  await page.keyboard.type("abc");

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(state.searchInputRect!.x + state.searchInputRect!.w - 4, center(state.searchInputRect!).y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "search" });

  await page.keyboard.type("z");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).contextMenu).toBeNull();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchQuery).toContain("z");
});

test("search box only uses accent outline while focused", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas pixel color is covered in desktop Chromium.");
  await openReadme(page);
  await page.mouse.click(24, 74);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchInputRect).not.toBeNull();

  let state = await appState<CanvasTargets>(page);
  const input = state.searchInputRect!;
  const focusedColor = await canvasPixel(page, input.x + 6, input.y);
  expect(focusedColor.slice(0, 3)).toEqual([79, 145, 232]);

  const group = state.editorGroups[0]!;
  await page.mouse.click(group.editorRect.x + 80, group.editorRect.y + 20);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("editor");
  state = await appState<CanvasTargets>(page);
  const blurredColor = await canvasPixel(page, state.searchInputRect!.x + 6, state.searchInputRect!.y);
  expect(blurredColor.slice(0, 3)).toEqual([61, 64, 71]);
});

test("chat sidebar accepts a turn and reports missing model as a system message", async ({ page }) => {
  await page.mouse.click(24, 124);
  await expect.poll(async () => (await appState<{ sidebarMode: string }>(page)).sidebarMode).toBe("chat");
  await expect.poll(async () => (await appState<{ activeInputKind: string | null }>(page)).activeInputKind).toBe("chat");
  await page.keyboard.type("explain selection");
  await expect.poll(async () => (await appState<{ chatDraft: string }>(page)).chatDraft).toBe("explain selection");
  await page.keyboard.press("Enter");
  await expect.poll(async () => {
    const state = await appState<{ chatMessages: Array<{ role: string; text: string; ok?: boolean }> }>(page);
    return state.chatMessages.some((msg) => msg.role === "system" && msg.ok === false && msg.text.includes("No model is configured"));
  }).toBe(true);
  await expect.poll(async () => {
    const state = await appState<{ chatMessages: Array<{ role: string; text: string }> }>(page);
    return state.chatMessages.some((msg) => msg.role === "assistant" && msg.text.includes("No model is configured"));
  }).toBe(false);
});

test("chat request failures are shown as system messages", async ({ page }) => {
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    await route.fulfill({
      status: 503,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({ error: { message: "service unavailable" } })
    });
  });

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
  });

  await page.mouse.click(24, 124);
  await page.keyboard.type("try to connect");
  await page.keyboard.press("Enter");
  await expect.poll(async () => {
    const state = await appState<{ chatMessages: Array<{ role: string; text: string }> }>(page);
    return state.chatMessages.some((msg) => msg.role === "system" && msg.text.includes("Request failed: HTTP 503"));
  }).toBe(true);
  await expect.poll(async () => {
    const state = await appState<{ chatMessages: Array<{ role: string; text: string }> }>(page);
    return state.chatMessages.some((msg) => msg.role === "assistant" && msg.text.includes("HTTP 503"));
  }).toBe(false);
});

test("chat requires known max context tokens before starting", async ({ page }) => {
  const completionRequests: Array<Record<string, any>> = [];
  await page.route("http://localhost:1234/v1/models", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({ data: [{ id: "local-no-context" }] })
    });
  });
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    completionRequests.push(route.request().postDataJSON() as Record<string, any>);
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({ choices: [{ message: { content: "should not run" } }] })
    });
  });

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-no-context",
      temperature: 0.2,
      maxContextTokens: 0
    }));
  });

  await page.mouse.click(24, 124);
  await page.keyboard.type("try without context");
  await page.keyboard.press("Enter");
  await expect.poll(async () => {
    const state = await appState<{ chatMessages: Array<{ role: string; text: string }> }>(page);
    return state.chatMessages.some((msg) => msg.role === "system" && msg.text.includes("Max context tokens are unknown"));
  }).toBe(true);
  expect(completionRequests).toEqual([]);
});

test("chat token counter calibrates on first model-backed turn", async ({ page }) => {
  const completionRequests: Array<Record<string, any>> = [];
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    if (route.request().method() === "OPTIONS") {
      await route.fulfill({
        status: 204,
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "POST, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type, Authorization"
        }
      });
      return;
    }
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content: isProbe ? "x" : "tracked assistant response" } }],
        usage: isProbe
          ? { prompt_tokens: 31, completion_tokens: 1, total_tokens: 32 }
          : { prompt_tokens: 37, completion_tokens: 3, total_tokens: 40 }
      })
    });
  });

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
  });

  await page.mouse.click(24, 124);
  await page.keyboard.type("count these tokens");
  await page.keyboard.press("Enter");

  await expect.poll(() => completionRequests.length).toBe(2);
  expect(completionRequests[0]!.max_tokens).toBe(1);
  expect(completionRequests[0]!.messages.at(-1)).toEqual({ role: "user", content: "test" });
  expect(completionRequests[1]!.max_tokens).toBeUndefined();
  expect(completionRequests[1]!.messages.at(-1)).toEqual({ role: "user", content: "count these tokens" });

  await expect.poll(async () => {
    const state = await appState<CanvasTargets>(page);
    return state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "tracked assistant response");
  }).toBe(true);
  const state = await appState<CanvasTargets>(page);
  expect(state.chatTokenUsage.calibrated).toBe(true);
  expect(state.chatTokenUsage.basePromptTokens).toBe(30);
  expect(state.chatTokenUsage.promptTokens).toBe(40);
  expect(state.chatTokenUsage.lastPromptTokens).toBe(37);
  expect(state.chatTokenUsage.lastCompletionTokens).toBe(3);
  expect(state.chatTokenUsage.lastTotalTokens).toBe(40);
  expect(state.chatTokenUsage.source).toBe("usage");
  expect(state.chatTokenUsage.dirty).toBe(false);
});

test("chat streams assistant response chunks as they arrive", async ({ page }) => {
  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    const global = window as any;
    const encoder = new TextEncoder();
    app.chat.clear();
    global.__streamDone = false;
    global.__streamRequestBodies = [];
    global.fetch = async (_input: RequestInfo | URL, init?: RequestInit) => {
      const body = JSON.parse(String(init?.body ?? "{}"));
      global.__streamRequestBodies.push(body);
      if (body.max_tokens === 1) {
        return new Response(JSON.stringify({
          choices: [{ message: { content: "x" } }],
          usage: { prompt_tokens: 31, completion_tokens: 1, total_tokens: 32 }
        }), { status: 200, headers: { "Content-Type": "application/json" } });
      }
      return new Response(new ReadableStream({
        start(controller) {
          global.__streamPush = (payload: string) => controller.enqueue(encoder.encode(payload));
          global.__streamClose = () => controller.close();
          controller.enqueue(encoder.encode('data: {"choices":[{"delta":{"reasoning":"stream thought"}}]}\n\n'));
          controller.enqueue(encoder.encode('data: {"choices":[{"delta":{"content":"Hello"}}]}\n\n'));
        }
      }), { status: 200, headers: { "Content-Type": "text/event-stream" } });
    };
    void app.chat.send("stream please", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    }).then(() => {
      global.__streamDone = true;
    });
  });

  await expect.poll(async () => {
    const state = await appState<CanvasTargets>(page);
    return state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "Hello");
  }).toBe(true);
  await expect.poll(async () => {
    const state = await appState<CanvasTargets>(page);
    return state.chatMessages.some((msg) => msg.role === "thinking" && msg.text === "stream thought");
  }).toBe(true);
  await expect.poll(() => page.evaluate(() => (window as any).__streamDone)).toBe(false);

  await page.evaluate(() => {
    const global = window as any;
    global.__streamPush('data: {"choices":[{"delta":{"content":" world"}}],"usage":{"prompt_tokens":37,"completion_tokens":2,"total_tokens":39}}\n\n');
    global.__streamPush("data: [DONE]\n\n");
    global.__streamClose();
  });
  await expect.poll(async () => {
    const state = await appState<CanvasTargets>(page);
    return state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "Hello world");
  }).toBe(true);
  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.filter((msg) => msg.role === "thinking" && msg.text === "stream thought")).toHaveLength(1);
  await expect.poll(() => page.evaluate(() => (window as any).__streamDone)).toBe(true);
  const requestBodies = await page.evaluate(() => (window as any).__streamRequestBodies);
  expect(requestBodies[1].stream).toBe(true);
});

test("long thinking bubbles keep their background while scrolled", async ({ page }) => {
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.sidebarMode = "chat";
    app.sidebarWidth = 340;
    app.chat.clear();
    const text = Array.from({ length: 260 }, (_item, index) => `thinking line ${String(index).padStart(3, "0")}`).join("\n");
    app.chat.messages.push({ id: "thinking-long", role: "thinking", text, at: Date.now() });
    app.chatScrollY = 1400;
    app.draw();
  });
  await expect.poll(async () => {
    const state = await appState<CanvasTargets>(page);
    return state.sidebarMode === "chat" && Boolean(state.chatInputRect);
  }).toBe(true);

  const state = await appState<CanvasTargets>(page);
  const input = state.chatInputRect!;
  const header = state.chatRootTarget!;
  const pixel = await canvasPixel(page, input.x + input.w - 34, header.y + header.h + 36);
  expect(pixel[0]!).toBeGreaterThan(pixel[2]! + 8);
  expect(pixel[1]!).toBeGreaterThan(pixel[2]! + 4);
});

test("long chat bubbles keep a visible header while scrolled through the middle", async ({ page }) => {
  const state = await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.sidebarMode = "chat";
    app.sidebarWidth = 340;
    app.chat.clear();
    app.chat.messages.push({
      id: "long-assistant",
      role: "assistant",
      text: Array.from({ length: 260 }, (_item, index) => `long assistant message line ${index} with enough words to wrap naturally`).join("\n"),
      at: Date.now()
    });
    app.chatScrollY = 950;
    app.draw();
    const transcript = app.hits.find((hit: { type: string }) => hit.type === "chatTranscript")?.rect;
    const labels = app.renderer.commands
      .filter((command: { type: string; text?: string }) => command.type === "text" && command.text === "ASSISTANT")
      .map((command: { y: number }) => command.y);
    return { transcript, labels };
  });
  expect(state.transcript).toBeTruthy();
  expect(state.labels.some((y: number) => y >= state.transcript.y && y <= state.transcript.y + 24)).toBe(true);
});

test("chat extracts provider, tag, and harmony thinking formats", async ({ page }) => {
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const message = isProbe
      ? { content: "x" }
      : nonProbeCompletions === 1
        ? { content: "provider answer", reasoning_content: "provider thought" }
        : nonProbeCompletions === 2
          ? { content: "lm studio answer", reasoning: "lm studio thought" }
          : nonProbeCompletions === 3
            ? { content: "<think>tag thought</think>tag answer" }
            : { content: "<|start|>assistant<|channel|>analysis<|message|>harmony thought<|end|><|start|>assistant<|channel|>final<|message|>harmony answer<|end|>" };
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    const runtime = () => ({
      maxToolCallsPerTurn: 50,
      detectDuplicateToolCalls: true,
      toolCallFormat: "none",
      compactFreePercent: 10
    });
    await app.chat.send("provider", undefined, [], { runtime: runtime(), onUpdate: () => app.scheduleDraw() });
    await app.chat.send("lm studio", undefined, [], { runtime: runtime(), onUpdate: () => app.scheduleDraw() });
    await app.chat.send("tag", undefined, [], { runtime: runtime(), onUpdate: () => app.scheduleDraw() });
    await app.chat.send("harmony", undefined, [], { runtime: runtime(), onUpdate: () => app.scheduleDraw() });
  });

  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "thinking" && msg.text === "provider thought")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "provider answer")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "thinking" && msg.text === "lm studio thought")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "lm studio answer")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "thinking" && msg.text === "tag thought")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "tag answer")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "thinking" && msg.text === "harmony thought")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "harmony answer")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.text.includes("<think>") || msg.text.includes("<|channel|>analysis"))).toBe(false);
});

test("chat tool-call limit modal controls only the current turn", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas modal tool-call flow is covered in desktop Chromium.");
  const completionRequests: Array<Record<string, any>> = [];
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? "<tool>readFile(\"/README.md\")</tool>"
        : nonProbeCompletions === 2
          ? "first turn done"
          : nonProbeCompletions === 3
            ? "<tool>readFile(\"/README.md\")</tool>"
            : "second turn done";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.settings = { ...app.settings, aiMaxToolCalls: 1 };
    app.saveAndApplySettings();
  });

  await page.mouse.click(24, 124);
  await page.keyboard.type("first turn");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal?.kind).toBe("toolCallLimit");
  let state = await waitForModal(page);
  expect(state.modal?.buttons.map((button) => [button.action, button.label])).toEqual([
    ["allowMore", "Allow 1 more"],
    ["allowAll", "Allow all"],
    ["stopToolCalls", "Stop tool calls"]
  ]);
  await clickModalButton(page, state, "allowAll");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "assistant" && msg.text === "first turn done")).toBe(true);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.chatInputRect!)));
  await page.keyboard.type("second turn");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal?.kind).toBe("toolCallLimit");
  state = await waitForModal(page);
  await clickModalButton(page, state, "stopToolCalls");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "system" && msg.text === "Max tool calls reached; stopped tool calls for this turn.")).toBe(true);
  expect(completionRequests.filter((request) => request.max_tokens !== 1)).toHaveLength(3);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.text === "second turn done")).toBe(false);
});

test("duplicate tool-call modal can allow or break the duplicate call", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas modal duplicate-tool flow is covered in desktop Chromium.");
  const completionRequests: Array<Record<string, any>> = [];
  let nonProbeCompletions = 0;
  const duplicateReads = '<tool>readFile("/README.md")</tool>\n<tool>readFile("/README.md")</tool>';
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? duplicateReads
        : nonProbeCompletions === 2
          ? "allowed duplicate done"
          : nonProbeCompletions === 3
            ? duplicateReads
            : "should not be reached";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    app.settings = { ...app.settings, aiDetectDuplicateToolCalls: true };
    app.saveAndApplySettings();
  });

  await page.mouse.click(24, 124);
  await page.keyboard.type("allow duplicate");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal?.kind).toBe("duplicateToolCall");
  let state = await waitForModal(page);
  expect(state.modal?.title).toBe("Duplicate tool call detected");
  expect(state.modal?.buttons.map((button) => [button.action, button.label])).toEqual([
    ["allowDuplicateTool", "Allow"],
    ["breakDuplicateTool", "Break"]
  ]);
  await clickModalButton(page, state, "allowDuplicateTool");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "assistant" && msg.text === "allowed duplicate done")).toBe(true);
  state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.filter((msg) => msg.role === "tool_result" && msg.name === "readFile" && msg.ok).length).toBe(2);

  await page.mouse.click(...pointArgs(center(state.chatInputRect!)));
  await page.keyboard.type("break duplicate");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal?.kind).toBe("duplicateToolCall");
  state = await waitForModal(page);
  await clickModalButton(page, state, "breakDuplicateTool");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "tool_result" && msg.name === "readFile" && msg.ok === false && msg.text.includes("Duplicate tool call detected"))).toBe(true);
  expect(completionRequests.filter((request) => request.max_tokens !== 1)).toHaveLength(3);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.text === "should not be reached")).toBe(false);
});

test("chat tool-call format controls manual parsing", async ({ page }) => {
  const completionRequests: Array<Record<string, any>> = [];
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<|channel|>commentary to=functions.readFile <|constrain|>json<|message|>{"path":"/README.md"}'
        : nonProbeCompletions === 2
          ? "harmony done"
          : "<tool>readFile(\"/README.md\")</tool>";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.chat.send("use harmony", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "harmony",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
    await app.chat.send("none should not parse", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "none",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  await expect.poll(() => completionRequests.filter((request) => request.max_tokens !== 1).length).toBe(3);
  expect(completionRequests[2]!.messages).toContainEqual(expect.objectContaining({
    role: "user",
    content: expect.stringContaining("<|channel|>commentary <|message|># Carrot Editor")
  }));
  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "tool_result" && msg.name === "readFile" && msg.text.includes("# Carrot Editor"))).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "harmony done")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === '<tool>readFile("/README.md")</tool>')).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text.includes("<|channel|>commentary"))).toBe(false);
  expect(state.chatMessages.some((msg) => msg.role === "user" && msg.text.includes("<|channel|>commentary <|message|># Carrot Editor"))).toBe(false);
  expect(state.chatMessages.filter((msg) => msg.role === "tool_result" && msg.name === "readFile")).toHaveLength(1);
});

test("AI grep tools search workspace and large individual files", async ({ page }) => {
  const completionRequests: Array<Record<string, any>> = [];
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<tool>grep("UNIQUE_GREP_NEEDLE")</tool>'
        : nonProbeCompletions === 2
          ? '<tool>grepFile("LARGE_GREP_NEEDLE", "/vendor/large-header.h")</tool>'
          : "grep checks complete";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 44 + nonProbeCompletions, completion_tokens: 4, total_tokens: 48 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.vfs.writeFile("/src/grep-target.ts", "export const UNIQUE_GREP_NEEDLE = true;\n", "text/plain");
    const largeHeader = `${"x".repeat(1024 * 1024 + 32)}\n#define LARGE_GREP_NEEDLE 1\n`;
    await app.vfs.writeFile("/vendor/large-header.h", largeHeader, "text/plain");
    await app.refreshFiles();
    await app.chat.send("verify grep tools", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  await expect.poll(() => completionRequests.filter((request) => request.max_tokens !== 1).length).toBe(3);
  const removedToolName = "frep" + "File";
  expect(completionRequests[1]!.messages[0].content).not.toContain(removedToolName);
  const state = await appState<CanvasTargets>(page);
  const grepResult = state.chatMessages.find((msg) => msg.role === "tool_result" && msg.name === "grep");
  expect(grepResult?.ok).toBe(true);
  expect(grepResult?.text).toContain("/src/grep-target.ts:1: export const UNIQUE_GREP_NEEDLE = true;");
  const grepFileResult = state.chatMessages.find((msg) => msg.role === "tool_result" && msg.name === "grepFile");
  expect(grepFileResult?.ok).toBe(true);
  expect(grepFileResult?.text).toContain("/vendor/large-header.h:2: #define LARGE_GREP_NEEDLE 1");
  expect(grepFileResult?.text).not.toContain("skipped");
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "grep checks complete")).toBe(true);
});

test("chat repairs tool calls that the model placed in hidden thinking", async ({ page }) => {
  const completionRequests: Array<Record<string, any>> = [];
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const message = isProbe
      ? { content: "x" }
      : nonProbeCompletions === 1
        ? { content: "", reasoning: "I should call commentary to=readFile <|message|>{\"path\":\"/README.md\"}" }
        : nonProbeCompletions === 2
          ? { content: '<|channel|>commentary to=readFile <|message|>{"path":"/README.md"}<|call|>' }
          : { content: "read complete" };
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 50 + nonProbeCompletions, completion_tokens: 4, total_tokens: 54 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.chat.send("call a tool to read the README.md file", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "harmony",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "thinking" && msg.text.includes("commentary to=readFile"))).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "system" && msg.ok === false && msg.text.includes("Hidden thinking is not executable"))).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "tool_result" && msg.name === "readFile" && msg.text.includes("# Carrot Editor"))).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "read complete")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "(empty response)")).toBe(false);

  const nonProbeRequests = completionRequests.filter((request) => request.max_tokens !== 1);
  expect(nonProbeRequests).toHaveLength(3);
  const repairRequestText = nonProbeRequests[1]!.messages.map((msg: { content: string }) => msg.content).join("\n");
  expect(repairRequestText).toContain("Hidden reasoning is not executable");
  expect(repairRequestText).not.toContain("[thinking]");
  expect(repairRequestText).not.toContain("I should call commentary to=readFile");
});

test("chat uses native OpenAI tool calls for GPT-OSS models", async ({ page }) => {
  const completionRequests: Array<Record<string, any>> = [];
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const message = isProbe
      ? { content: "x" }
      : nonProbeCompletions === 1
        ? {
            content: "",
            reasoning: "I need to inspect the README.",
            tool_calls: [{
              id: "call_readme",
              type: "function",
              function: { name: "readFile", arguments: "{\"path\":\"/README.md\"}" }
            }]
          }
        : { content: "native done" };
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message }],
        usage: isProbe
          ? { prompt_tokens: 34, completion_tokens: 1, total_tokens: 35 }
          : { prompt_tokens: 48 + nonProbeCompletions, completion_tokens: 4, total_tokens: 52 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "openai/gpt-oss-20b",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    localStorage.setItem("slug.aiSystemPrompt", "Native debug prompt\n\nAvailable tools:\n- readFile(path)\n\nTag tool-call format:\n<tool>readFile(\"/README.md\")</tool>");
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.chat.send("make a native read", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "harmony",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  await expect.poll(() => completionRequests.length).toBe(3);
  const removedToolName = "frep" + "File";
  expect(completionRequests[0]!.tools.map((tool: { function: { name: string } }) => tool.function.name)).toContain("readFile");
  expect(completionRequests[1]!.tools.map((tool: { function: { name: string } }) => tool.function.name)).toContain("readFile");
  expect(completionRequests[1]!.tools.map((tool: { function: { name: string } }) => tool.function.name)).not.toContain(removedToolName);
  expect(completionRequests[1]!.tool_choice).toBe("auto");
  expect(completionRequests[1]!.messages[0].content).toContain("Primary tool protocol");
  expect(completionRequests[1]!.messages[0].content).not.toContain(removedToolName);
  expect(completionRequests[1]!.messages[0].content).not.toContain("Tag tool-call format");
  expect(completionRequests[1]!.messages[0].content).not.toContain("Harmony-style tool-call format");
  expect(completionRequests[2]!.messages).toContainEqual(expect.objectContaining({
    role: "assistant",
    tool_calls: [expect.objectContaining({
      id: "call_readme",
      type: "function",
      function: { name: "readFile", arguments: "{\"path\":\"/README.md\"}" }
    })]
  }));
  expect(completionRequests[2]!.messages).toContainEqual(expect.objectContaining({
    role: "tool",
    tool_call_id: "call_readme",
    content: expect.stringContaining("# Carrot Editor")
  }));
  expect(completionRequests[2]!.messages.map((msg: { content?: string }) => msg.content ?? "").join("\n")).not.toContain("Hidden reasoning is not executable");

  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "thinking" && msg.text === "I need to inspect the README.")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "tool_result" && msg.name === "readFile" && msg.text.includes("# Carrot Editor"))).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "native done")).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "user" && msg.text.includes("<|channel|>commentary <|message|># Carrot Editor"))).toBe(false);
});

test("AI file tools refresh the file tree and open document views", async ({ page }) => {
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<tool>writeFile("/ai-created.txt", "created from ai\\n")</tool>'
        : nonProbeCompletions === 2
          ? '<tool>readFile("/README.md")</tool>'
          : nonProbeCompletions === 3
            ? '<tool>writeFile("/README.md", "updated open file\\n")</tool>'
            : "workspace updated";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.openFile("/README.md");
  });

  await page.mouse.click(24, 124);
  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.chatInputRect!)));
  await page.keyboard.type("update the workspace");
  await page.keyboard.press("Enter");

  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toBe("updated open file\n");
  await page.mouse.click(24, 28);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/ai-created.txt");
  await expect.poll(() => readTextIfExists(page, "/ai-created.txt")).toBe("created from ai\n");
  await expect.poll(() => readTextIfExists(page, "/README.md")).toBe("updated open file\n");
  state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.filter((msg) => msg.role === "tool_call")).toHaveLength(3);
  expect(state.chatMessages.filter((msg) => msg.role === "tool_result")).toHaveLength(3);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text.includes("<tool>writeFile"))).toBe(false);
  expect(state.chatMessages.some((msg) => msg.role === "user" && msg.text.startsWith("<result>"))).toBe(false);
});

test("AI write tools require reading existing files before modification", async ({ page }) => {
  const completionRequests: Array<Record<string, any>> = [];
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<tool>writeFile("/README.md", "unsafe overwrite\\n")</tool>'
        : nonProbeCompletions === 2
          ? "blocked overwrite"
          : nonProbeCompletions === 3
            ? '<tool>readFile("/README.md")</tool>'
            : nonProbeCompletions === 4
              ? '<tool>writeFile("/README.md", "safe overwrite\\n")</tool>'
              : "safe overwrite done";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.chat.send("overwrite without reading", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  let state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "tool_result" && msg.name === "writeFile" && msg.ok === false && msg.text.includes("call readFile first"))).toBe(true);
  await expect.poll(() => readTextIfExists(page, "/README.md")).not.toBe("unsafe overwrite\n");
  expect(completionRequests.filter((request) => request.max_tokens !== 1)).toHaveLength(2);

  await page.evaluate(async () => {
    const app = window.__slugApp! as any;
    await app.chat.send("read then overwrite", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  await expect.poll(() => readTextIfExists(page, "/README.md")).toBe("safe overwrite\n");
  state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "safe overwrite done")).toBe(true);
  expect(state.chatMessages.filter((msg) => msg.role === "tool_result" && msg.name === "writeFile" && msg.ok).length).toBe(1);
});

test("AI tool batches stop after the first failed tool call", async ({ page }) => {
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<tool>bash("echo bad > bad.txt")</tool>\n<tool>writeFile("/should-not-run.txt", "bad\\n")</tool>'
        : "stopped after failed tool";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.chat.send("run a bad batch", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: false,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  await expect.poll(() => readTextIfExists(page, "/should-not-run.txt")).toBeNull();
  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "tool_result" && msg.name === "bash" && msg.ok === false && msg.text.includes("shell operators are not supported"))).toBe(true);
  expect(state.chatMessages.some((msg) => msg.role === "tool_call" && msg.name === "writeFile")).toBe(false);
  expect(state.chatMessages.some((msg) => msg.role === "assistant" && msg.text === "stopped after failed tool")).toBe(true);
});

test("AI bash rm expands globs and reports removals", async ({ page }) => {
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<tool>bash("rm -rf *")</tool>'
        : "removed";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    await app.vfs.resetToEmpty();
    await app.vfs.writeFile("/alpha.txt", "alpha", "text/plain");
    await app.vfs.writeFile("/folder/beta.txt", "beta", "text/plain");
    await app.refreshFiles();
    app.chat.clear();
    await app.chat.send("clear with shell glob", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: false,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.stat("/alpha.txt").then(Boolean))).toBe(false);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.vfs.stat("/folder").then(Boolean))).toBe(false);
  const state = await appState<CanvasTargets>(page);
  const rmResult = state.chatMessages.find((msg) => msg.role === "tool_result" && msg.name === "bash" && msg.ok);
  expect(rmResult?.text).toMatch(/^Removed \d+ paths?$/);
});

test("AI bash rejects unsupported flags loudly", async ({ page }) => {
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<tool>bash("ls -la")</tool>'
        : "flag rejected";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.chat.send("try unsupported ls flags", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: false,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "tool_result" && msg.name === "bash" && msg.ok === false && msg.text.includes("unsupported browser flag: -la"))).toBe(true);
});

test("AI writeFile requires a read after bash-created files", async ({ page }) => {
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<tool>bash("touch /scratch.txt")</tool>'
        : nonProbeCompletions === 2
          ? '<tool>writeFile("/scratch.txt", "unsafe\\n")</tool>'
          : "blocked";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.chat.send("touch then write without reading", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: false,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  await expect.poll(() => readTextIfExists(page, "/scratch.txt")).toBe("");
  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.some((msg) => msg.role === "tool_result" && msg.name === "writeFile" && msg.ok === false && msg.text.includes("call readFile first"))).toBe(true);
});

test("harmony writeFile calls create files when the model omits the call terminator", async ({ page }) => {
  let nonProbeCompletions = 0;
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    const isProbe = body.max_tokens === 1;
    if (!isProbe) nonProbeCompletions++;
    const content = isProbe
      ? "x"
      : nonProbeCompletions === 1
        ? '<|channel|>commentary to=writeFile <|constrain|>json<|message|>{"path":"/harmony-created.txt","content":"created by harmony\\n"}'
        : "harmony file created";
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content } }],
        usage: isProbe
          ? { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
          : { prompt_tokens: 40 + nonProbeCompletions, completion_tokens: 4, total_tokens: 44 + nonProbeCompletions }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    await app.chat.send("make a new file", undefined, app.docs.all(), {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "harmony",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw(),
      onWorkspaceChange: (change: unknown) => app.handleAiWorkspaceChange(change)
    });
  });

  await expect.poll(() => readTextIfExists(page, "/harmony-created.txt")).toBe("created by harmony\n");
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.sidebarMode = "files";
    if (app.sidebarWidth === 0) app.sidebarWidth = app.lastSidebarWidth || 280;
    app.draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).fileTargets.map((item) => item.path)).toContain("/harmony-created.txt");
});

test("chat auto-compacts after a model response crosses the threshold", async ({ page }) => {
  const completionRequests: Array<Record<string, any>> = [];
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const index = completionRequests.length;
    if (index === 1) {
      await route.fulfill({
        status: 200,
        contentType: "application/json",
        headers: { "Access-Control-Allow-Origin": "*" },
        body: JSON.stringify({
          choices: [{ message: { content: "x" } }],
          usage: { prompt_tokens: 20, completion_tokens: 1, total_tokens: 21 }
        })
      });
      return;
    }
    if (index === 2) {
      await route.fulfill({
        status: 200,
        contentType: "application/json",
        headers: { "Access-Control-Allow-Origin": "*" },
        body: JSON.stringify({
          choices: [{ message: { content: "full response before compact" } }],
          usage: { prompt_tokens: 40, completion_tokens: 8, total_tokens: 48 }
        })
      });
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 1000));
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content: "User wants a browser editor; continue from the current AI compaction work." } }],
        usage: { prompt_tokens: 42, completion_tokens: 12, total_tokens: 54 }
      })
    });
  });

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 50
    }));
    localStorage.setItem("slug.aiCompactPrompt", "Compact hard. Preserve intent, decisions, files, errors, and next steps.");
  });

  await page.mouse.click(24, 124);
  await page.keyboard.type("please do the work");
  await page.keyboard.press("Enter");

  await expect.poll(() => completionRequests.length).toBe(3);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal?.kind).toBe("compactProgress");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal).toBeNull();

  expect(completionRequests[0]!.max_tokens).toBe(1);
  expect(completionRequests[0]!.messages.at(-1)).toEqual({ role: "user", content: "test" });
  expect(completionRequests[1]!.messages.at(-1)).toEqual({ role: "user", content: "please do the work" });
  expect(completionRequests[2]!.messages[0]).toEqual({ role: "system", content: "Compact hard. Preserve intent, decisions, files, errors, and next steps." });
  expect(completionRequests[2]!.messages.at(-1)).toEqual({ role: "user", content: "compact / summarize this chat" });

  const compactRoles = completionRequests[2]!.messages.map((msg: { role: string; content: string }) => msg.role);
  expect(compactRoles).toEqual(["system", "user", "assistant", "user"]);
  expect(completionRequests[2]!.messages.map((msg: { content: string }) => msg.content).join("\n")).not.toContain("<tool>");

  const state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.map((msg) => ({ role: msg.role, text: msg.text }))).toEqual([
    {
      role: "system",
      text: "Summary of compacted conversation\n\nUser wants a browser editor; continue from the current AI compaction work."
    }
  ]);
  expect(state.chatTokenUsage.source).toBe("estimate");
  expect(state.chatTokenUsage.dirty).toBe(true);
});

test("chat header context menu exports and clears chat", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Browser download behavior is covered in desktop Chromium.");
  await page.mouse.click(24, 124);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("chat");
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.chat.clear();
    app.chat.messages.push(
      { id: "msg-user", role: "user", text: "hello", at: 1710000000000 },
      { id: "msg-assistant", role: "assistant", text: "world", at: 1710000001000 },
      { id: "msg-internal-assistant", role: "assistant", text: "<tool>readFile(\"/README.md\")</tool>", at: 1710000002000, internal: true },
      { id: "msg-internal-user", role: "user", text: "<result>readme contents</result>", at: 1710000003000, internal: true }
    );
    app.scheduleDraw();
  });

  let state = await appState<CanvasTargets>(page);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatRootTarget).not.toBeNull();
  await page.mouse.click(...pointArgs(center(state.chatRootTarget!)), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "chatRoot" });
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["exportChat", "debugChat", "clearChat", "compactChat"]);
  expect(menuItem(state, "exportChat").enabled).toBe(true);
  expect(menuItem(state, "debugChat").enabled).toBe(true);
  expect(menuItem(state, "clearChat").enabled).toBe(true);
  expect(menuItem(state, "compactChat").enabled).toBe(true);

  const downloadPromise = page.waitForEvent("download");
  await clickMenuItem(page, state, "exportChat");
  const download = await downloadPromise;
  expect(download.suggestedFilename()).toMatch(/^chat-\d{8}-\d{6}\.jsonl$/);
  const path = await download.path();
  expect(path).toBeTruthy();
  const rows = (await readFile(path!, "utf8")).trim().split("\n").map((line) => JSON.parse(line) as { role: string; text: string; at: string });
  expect(rows.map((row) => ({ role: row.role, text: row.text }))).toEqual([
    { role: "user", text: "hello" },
    { role: "assistant", text: "world" }
  ]);
  expect(rows[0]!.at).toBe("2024-03-09T16:00:00.000Z");

  await page.evaluate(() => {
    localStorage.setItem("slug.aiSystemPrompt", "Debug system prompt\n\nAvailable tools:\n- readFile(path)\n\nTag tool-call format:\n<tool>readFile(\"/README.md\")</tool>\n\nHarmony-style tool-call format:\n<|channel|>commentary to=readFile <|message|>{\"path\":\"/README.md\"}<|call|>");
    localStorage.setItem("slug.aiTagToolPrompt", "Debug tag tool prompt");
    const app = window.__slugApp! as any;
    app.chat.messages.unshift({ id: "msg-ui-system", role: "system", text: "AI agent ready. Configure an OpenAI-compatible endpoint in Settings.", at: 1709999999000 });
    app.chat.messages.push({ id: "msg-thinking", role: "thinking", text: "private thought that should not be sent", at: 1710000004000 });
    app.chat.messages.push({ id: "msg-old-repair", role: "user", text: "Your previous response put a tool call inside hidden reasoning/thinking. Hidden reasoning is not executable.", at: 1710000005000, internal: true });
    app.settings = { ...app.settings, aiInsertEditorContext: false };
    app.saveAndApplySettings();
    app.draw();
  });

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.chatRootTarget!)), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "debugChat");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("Debug Chat");
  const debugState = await appState<CanvasTargets>(page);
  const debugRows = debugState.activeText!.trim().split("\n").map((line) => JSON.parse(line) as { role: string; content: string; index: number });
  expect(debugRows.map((row) => ({ role: row.role, content: row.content }))).toEqual([
    { role: "system", content: "Debug system prompt\n\nDebug tag tool prompt" },
    { role: "user", content: "hello" },
    { role: "assistant", content: "world" },
    { role: "assistant", content: "<tool>readFile(\"/README.md\")</tool>" },
    { role: "user", content: "<result>readme contents</result>" }
  ]);
  expect(debugRows.map((row) => row.index)).toEqual([0, 1, 2, 3, 4]);
  expect(debugState.activeText).not.toContain("AI agent ready");
  expect(debugState.activeText).not.toContain("private thought that should not be sent");
  expect(debugState.activeText).not.toContain("Tag tool-call format");
  expect(debugState.activeText).not.toContain("Your previous response put a tool call inside hidden reasoning");
  expect(debugState.statusText).toBe("Opened chat debug JSONL");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.chatRootTarget!)), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "clearChat");
  state = await waitForModal(page);
  expect(state.modal?.kind).toBe("clearChat");
  expect(state.chatMessages.length).toBeGreaterThan(0);
  await clickModalButton(page, state, "cancel");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.length).toBeGreaterThan(0);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.chatRootTarget!)), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "clearChat");
  state = await waitForModal(page);
  await clickModalButton(page, state, "clearChat");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).statusText).toBe("Chat cleared");
});

test("chat bubble context menu copies and confirms clearing chat", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Desktop chat bubble context menu behavior is covered in Chromium.");
  await page.mouse.click(24, 124);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("chat");
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.chat.clear();
    app.chat.messages.push(
      { id: "msg-user", role: "user", text: "hello bubble", at: 1710000000000 },
      { id: "msg-assistant", role: "assistant", text: "assistant reply", at: 1710000001000 }
    );
    app.draw();
  });

  let state = await appState<CanvasTargets>(page);
  const bubble = state.chatBubbleTargets.find((target) => target.text === "assistant reply");
  if (!bubble) throw new Error("Missing assistant chat bubble");
  await page.mouse.click(...pointArgs(center(bubble.rect)), { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.contextMenu?.scope).toEqual({ type: "chatBubble", messageId: "msg-assistant" });
  expect(state.contextMenu?.items.map((item) => item.command)).toEqual(["copyBubble", "copyChat", "clearChat"]);

  await clickMenuItem(page, state, "copyBubble");
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe("assistant reply");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(bubble.rect)), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "copyChat");
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe("USER\nhello bubble\n\nASSISTANT\nassistant reply");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(bubble.rect)), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "clearChat");
  state = await waitForModal(page);
  expect(state.modal?.kind).toBe("clearChat");
  await clickModalButton(page, state, "clearChat");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages).toEqual([]);
});

test("chat header compact command manually compacts the current chat", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas context-menu AI flow is covered in desktop Chromium.");
  const completionRequests: Array<Record<string, any>> = [];
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    await new Promise((resolve) => setTimeout(resolve, 1000));
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content: "Keep implementing the browser editor chat header controls." } }],
        usage: { prompt_tokens: 41, completion_tokens: 9, total_tokens: 50 }
      })
    });
  });

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    localStorage.setItem("slug.aiCompactPrompt", "Compact manually. Preserve user intent and next steps.");
    const app = window.__slugApp! as any;
    app.sidebarMode = "chat";
    app.chat.clear();
    app.chat.messages.push(
      { id: "msg-user", role: "user", text: "add chat tools", at: Date.now() },
      { id: "msg-assistant", role: "assistant", text: "I will add them. <tool>readFile(\"/README.md\")</tool>", at: Date.now() },
      { id: "msg-tool", role: "tool_result", name: "readFile", ok: true, text: "readme contents", at: Date.now() }
    );
    app.draw();
  });

  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatRootTarget).not.toBeNull();
  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.chatRootTarget!)), { button: "right" });
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "compactChat");

  await expect.poll(() => completionRequests.length).toBe(1);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal?.kind).toBe("compactProgress");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).modal).toBeNull();
  expect(completionRequests[0]!.max_tokens).toBe(700);
  expect(completionRequests[0]!.messages[0]).toEqual({ role: "system", content: "Compact manually. Preserve user intent and next steps." });
  expect(completionRequests[0]!.messages.at(-1)).toEqual({ role: "user", content: "compact / summarize this chat" });
  expect(completionRequests[0]!.messages.map((msg: { role: string }) => msg.role)).toEqual(["system", "user", "assistant", "user"]);
  expect(completionRequests[0]!.messages.map((msg: { content: string }) => msg.content).join("\n")).not.toContain("<tool>");

  state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.map((msg) => ({ role: msg.role, text: msg.text }))).toEqual([
    {
      role: "system",
      text: "Summary of compacted conversation\n\nKeep implementing the browser editor chat header controls."
    }
  ]);
  expect(state.statusText).toBe("Conversation compacted.");
});

test("compacted system summaries are kept in model context", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "OpenAI-compatible request payload shape is covered in desktop Chromium.");
  const completionRequests: Array<Record<string, any>> = [];
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    if (route.request().method() === "OPTIONS") {
      await route.fulfill({
        status: 204,
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "POST, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type, Authorization"
        }
      });
      return;
    }
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content: isProbe ? "x" : "continued from summary" } }],
        usage: isProbe
          ? { prompt_tokens: 38, completion_tokens: 1, total_tokens: 39 }
          : { prompt_tokens: 44, completion_tokens: 3, total_tokens: 47 }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    app.chat.messages.push(
      { id: "msg-summary", role: "system", text: "Summary of compacted conversation\n\nThe user wants chat header controls implemented.", at: Date.now() },
      { id: "msg-error", role: "system", text: "Request failed: HTTP 503", at: Date.now() }
    );
    await app.chat.send("continue", undefined, [], {
      runtime: {
        maxToolCallsPerTurn: 50,
        detectDuplicateToolCalls: true,
        toolCallFormat: "tag",
        compactFreePercent: 10
      },
      onUpdate: () => app.scheduleDraw()
    });
  });

  await expect.poll(() => completionRequests.length).toBe(2);
  expect(completionRequests[0]!.max_tokens).toBe(1);
  expect(completionRequests[0]!.messages).toContainEqual({
    role: "user",
    content: "Summary of compacted conversation\n\nThe user wants chat header controls implemented."
  });
  expect(completionRequests[1]!.messages).toContainEqual({
    role: "user",
    content: "Summary of compacted conversation\n\nThe user wants chat header controls implemented."
  });
  expect(completionRequests[1]!.messages.at(-1)).toEqual({ role: "user", content: "continue" });
  expect(completionRequests[1]!.messages.map((msg: { content: string }) => msg.content).join("\n")).not.toContain("Request failed: HTTP 503");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "assistant" && msg.text === "continued from summary")).toBe(true);
});

test("chat inserts transient editor context when enabled", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "OpenAI-compatible request payload shape is covered in desktop Chromium.");
  const completionRequests: Array<Record<string, any>> = [];
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    if (route.request().method() === "OPTIONS") {
      await route.fulfill({
        status: 204,
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "POST, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type, Authorization"
        }
      });
      return;
    }
    const body = route.request().postDataJSON() as Record<string, any>;
    completionRequests.push(body);
    const isProbe = body.max_tokens === 1;
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content: isProbe ? "x" : "context ok" } }],
        usage: isProbe
          ? { prompt_tokens: 70, completion_tokens: 1, total_tokens: 71 }
          : { prompt_tokens: 84, completion_tokens: 2, total_tokens: 86 }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.chat.clear();
    app.settings = { ...app.settings, aiInsertEditorContext: true };
    app.saveAndApplySettings();
    await app.openFile("/README.md");
    app.activeDoc().setSelection({ line: 0, col: 0 }, { line: 0, col: 15 });
    app.selectFileTreePath("/notes");
    app.sidebarMode = "chat";
    if (app.sidebarWidth === 0) app.sidebarWidth = app.lastSidebarWidth || 280;
    app.chatDraft.replaceSelection("describe the workspace");
    await app.sendChat();
  });

  await expect.poll(() => completionRequests.length).toBe(2);
  const probeMessages = completionRequests[0]!.messages as Array<{ role: string; content: string }>;
  expect(probeMessages[1]).toEqual(expect.objectContaining({ role: "user", content: expect.stringContaining("<editor-context>") }));
  expect(probeMessages.at(-1)).toEqual({ role: "user", content: "test" });
  const messages = completionRequests[1]!.messages as Array<{ role: string; content: string }>;
  expect(messages[0]!.role).toBe("system");
  expect(messages[1]).toEqual(expect.objectContaining({ role: "user", content: expect.stringContaining("<editor-context>") }));
  expect(messages.at(-1)).toEqual({ role: "user", content: "describe the workspace" });
  const context = messages[1]!.content;
  expect(context).toContain("File tree:");
  expect(context).toContain("- /README.md");
  expect(context).toContain("- /src/");
  expect(context).toContain("- /src/main.ts");
  expect(context).toContain("Open files:");
  expect(context).toContain("- README.md");
  expect(context).toContain("Selected in file tree: /notes");
  expect(context).toContain("Active file: /README.md");
  expect(context).toContain("Active editor selection from /README.md:");
  expect(context).toContain("# Carrot Editor");
  expect(context).toContain("Use readFile before relying on or modifying existing file contents.");
  expect((await appState<CanvasTargets>(page)).chatMessages.map((msg) => msg.text).join("\n")).not.toContain("<editor-context>");
});

test("chat requests append the selected editable tool prompt", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "OpenAI-compatible request payload shape is covered in desktop Chromium.");
  const completionRequests: Array<Record<string, any>> = [];
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    if (route.request().method() === "OPTIONS") {
      await route.fulfill({
        status: 204,
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "POST, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type, Authorization"
        }
      });
      return;
    }
    completionRequests.push(route.request().postDataJSON() as Record<string, any>);
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        choices: [{ message: { content: "prompt ok" } }],
        usage: { prompt_tokens: 30 + completionRequests.length, completion_tokens: 1, total_tokens: 31 + completionRequests.length }
      })
    });
  });

  await page.evaluate(async () => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    localStorage.setItem("slug.aiSystemPrompt", "Base prompt");
    localStorage.setItem("slug.aiTagToolPrompt", "Tag tool prompt");
    localStorage.setItem("slug.aiHarmonyToolPrompt", "Harmony tool prompt");
    const app = window.__slugApp! as any;
    app.chat.clear();
    const runtime = (toolCallFormat: "tag" | "harmony" | "none") => ({
      maxToolCallsPerTurn: 50,
      detectDuplicateToolCalls: true,
      toolCallFormat,
      compactFreePercent: 10
    });
    await app.chat.send("tag turn", undefined, [], { runtime: runtime("tag"), onUpdate: () => app.scheduleDraw() });
    localStorage.setItem("slug.aiHarmonyToolPrompt", "Updated harmony prompt");
    await app.chat.send("harmony turn", undefined, [], { runtime: runtime("harmony"), onUpdate: () => app.scheduleDraw() });
    localStorage.setItem("slug.aiSystemPrompt", "Updated base prompt");
    await app.chat.send("none turn", undefined, [], { runtime: runtime("none"), onUpdate: () => app.scheduleDraw() });
  });

  await expect.poll(() => completionRequests.length).toBe(6);
  expect(completionRequests[1]!.messages[0]).toEqual({ role: "system", content: "Base prompt\n\nTag tool prompt" });
  expect(completionRequests[3]!.messages[0]).toEqual({ role: "system", content: "Base prompt\n\nUpdated harmony prompt" });
  expect(completionRequests[5]!.messages[0]).toEqual({ role: "system", content: "Updated base prompt" });
});

test("chat sidebar supports multiline input, scrollbars, and send button turns", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas chat hit geometry is covered in desktop Chromium.");
  await page.mouse.click(24, 124);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatInputRect).not.toBeNull();
  let state = await appState<CanvasTargets>(page);
  expect(state.chatMessages).toEqual([]);
  const lineH = await page.evaluate(() => window.__slugApp!.renderer.lineHeight("ui"));
  expect(state.chatInputRect!.h).toBeGreaterThanOrEqual(lineH * 4 + 8);
  expect(state.chatInputRect!.h).toBeLessThanOrEqual(lineH * 4 + 22);
  await page.keyboard.type("line one");
  await page.keyboard.press("Shift+Enter");
  await page.keyboard.type("line two");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatDraft).toBe("line one\nline two");

  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatSendTarget?.enabled ?? false).toBe(true);
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.chatSendTarget!.rect)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatDraft).toBe("");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "user" && msg.text === "line one\nline two")).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "system" && msg.ok === false && msg.text.includes("No model is configured"))).toBe(true);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    for (let i = 0; i < 18; i++) app.chat.messages.push({ id: `test-${i}`, role: i % 2 ? "assistant" : "user", text: `message ${i} `.repeat(10), at: Date.now() });
    app.scheduleDraw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatScrollbars.some((bar) => bar.panel === "chatTranscript")).toBe(true);
  state = await appState<CanvasTargets>(page);
  const transcriptBar = state.chatScrollbars.find((bar) => bar.panel === "chatTranscript")!;
  await page.mouse.move(transcriptBar.trackRect.x + 2, transcriptBar.trackRect.y + transcriptBar.trackRect.h / 2);
  await page.mouse.wheel(0, 600);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatScrollY).toBeGreaterThan(0);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.chatDraft.selectAll();
    app.chatDraft.replaceSelection("wrapped composer text ".repeat(60));
    app.chatInputScrollY = 0;
    app.scheduleDraw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatScrollbars.some((bar) => bar.panel === "chatInput")).toBe(true);
});

test("chat show thinking checkbox hides only thinking bubbles", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas chat hit geometry is covered in desktop Chromium.");
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.toggleActivityMode("chat");
    app.chat.messages.push({ id: "msg-user", role: "user", text: "question", at: Date.now() });
    app.chat.messages.push({ id: "msg-thinking", role: "thinking", text: "private thought", at: Date.now() });
    app.chat.messages.push({ id: "msg-assistant", role: "assistant", text: "answer", at: Date.now() });
    app.draw();
  });

  let state = await appState<CanvasTargets>(page);
  expect(state.chatShowThinking).toBe(true);
  expect(state.chatShowThinkingTarget).not.toBeNull();
  expect(state.chatShowThinkingTarget!.x).toBeLessThan(state.chatSendTarget!.rect.x);
  expect(state.chatMessages.map((msg) => msg.role)).toEqual(["user", "thinking", "assistant"]);
  expect(state.chatDisplayedMessages.map((msg) => msg.role)).toEqual(["user", "thinking", "assistant"]);

  await page.mouse.click(...pointArgs(center(state.chatShowThinkingTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatShowThinking).toBe(false);
  state = await appState<CanvasTargets>(page);
  expect(state.chatMessages.map((msg) => msg.role)).toEqual(["user", "thinking", "assistant"]);
  expect(state.chatDisplayedMessages.map((msg) => msg.role)).toEqual(["user", "assistant"]);
  expect(state.settings.showThinking).toBe(false);

  await page.mouse.click(...pointArgs(center(state.chatShowThinkingTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatDisplayedMessages.map((msg) => msg.role)).toEqual(["user", "thinking", "assistant"]);
});

test("chat send button becomes stop and cancels a running turn", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas chat hit geometry is covered in desktop Chromium.");
  const completionGate: { release?: () => void } = {};
  await page.route("http://localhost:1234/v1/chat/completions", async (route) => {
    const body = route.request().postDataJSON() as Record<string, any>;
    const isProbe = body.max_tokens === 1;
    if (isProbe) {
      await route.fulfill({
        status: 200,
        contentType: "application/json",
        headers: { "Access-Control-Allow-Origin": "*" },
        body: JSON.stringify({
          choices: [{ message: { content: "x" } }],
          usage: { prompt_tokens: 30, completion_tokens: 1, total_tokens: 31 }
        })
      });
      return;
    }
    await new Promise<void>((resolve) => {
      completionGate.release = resolve;
    });
    try {
      await route.fulfill({
        status: 200,
        contentType: "application/json",
        headers: { "Access-Control-Allow-Origin": "*" },
        body: JSON.stringify({
          choices: [{ message: { content: "late response" } }],
          usage: { prompt_tokens: 42, completion_tokens: 3, total_tokens: 45 }
        })
      });
    } catch {
      // The request may already be aborted by the Stop button.
    }
  });

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://localhost:1234/v1",
      apiKey: "",
      model: "local-test-model",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
  });

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.toggleActivityMode("chat");
    app.draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatInputRect).not.toBeNull();
  await page.keyboard.type("slow turn");
  let state = await appState<CanvasTargets>(page);
  expect(state.chatSendTarget?.label).toBe("Send");
  expect(state.chatSendTarget?.enabled).toBe(true);
  await page.mouse.click(...pointArgs(center(state.chatSendTarget!.rect)));

  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatRunning).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatSendTarget?.label).toBe("Stop");
  await expect.poll(() => Boolean(completionGate.release)).toBe(true);
  state = await appState<CanvasTargets>(page);
  expect(state.chatSendTarget?.enabled).toBe(true);
  await page.mouse.click(...pointArgs(center(state.chatSendTarget!.rect)));
  completionGate.release?.();

  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatRunning).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "system" && msg.text === "Turn canceled.")).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatSendTarget?.label).toBe("Send");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).chatMessages.some((msg) => msg.role === "assistant" && msg.text === "late response")).toBe(false);
});

test("AI settings edit prompts and configure OpenAI-compatible endpoint", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas settings controls are covered in desktop Chromium.");
  await page.route("http://localhost:1234/v1/models", async (route) => {
    const corsHeaders = {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, OPTIONS",
      "Access-Control-Allow-Headers": "authorization, content-type"
    };
    if (route.request().method() === "OPTIONS") {
      await route.fulfill({ status: 204, headers: corsHeaders });
      return;
    }
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      headers: corsHeaders,
      body: JSON.stringify({ data: [{ id: "local-test-model", max_context_length: 8192 }, { id: "other-model", max_context_length: 4096 }] })
    });
  });
  await page.route("http://localhost:1240/v1/models", async (route) => {
    const corsHeaders = {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, OPTIONS",
      "Access-Control-Allow-Headers": "authorization, content-type"
    };
    if (route.request().method() === "OPTIONS") {
      await route.fulfill({ status: 204, headers: corsHeaders });
      return;
    }
    await route.fulfill({
      status: 401,
      contentType: "text/plain",
      headers: corsHeaders,
      body: "bad api key"
    });
  });

  let state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(state.settingsActivityTarget!)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).sidebarMode).toBe("settings");

  const mod = desktopShortcutModifier();
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "textField", "aiBaseUrl"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("localhost");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.apiBaseUrl).toBe("localhost");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "textField", "aiBaseUrl"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("192.168.0.174");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.apiBaseUrl).toBe("192.168.0.174");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "textField", "aiBaseUrl"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("localhost");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.apiBaseUrl).toBe("localhost");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "checkAiServer"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiConnectionStatus.state).toBe("ok");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiConnectionStatus.baseUrl).toBe("http://localhost:1234/v1");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointFieldState).toBe("ok");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "textField", "aiApiKey"))));
  await page.keyboard.type("local-secret");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.apiKey).toBe("local-secret");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "textField", "aiBaseUrl"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("localhost:1240");
  await page.keyboard.press("Enter");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "checkAiServer"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiConnectionStatus.state).toBe("error");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiConnectionStatus.message).toContain("HTTP 401");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointFieldState).toBe("error");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "textField", "aiBaseUrl"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("localhost");
  await page.keyboard.press("Enter");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "probeLmStudioModels"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiModels.map((model) => model.id)).toContain("local-test-model");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiConnectionStatus.message).toContain("Found");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiConnectionStatus.baseUrl).toBe("http://localhost:1234/v1");
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiModels"))).toBeNull();

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsDropdown", "aiModel"))));
  state = await waitForContextMenu(page);
  const modelCommand = state.contextMenu!.items.find((item) => item.command.startsWith("aiModel:local-test-model"))!.command as ContextCommand;
  await clickMenuItem(page, state, modelCommand);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.model).toBe("local-test-model");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.maxContextTokens).toBe(8192);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "aiModelManual"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.aiModelManual).toBe(true);
  state = await appState<CanvasTargets>(page);
  expect(state.settingsTargets.some((target) => target.type === "textField" && target.key === "aiModel")).toBe(true);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "textField", "aiModel"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("manual-model");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.model).toBe("manual-model");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "aiModelManual"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.aiModelManual).toBe(false);
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsDropdown", "aiModel"))));
  state = await waitForContextMenu(page);
  const restoredModelCommand = state.contextMenu!.items.find((item) => item.command.startsWith("aiModel:local-test-model"))!.command as ContextCommand;
  await clickMenuItem(page, state, restoredModelCommand);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.model).toBe("local-test-model");

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "textField", "aiMaxContextTokens"))));
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("4096");
  await page.keyboard.press("Enter");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.maxContextTokens).toBe(4096);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "probeLmStudioMaxTokens"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig.maxContextTokens).toBe(8192);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settingsScrollY = 120;
    app.draw();
  });
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "editSystemPrompt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("System Prompt");
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("Custom system prompt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? false)).toBe(true);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiSystemPrompt"))).not.toBe("Custom system prompt");
  await page.keyboard.press(`${mod}+S`);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiSystemPrompt"))).toBe("Custom system prompt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? true)).toBe(false);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "editTagToolPrompt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("Tag Tool Prompt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toContain("Supported bash commands:");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toContain("Use grep(pattern) or grepFile(pattern, path) instead of shell grep.");
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("Custom tag tool prompt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? false)).toBe(true);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiTagToolPrompt"))).not.toBe("Custom tag tool prompt");
  await page.keyboard.press(`${mod}+S`);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiTagToolPrompt"))).toBe("Custom tag tool prompt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? true)).toBe(false);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "editHarmonyToolPrompt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("Harmony Tool Prompt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toContain("Supported bash commands:");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeText).toContain("Use grep(pattern) or grepFile(pattern, path) instead of shell grep.");
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("Custom harmony tool prompt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? false)).toBe(true);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiHarmonyToolPrompt"))).not.toBe("Custom harmony tool prompt");
  await page.keyboard.press(`${mod}+S`);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiHarmonyToolPrompt"))).toBe("Custom harmony tool prompt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? true)).toBe(false);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settingsScrollY = 220;
    app.draw();
  });
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "editCompactPrompt"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("Compact Prompt");
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type("Custom compact prompt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? false)).toBe(true);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiCompactPrompt"))).not.toBe("Custom compact prompt");
  await page.keyboard.press(`${mod}+S`);
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiCompactPrompt"))).toBe("Custom compact prompt");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.activeDoc()?.dirty ?? true)).toBe(false);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settingsScrollY = 520;
    app.draw();
  });
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsDropdown", "aiToolCallFormat"))));
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "aiToolFormatHarmony");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.aiToolCallFormat).toBe("harmony");
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsDropdown", "aiToolCallFormat"))));
  state = await waitForContextMenu(page);
  await clickMenuItem(page, state, "aiToolFormatNone");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.aiToolCallFormat).toBe("none");
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settingsScrollY = 620;
    app.draw();
  });
  state = await appState<CanvasTargets>(page);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.aiInsertEditorContext).toBe(true);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsCheckbox", "aiInsertEditorContext"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.aiInsertEditorContext).toBe(false);
  await expect.poll(() => page.evaluate(() => JSON.parse(localStorage.getItem("slug.settings") ?? "{}").aiInsertEditorContext)).toBe(false);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settingsExpanded.add("danger");
    app.settingsScrollY = Number.MAX_SAFE_INTEGER;
    app.draw();
  });
  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(settingTarget(state.settingsTargets, "settingsButton", "resetAll"))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.aiInsertEditorContext).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settings.aiToolCallFormat).toBe("tag");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiModels).toEqual([]);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).aiEndpointConfig).toEqual({
    apiBaseUrl: "http://localhost:1234/v1",
    apiKey: "",
    model: "",
    temperature: 0.2,
    maxContextTokens: 0
  });
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiSystemPrompt"))).toBeNull();
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiTagToolPrompt"))).toBeNull();
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiHarmonyToolPrompt"))).toBeNull();
  await expect.poll(() => page.evaluate(() => localStorage.getItem("slug.aiCompactPrompt"))).toBeNull();
  await expect.poll(() => page.evaluate(() => ({
    text: window.__slugApp!.docs.getByPath("/.slug-system-prompt.md")?.getText() ?? "",
    dirty: window.__slugApp!.docs.getByPath("/.slug-system-prompt.md")?.dirty ?? true
  }))).toEqual({ text: "You are an AI coding assistant inside a browser code editor.\n\nYou can inspect and edit the virtual workspace using tool calls when tools are available. Keep responses concise, use tools when you need file contents, and prefer precise edits over broad rewrites.", dirty: false });
});

test("tabs close, reorder, and the sidebar splitter resizes", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
    await app.openFile("/notes/shortcuts.txt");
  });
  await expect.poll(async () => (await appState<{ openTabs: string[] }>(page)).openTabs.length).toBe(3);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(3);

  let state = await appState<CanvasTargets>(page);
  const readme = targetFor(state.tabTargets, "/README.md");
  const last = state.tabTargets.at(-1)!;
  await drag(page, center(readme), center(last.rect));
  await expect.poll(async () => (await appState<{ openTabs: string[] }>(page)).openTabs.at(-1)).toBe("/README.md");

  state = await appState<CanvasTargets>(page);
  const readmeClose = targetFor(state.tabCloseTargets, "/README.md");
  await page.mouse.click(...pointArgs(center(readmeClose)));
  await expect.poll(async () => (await appState<{ openTabs: string[] }>(page)).openTabs).not.toContain("/README.md");

  state = await appState<CanvasTargets>(page);
  const initialWidth = state.sidebarWidth;
  const splitter = state.sidebarResizeTarget!;
  await drag(page, center(splitter), { x: center(splitter).x + 140, y: center(splitter).y });
  await expect.poll(async () => (await appState<{ sidebarWidth: number }>(page)).sidebarWidth).toBeGreaterThan(initialWidth + 80);
});

test("activity bar toggles the sidebar and tab drag shows a dock preview", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.mouse.click(24, 24);
  await expect.poll(async () => (await appState<{ sidebarVisible: boolean; sidebarWidth: number }>(page)).sidebarVisible).toBe(false);
  await page.mouse.click(24, 24);
  await expect.poll(async () => (await appState<{ sidebarVisible: boolean; sidebarWidth: number }>(page)).sidebarVisible).toBe(true);

  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(2);
  const state = await appState<CanvasTargets>(page);
  const mainTab = targetFor(state.tabTargets, "/src/main.ts");
  const group = state.editorGroups[0]!;
  const from = center(mainTab);
  const to = { x: group.frameRect.x + group.frameRect.w - 24, y: group.frameRect.y + group.frameRect.h / 2 };
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move(to.x, to.y, { steps: 6 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).dockPreview?.zone ?? null).toBe("right");
  await page.mouse.up();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(2);
});

test("dock splitters resize left-right dock panes", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    await window.__slugApp!.openFile("/README.md");
    await window.__slugApp!.openFile("/src/main.ts");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(2);

  let state = await appState<CanvasTargets>(page);
  const mainTab = targetFor(state.tabTargets, "/src/main.ts");
  const group = state.editorGroups[0]!;
  await drag(page, center(mainTab), { x: group.frameRect.x + group.frameRect.w - 24, y: group.frameRect.y + group.frameRect.h / 2 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(2);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).dockSplitters.some((splitter) => splitter.direction === "row")).toBe(true);

  state = await appState<CanvasTargets>(page);
  const splitter = state.dockSplitters.find((item) => item.direction === "row")!;
  const before = groupsByX(state.editorGroups);
  const leftBefore = before[0]!.frameRect.w;
  const rightBefore = before[1]!.frameRect.w;
  await drag(page, center(splitter.rect), { x: center(splitter.rect).x + 140, y: center(splitter.rect).y });

  await expect.poll(async () => groupsByX((await appState<CanvasTargets>(page)).editorGroups)[0]!.frameRect.w).toBeGreaterThan(leftBefore + 80);
  state = await appState<CanvasTargets>(page);
  const after = groupsByX(state.editorGroups);
  expect(after[1]!.frameRect.w).toBeLessThan(rightBefore - 80);
});

test("dock splitters resize top-bottom dock panes", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    await window.__slugApp!.openFile("/README.md");
    await window.__slugApp!.openFile("/src/main.ts");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(2);

  let state = await appState<CanvasTargets>(page);
  const mainTab = targetFor(state.tabTargets, "/src/main.ts");
  const group = state.editorGroups[0]!;
  await drag(page, center(mainTab), { x: group.frameRect.x + group.frameRect.w / 2, y: group.frameRect.y + group.frameRect.h - 24 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(2);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).dockSplitters.some((splitter) => splitter.direction === "column")).toBe(true);

  state = await appState<CanvasTargets>(page);
  const splitter = state.dockSplitters.find((item) => item.direction === "column")!;
  const before = groupsByY(state.editorGroups);
  const topBefore = before[0]!.frameRect.h;
  const bottomBefore = before[1]!.frameRect.h;
  await drag(page, center(splitter.rect), { x: center(splitter.rect).x, y: center(splitter.rect).y + 120 });

  await expect.poll(async () => groupsByY((await appState<CanvasTargets>(page)).editorGroups)[0]!.frameRect.h).toBeGreaterThan(topBefore + 70);
  state = await appState<CanvasTargets>(page);
  const after = groupsByY(state.editorGroups);
  expect(after[1]!.frameRect.h).toBeLessThan(bottomBefore - 70);
});

test("editor scrolling is per document and exposes a draggable scrollbar", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas wheel geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    const wideLine = Array.from({ length: 90 }, (_, index) => `alpha_${String(index + 1).padStart(2, "0")}`).join(" ");
    const longA = [wideLine, ...Array.from({ length: 139 }, (_, index) => `alpha ${String(index + 1).padStart(3, "0")}`)].join("\n");
    const longB = Array.from({ length: 140 }, (_, index) => `beta ${String(index + 1).padStart(3, "0")}`).join("\n");
    await app.vfs.writeFile("/long-a.txt", longA, "text/plain");
    await app.vfs.writeFile("/long-b.txt", longB, "text/plain");
    await app.refreshFiles();
    await app.openFile("/README.md");
    await app.openFile("/long-a.txt");
    await app.openFile("/long-b.txt");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBeGreaterThanOrEqual(3);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorScrollbars.some((bar) => bar.path === "/long-b.txt")).toBe(true);

  let state = await appState<CanvasTargets>(page);
  const activeGroup = state.editorGroups.find((group) => group.activePath === "/long-b.txt")!;
  await page.mouse.move(...pointArgs(center(activeGroup.editorRect)));
  await page.mouse.wheel(0, 900);
  await expect.poll(async () => (await activeGroupState(page, "/long-b.txt")).scrollY).toBeGreaterThan(500);

  state = await appState<CanvasTargets>(page);
  const bScroll = state.editorGroups.find((group) => group.activePath === "/long-b.txt")!.scrollY;
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/long-a.txt"))));
  await expect.poll(async () => (await activeGroupState(page, "/long-a.txt")).scrollY).toBe(0);

  state = await appState<CanvasTargets>(page);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorScrollbars.some((bar) => bar.path === "/long-a.txt" && bar.axis === "horizontal")).toBe(true);
  const bar = state.editorScrollbars.find((item) => item.path === "/long-a.txt" && item.axis === "vertical")!;
  const thumb = center(bar.thumbRect);
  await page.mouse.move(...pointArgs(thumb));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).hoveredScrollbar?.path ?? null).toBe("/long-a.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).hoveredScrollbar?.overThumb ?? false).toBe(true);
  await expect.poll(async () => canvasCursor(page)).toBe("");
  await page.mouse.down();
  await page.mouse.move(thumb.x, thumb.y + 120, { steps: 6 });
  await expect.poll(async () => canvasCursor(page)).toBe("");
  await page.mouse.up();
  await expect.poll(async () => (await activeGroupState(page, "/long-a.txt")).scrollY).toBeGreaterThan(300);

  state = await appState<CanvasTargets>(page);
  const horizontalBar = state.editorScrollbars.find((item) => item.path === "/long-a.txt" && item.axis === "horizontal")!;
  const horizontalThumb = center(horizontalBar.thumbRect);
  await drag(page, horizontalThumb, { x: horizontalThumb.x + 180, y: horizontalThumb.y });
  await expect.poll(async () => (await activeGroupState(page, "/long-a.txt")).scrollX).toBeGreaterThan(120);

  state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, "/long-b.txt"))));
  await expect.poll(async () => (await activeGroupState(page, "/long-b.txt")).scrollY).toBeGreaterThanOrEqual(bScroll - 1);
});

test("mobile touch drag scrolls editor document content", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile editor touch scrolling is covered in mobile WebKit.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    app.sidebarWidth = 0;
    const text = Array.from({ length: 180 }, (_, index) => `touch scroll line ${String(index + 1).padStart(3, "0")}`).join("\n");
    await app.vfs.writeFile("/touch-scroll.txt", text, "text/plain");
    await app.refreshFiles();
    await app.openFile("/touch-scroll.txt");
    (app as any).draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/touch-scroll.txt");
  const group = await activeGroupState(page, "/touch-scroll.txt");
  const from = { x: group.editorRect.x + group.editorRect.w * 0.55, y: group.editorRect.y + group.editorRect.h * 0.78 };
  const to = { x: from.x, y: group.editorRect.y + group.editorRect.h * 0.22 };
  await touchDrag(page, from, to);
  await expect.poll(async () => (await activeGroupState(page, "/touch-scroll.txt")).scrollY).toBeGreaterThan(80);
});

test("mobile long press selects a word and selection handles drag with scroll", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile long-press selection is covered in mobile WebKit.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    app.sidebarWidth = 0;
    const text = ["alpha beta gamma", ...Array.from({ length: 160 }, (_, index) => `line ${String(index + 1).padStart(3, "0")} content`)].join("\n");
    await app.vfs.writeFile("/mobile-select.txt", text, "text/plain");
    await app.refreshFiles();
    await app.openFile("/mobile-select.txt");
    (app as any).draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/mobile-select.txt");
  const group = await activeGroupState(page, "/mobile-select.txt");
  const lineH = await page.evaluate(() => window.__slugApp!.renderer.lineHeight("code"));
  const wordPoint = { x: group.editorRect.x + group.gutterWidth + 18, y: group.editorRect.y + lineH / 2 };

  await touchLongPress(page, wordPoint);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).selectedText).toBe("alpha");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).mobileSelectionHandles.length).toBe(2);

  const state = await appState<CanvasTargets>(page);
  const endHandle = state.mobileSelectionHandles.find((handle) => handle.edge === "end");
  if (!endHandle) throw new Error("Missing end selection handle");
  const dragTo = { x: group.editorRect.x + group.editorRect.w * 0.55, y: group.editorRect.y + group.editorRect.h - 8 };
  await touchDragAndHold(page, center(endHandle.rect), dragTo, 650);
  await expect.poll(async () => (await activeGroupState(page, "/mobile-select.txt")).scrollY).toBeGreaterThan(20);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).selectedText).toContain("line");
});

test("mobile selection handles appear on text input selections", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile text input handles are covered in mobile WebKit.");

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.sidebarMode = "search";
    app.sidebarWidth = 280;
    app.searchBuffer.text = "alpha beta";
    app.searchBuffer.anchor = 0;
    app.searchBuffer.cursor = 5;
    app.searchBuffer.scrollX = 0;
    app.draw();
    app.focusTextField("search", app.getStateForTests().searchInputRect);
    app.draw();
  });
  await expect.poll(async () => {
    const state = await appState<CanvasTargets>(page);
    return state.mobileSelectionHandles.filter((handle) => handle.target === "textField" && handle.path === "search").length;
  }).toBe(2);

  let state = await appState<CanvasTargets>(page);
  const searchEnd = state.mobileSelectionHandles.find((handle) => handle.target === "textField" && handle.path === "search" && handle.edge === "end");
  if (!searchEnd) throw new Error("Missing search end handle");
  await touchDrag(page, center(searchEnd.rect), { x: state.searchInputRect!.x + state.searchInputRect!.w - 12, y: center(searchEnd.rect).y });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).searchSelectedText).toContain("beta");

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.sidebarMode = "chat";
    app.sidebarWidth = 280;
    app.chatDraft.selectAll();
    app.chatDraft.replaceSelection("hello chat world");
    app.chatDraft.markSaved();
    app.chatDraft.setSelection({ line: 0, col: 0 }, { line: 0, col: 5 });
    app.draw();
    app.focusMiniTarget("chat", app.getStateForTests().chatInputRect);
    app.draw();
  });
  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    return current.mobileSelectionHandles.filter((handle) => handle.target === "chatInput").length;
  }).toBe(2);

  await page.evaluate(async () => {
    const app = window.__slugApp! as any;
    await app.vfs.writeFile("/rename-handles.txt", "rename handles", "text/plain");
    await app.refreshFiles();
    app.sidebarMode = "files";
    app.sidebarWidth = 280;
    app.startRename("/rename-handles.txt");
    app.renameBuffer.anchor = 0;
    app.renameBuffer.cursor = 6;
    app.draw();
  });
  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    return current.mobileSelectionHandles.filter((handle) => handle.target === "rename" && handle.path === "/rename-handles.txt").length;
  }).toBe(2);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.sidebarMode = "settings";
    app.sidebarWidth = 280;
    app.draw();
    const fontSizeRect = app.getStateForTests().settingsTargets.find((target: any) => target.type === "settingsNumber" && target.key === "fontSize")?.rect;
    if (!fontSizeRect) throw new Error("Missing font size input");
    app.focusSettingsNumber("fontSize", fontSizeRect);
    app.settingsNumberBuffer.selectAll();
    app.draw();
  });
  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    return current.mobileSelectionHandles.filter((handle) => handle.target === "settingsNumber" && handle.path === "fontSize").length;
  }).toBe(2);
});

test("mobile long press selects settings text inputs", async ({ page, browserName }) => {
  test.skip(browserName !== "webkit", "Mobile settings text long-press behavior is covered in mobile WebKit.");

  await page.evaluate(() => {
    localStorage.setItem("slug.aiEndpointConfig", JSON.stringify({
      apiBaseUrl: "http://alpha.localhost:1234/v1",
      apiKey: "",
      model: "",
      temperature: 0.2,
      maxContextTokens: 8192
    }));
    const app = window.__slugApp! as any;
    app.sidebarMode = "settings";
    app.sidebarWidth = 320;
    app.settingsScrollY = 0;
    app.draw();
  });

  let state = await appState<CanvasTargets>(page);
  let baseUrl = settingTarget(state.settingsTargets, "textField", "aiBaseUrl");
  await page.touchscreen.tap(...pointArgs(center(baseUrl)));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("aiBaseUrl");
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).touchKeyboardStabilizing).toBe(true);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).visualViewportResizeDeferred).toBe(true);
  await page.setViewportSize({ width: 390, height: 520 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("aiBaseUrl");
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);
  await page.waitForTimeout(TOUCH_KEYBOARD_SETTLE_TEST_MS);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).touchKeyboardStabilizing).toBe(false);
  await expect.poll(async () => (await appState<CanvasTargets>(page)).visualViewportResizeDeferred).toBe(false);
  await expect.poll(() => page.evaluate(() => document.activeElement?.classList.contains("input-bridge") ?? false)).toBe(true);

  state = await appState<CanvasTargets>(page);
  baseUrl = settingTarget(state.settingsTargets, "textField", "aiBaseUrl");
  await touchLongPress(page, { x: baseUrl.x + baseUrl.w * 0.45, y: center(baseUrl).y });
  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    return current.mobileSelectionHandles.filter((handle) => handle.target === "textField" && handle.path === "aiBaseUrl").length;
  }).toBe(2);
  state = await appState<CanvasTargets>(page);
  const baseUrlEndHandle = state.mobileSelectionHandles.find((handle) => handle.target === "textField" && handle.path === "aiBaseUrl" && handle.edge === "end");
  if (!baseUrlEndHandle) throw new Error("Missing AI base URL end handle");
  await touchDrag(page, center(baseUrlEndHandle.rect), { x: baseUrl.x + baseUrl.w - 18, y: center(baseUrlEndHandle.rect).y });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activeInputKind).toBe("aiBaseUrl");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsTextSelectedText.length).toBeGreaterThan(0);
  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    return current.mobileSelectionHandles.filter((handle) => handle.target === "textField" && handle.path === "aiBaseUrl").length;
  }).toBe(2);

  state = await appState<CanvasTargets>(page);
  const fontSize = settingTarget(state.settingsTargets, "settingsNumber", "fontSize");
  await touchLongPress(page, center(fontSize));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).settingsNumberSelectedText).toBe("14");
  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    return current.mobileSelectionHandles.filter((handle) => handle.target === "settingsNumber" && handle.path === "fontSize").length;
  }).toBe(2);
});

test("editor hides unused scrollbars and grows the line number gutter", async ({ page }) => {
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    app.sidebarWidth = 0;
    await app.vfs.writeFile("/fits.txt", "one\ntwo\nthree", "text/plain");
    await app.refreshFiles();
    await app.openFile("/fits.txt");
    (app as any).draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/fits.txt");
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorScrollbars.filter((bar) => bar.path === "/fits.txt")).toEqual([]);

  const small = await activeGroupState(page, "/fits.txt");
  const expectedThreeDigitGutter = await page.evaluate(() => Math.ceil(window.__slugApp!.renderer.measureText("999", "code") + 22));
  expect(small.gutterWidth).toBe(expectedThreeDigitGutter);

  await page.evaluate(async () => {
    const app = window.__slugApp!;
    const text = Array.from({ length: 1000 }, (_, index) => `line ${String(index + 1).padStart(4, "0")}`).join("\n");
    await app.vfs.writeFile("/thousand.txt", text, "text/plain");
    await app.refreshFiles();
    await app.openFile("/thousand.txt");
    (app as any).draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/thousand.txt");
  const large = await activeGroupState(page, "/thousand.txt");
  expect(large.gutterWidth).toBeGreaterThan(small.gutterWidth);
});

test("editor clips horizontally scrolled text out of the line number gutter", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Pixel-level gutter clipping is covered in desktop Chromium.");
  await page.setViewportSize({ width: 420, height: 260 });
  await page.evaluate(async () => {
    const app = window.__slugApp! as any;
    app.sidebarWidth = 0;
    await app.vfs.writeFile("/wide-gutter.txt", `${"M".repeat(240)}\nshort`, "text/plain");
    await app.refreshFiles();
    await app.openFile("/wide-gutter.txt");
    const doc = app.activeDoc();
    app.scrollForDoc(doc.id).x = 46;
    app.draw();
  });
  await expect.poll(async () => (await activeGroupState(page, "/wide-gutter.txt")).scrollX).toBeGreaterThan(0);

  const group = await activeGroupState(page, "/wide-gutter.txt");
  const lineH = await page.evaluate(() => window.__slugApp!.renderer.lineHeight("code"));
  const maxGutterChannel = await page.evaluate(({ rect, gutterWidth, lineH }) => {
    (window as any).__slugApp?.draw?.();
    const canvas = document.querySelector("#editor-canvas") as HTMLCanvasElement;
    const gl = canvas.getContext("webgl2")!;
    const canvasRect = canvas.getBoundingClientRect();
    const dprX = canvas.width / canvasRect.width;
    const dprY = canvas.height / canvasRect.height;
    const x0 = Math.floor((rect.x + 4) * dprX);
    const x1 = Math.floor((rect.x + Math.min(gutterWidth * 0.45, Math.max(6, gutterWidth - 22))) * dprX);
    const yTop = rect.y + 5;
    const yBottom = rect.y + Math.min(lineH - 2, 16);
    const y0 = Math.floor(canvas.height - yBottom * dprY);
    const w = Math.max(1, x1 - x0);
    const h = Math.max(1, Math.ceil((yBottom - yTop) * dprY));
    const pixels = new Uint8Array(w * h * 4);
    gl.readPixels(x0, y0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    let max = 0;
    for (let i = 0; i < pixels.length; i += 4) {
      max = Math.max(max, pixels[i]!, pixels[i + 1]!, pixels[i + 2]!);
    }
    return max;
  }, { rect: group.editorRect, gutterWidth: group.gutterWidth, lineH });
  expect(maxGutterChannel).toBeLessThan(90);
});

test("editor renders optional whitespace markers for spaces, tabs, and line breaks", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas pixel whitespace rendering is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp! as any;
    app.sidebarWidth = 0;
    app.settings = { ...app.settings, showWhitespace: true, tabSpaces: 4, useTabStops: true };
    app.saveAndApplySettings();
    await app.vfs.writeFile("/whitespace.txt", "a b\tc\nnext", "text/plain");
    await app.refreshFiles();
    await app.openFile("/whitespace.txt");
    app.activeDoc().setSelection({ line: 1, col: 0 });
    app.draw();
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe("/whitespace.txt");

  const markerRects = await page.evaluate(() => {
    const app = window.__slugApp! as any;
    const state = app.getStateForTests() as CanvasTargets;
    const group = state.editorGroups.find((item) => item.activePath === "/whitespace.txt")!;
    const line = app.activeDoc().lines[0] as string;
    const lineH = app.renderer.lineHeight("code");
    const textX = group.editorRect.x + group.gutterWidth + 8 - group.scrollX;
    const y = group.editorRect.y - (group.scrollY % lineH);
    const spaceStart = app.measureCodePrefix(line, 1);
    const spaceW = app.renderer.measureText(" ", "code");
    const tabStart = app.measureCodePrefix(line, 3);
    const tabW = app.codeAdvanceForChar("\t", tabStart);
    const end = app.measureCodePrefix(line, line.length);
    const newlineW = app.renderer.measureText("\\n", "code");
    return {
      space: { x: textX + spaceStart + spaceW * 0.5 - 1, y: y + lineH * 0.66 - 1, w: 2, h: 2 },
      tab: { x: textX + tabStart + tabW * 0.5 - 5, y: y + lineH * 0.56 - 3, w: 10, h: 6 },
      newline: { x: textX + end, y: y + 4, w: newlineW, h: Math.max(4, lineH - 5) }
    };
  });

  const enabled = {
    space: await canvasMaxChannel(page, markerRects.space),
    tab: await canvasMaxChannel(page, markerRects.tab),
    newline: await canvasMaxChannel(page, markerRects.newline)
  };
  expect(enabled.space).toBeGreaterThan(65);
  expect(enabled.tab).toBeGreaterThan(65);
  expect(enabled.newline).toBeGreaterThan(65);

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settings = { ...app.settings, showWhitespace: false };
    app.saveAndApplySettings();
    app.draw();
  });
  const disabled = {
    space: await canvasMaxChannel(page, markerRects.space),
    tab: await canvasMaxChannel(page, markerRects.tab),
    newline: await canvasMaxChannel(page, markerRects.newline)
  };
  expect(enabled.space).toBeGreaterThan(disabled.space + 20);
  expect(enabled.tab).toBeGreaterThan(disabled.tab + 20);
  expect(enabled.newline).toBeGreaterThan(disabled.newline + 20);
});

test("dock overlay exposes five targets and supports vertical splits", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
    await app.openFile("/notes/shortcuts.txt");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(3);

  let state = await appState<CanvasTargets>(page);
  const noteTab = targetFor(state.tabTargets, "/notes/shortcuts.txt");
  const group = state.editorGroups[0]!;
  const from = center(noteTab);
  const to = { x: group.frameRect.x + group.frameRect.w / 2, y: group.frameRect.y + group.frameRect.h - 24 };
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move(to.x, to.y, { steps: 8 });

  await expect.poll(async () => (await appState<CanvasTargets>(page)).dockPreview?.zone ?? null).toBe("bottom");
  state = await appState<CanvasTargets>(page);
  expect(new Set(state.dockOverlayTargets.map((target) => target.zone))).toEqual(new Set(["center", "left", "right", "top", "bottom"]));
  const centerTarget = state.dockOverlayTargets.find((target) => target.zone === "center")!;
  const topTarget = state.dockOverlayTargets.find((target) => target.zone === "top")!;
  const leftTarget = state.dockOverlayTargets.find((target) => target.zone === "left")!;
  expect(centerTarget.previewRect.x).toBeCloseTo(group.editorRect.x + group.editorRect.w * 0.33, 1);
  expect(centerTarget.previewRect.y).toBeCloseTo(group.editorRect.y + group.editorRect.h * 0.33, 1);
  expect(centerTarget.previewRect.w).toBeCloseTo(group.editorRect.w * 0.34, 1);
  expect(centerTarget.previewRect.h).toBeCloseTo(group.editorRect.h * 0.34, 1);
  expect(topTarget.previewRect.h).toBeCloseTo(group.editorRect.h * 0.33, 1);
  expect(leftTarget.previewRect.w).toBeCloseTo(group.editorRect.w * 0.33, 1);

  await page.mouse.up();
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(2);
  state = await appState<CanvasTargets>(page);
  const notesGroup = state.editorGroups.find((item) => item.tabs.includes("/notes/shortcuts.txt"));
  const otherGroup = state.editorGroups.find((item) => !item.tabs.includes("/notes/shortcuts.txt"));
  expect(notesGroup?.frameRect.y).toBeGreaterThan(otherGroup?.frameRect.y ?? Number.POSITIVE_INFINITY);
});

test("center dock target adds a tab to the hovered dock slot", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
    await app.openFile("/notes/shortcuts.txt");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(3);

  let state = await appState<CanvasTargets>(page);
  const noteTab = targetFor(state.tabTargets, "/notes/shortcuts.txt");
  let rootGroup = state.editorGroups[0]!;
  await drag(page, center(noteTab), { x: rootGroup.frameRect.x + rootGroup.frameRect.w - 24, y: rootGroup.frameRect.y + rootGroup.frameRect.h / 2 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(2);

  state = await appState<CanvasTargets>(page);
  const mainTab = targetFor(state.tabTargets, "/src/main.ts");
  const notesGroup = state.editorGroups.find((group) => group.tabs.includes("/notes/shortcuts.txt"))!;
  const target = center(notesGroup.frameRect);
  await page.mouse.move(...pointArgs(center(mainTab)));
  await page.mouse.down();
  await page.mouse.move(target.x, target.y, { steps: 8 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).dockPreview?.zone ?? null).toBe("center");
  await page.mouse.up();

  state = await appState<CanvasTargets>(page);
  rootGroup = state.editorGroups.find((group) => group.tabs.includes("/notes/shortcuts.txt"))!;
  expect(rootGroup.tabs).toEqual(expect.arrayContaining(["/src/main.ts", "/notes/shortcuts.txt"]));
});

test("center dock target can be re-entered repeatedly while dragging", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(2);
  const state = await appState<CanvasTargets>(page);
  const mainTab = targetFor(state.tabTargets, "/src/main.ts");
  const group = state.editorGroups[0]!;
  const centerPoint = center(group.frameRect);
  const rightPoint = { x: group.frameRect.x + group.frameRect.w - 28, y: centerPoint.y };

  await page.mouse.move(...pointArgs(center(mainTab)));
  await page.mouse.down();
  await page.mouse.move(centerPoint.x, centerPoint.y, { steps: 6 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).dockPreview?.zone ?? null).toBe("center");
  await page.mouse.move(rightPoint.x, rightPoint.y, { steps: 6 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).dockPreview?.zone ?? null).toBe("right");
  await page.mouse.move(centerPoint.x, centerPoint.y, { steps: 6 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).dockPreview?.zone ?? null).toBe("center");
  await page.mouse.up();
});

test("dragging the only tab in a dock slot removes that slot during the drag", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    const app = window.__slugApp!;
    await app.openFile("/README.md");
    await app.openFile("/src/main.ts");
    await app.openFile("/notes/shortcuts.txt");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(3);

  let state = await appState<CanvasTargets>(page);
  let noteTab = targetFor(state.tabTargets, "/notes/shortcuts.txt");
  let group = state.editorGroups[0]!;
  await drag(page, center(noteTab), { x: group.frameRect.x + group.frameRect.w / 2, y: group.frameRect.y + group.frameRect.h - 24 });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).editorGroups.length).toBe(2);

  state = await appState<CanvasTargets>(page);
  const bottomGroup = state.editorGroups.find((item) => item.tabs.includes("/notes/shortcuts.txt"))!;
  noteTab = targetFor(state.tabTargets, "/notes/shortcuts.txt");
  const noteCenter = center(noteTab);
  await page.mouse.move(...pointArgs(noteCenter));
  await page.mouse.down();
  await page.mouse.move(noteCenter.x + 16, noteCenter.y + 16, { steps: 3 });

  await expect.poll(async () => {
    const current = await appState<CanvasTargets>(page);
    return current.editorGroups.some((item) => item.tabs.includes("/notes/shortcuts.txt"));
  }).toBe(false);
  state = await appState<CanvasTargets>(page);
  expect(state.openTabs).not.toContain("/notes/shortcuts.txt");
  expect(state.dockOverlayTargets.every((target) => target.groupId !== bottomGroup.id)).toBe(true);
  await page.mouse.up();
});

test("dragging a tab shows a tab ghost under the pointer", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Canvas drag geometry is covered in desktop Chromium.");
  await page.evaluate(async () => {
    await window.__slugApp!.openFile("/README.md");
    await window.__slugApp!.openFile("/src/main.ts");
  });
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.length).toBe(2);

  const state = await appState<CanvasTargets>(page);
  const mainTab = targetFor(state.tabTargets, "/src/main.ts");
  const pointer = { x: mainTab.x + 180, y: mainTab.y + 92 };
  await page.mouse.move(...pointArgs(center(mainTab)));
  await expect.poll(async () => canvasCursor(page)).toBe("");
  await page.mouse.down();
  await page.mouse.move(pointer.x, pointer.y, { steps: 6 });
  await expect.poll(async () => canvasCursor(page)).toBe("");

  await expect.poll(async () => (await appState<CanvasTargets>(page)).dragGhost).not.toBeNull();
  const current = await appState<CanvasTargets>(page);
  expect(current.dragGhost!.x).toBeCloseTo(pointer.x - 18, 0);
  expect(current.dragGhost!.y).toBeCloseTo(pointer.y - 16, 0);
  expect(current.dragGhost!.w).toBeGreaterThan(120);
  await page.mouse.up();
});

function testDatabaseName(testInfo: import("@playwright/test").TestInfo): string {
  return `${testInfo.project.name}-${testInfo.workerIndex}-${testInfo.titlePath.join("-")}`.replace(/[^a-zA-Z0-9_-]/g, "-").slice(0, 80);
}

type CanvasRect = { x: number; y: number; w: number; h: number };
type CanvasTarget = { path: string; rect: CanvasRect };
type FolderCanvasTarget = CanvasTarget & { expanded: boolean };
type SettingsTarget = { type: string; key: string; rect: CanvasRect; enabled: boolean };
type ContextCommand = "rename" | "duplicate" | "delete" | "cut" | "copy" | "paste" | "systemCopy" | "systemPaste" | "undo" | "redo" | "createFile" | "createFolder" | "uploadFile" | "save" | "findInFile" | "close" | "closeOthers" | "newFile" | "closeAll" | "resetSettings" | "toggleLineNumbers" | "exportChat" | "debugChat" | "clearChat" | "compactChat" | "copyBubble" | "copyChat" | "systemCopyBubble" | "systemCopyChat" | "themeDark" | "themeLight" | "aiProviderLocal" | "aiProviderOpenAI" | "aiToolFormatNone" | "aiToolFormatTag" | "aiToolFormatHarmony" | `aiModel:${string}` | `highlight:${string}`;
type ModalAction = "save" | "discard" | "cancel" | "delete" | "download" | "replace" | "append" | "clearChat" | "allowMore" | "allowAll" | "stopToolCalls" | "allowDuplicateTool" | "breakDuplicateTool";
type CanvasTargets = {
  activePath: string | undefined;
  activeText: string | undefined;
  activeTab: string | undefined;
  activeSyntaxId: string | undefined;
  activeInputKind: string | null;
  selectedText: string;
  openTabs: string[];
  sidebarMode: string;
  sidebarWidth: number;
  sidebarVisible: boolean;
  statusText: string;
  selectedFileTreePath: string | null;
  hoveredFileTreePath: string | null;
  fileDragActive: boolean;
  fileDragLabel: string;
  filesScrollY: number;
  searchScrollY: number;
  settings: { theme: "dark" | "light"; fontSize: number; uiScale: number; monospacedFont: boolean; tabSpaces: number; useTabStops: boolean; showWhitespace: boolean; showThinking: boolean; renameOnDoubleClick: boolean; showLineNumbers: boolean; rememberOpenFiles: boolean; aiProvider: "local" | "openai"; aiModelManual: boolean; aiMaxToolCalls: number; aiDetectDuplicateToolCalls: boolean; aiToolCallFormat: "tag" | "harmony" | "none"; aiCompactFreePercent: number; aiInsertEditorContext: boolean };
  settingsActivityTarget: CanvasRect | null;
  downloadActivityTarget: CanvasRect | null;
  settingsNumberText: string;
  settingsNumberSelectedText: string;
  settingsTextSelectedText: string;
  activeSettingsNumber: string | null;
  settingsScrollY: number;
  settingsTargets: SettingsTarget[];
  settingsRootTarget: CanvasRect | null;
  searchQuery: string;
  searchScrollX: number;
  projectReplaceText: string;
  searchReplaceExpanded: boolean;
  searchSelectedText: string;
  searchInputRect: CanvasRect | null;
  projectReplaceInputRect: CanvasRect | null;
  searchTargets: SettingsTarget[];
  searchCaretVisible: boolean;
  searchResults: Array<{ path: string; line: number; text: string }>;
  searchResultTargets: Array<{ path: string; line: number; rect: CanvasRect }>;
  findOpen: boolean;
  findReplaceExpanded: boolean;
  findQuery: string;
  findReplaceText: string;
  findSelectedText: string;
  findReplaceSelectedText: string;
  findTargets: SettingsTarget[];
  chatMessages: Array<{ role: string; text: string; name?: string; ok?: boolean }>;
  chatDisplayedMessages: Array<{ role: string; text: string; name?: string; ok?: boolean }>;
  chatTokenUsage: { calibrated: boolean; dirty: boolean; basePromptTokens: number; promptTokens: number; lastPromptTokens: number; lastCompletionTokens: number; lastTotalTokens: number; source: string };
  chatRootTarget: CanvasRect | null;
  chatBubbleTargets: Array<{ id: string; role: string; text: string; rect: CanvasRect }>;
  chatDraft: string;
  chatScrollY: number;
  chatInputScrollY: number;
  chatInputRect: CanvasRect | null;
  chatRunning: boolean;
  chatSendTarget: ({ rect: CanvasRect; enabled: boolean; label: string } & Record<string, unknown>) | null;
  chatShowThinking: boolean;
  chatShowThinkingTarget: CanvasRect | null;
  chatScrollbars: Array<{ panel: "chatTranscript" | "chatInput"; rect: CanvasRect; trackRect: CanvasRect; thumbRect: CanvasRect }>;
  touchKeyboardStabilizing: boolean;
  visualViewportResizeDeferred: boolean;
  aiEndpointConfig: { apiBaseUrl: string; apiKey: string; model: string; temperature: number; maxContextTokens: number };
  aiModels: Array<{ id: string; contextLength: number }>;
  aiConnectionStatus: { state: "idle" | "checking" | "ok" | "error"; message: string; baseUrl?: string; checkedAt?: number };
  aiEndpointFieldState: "ok" | "error" | null;
  fileTargets: CanvasTarget[];
  folderTargets: FolderCanvasTarget[];
  filePanelEmptyHint: string | null;
  filesRootTarget: CanvasRect | null;
  renamePath: string | null;
  renameText: string;
  renameSelectedText: string;
  renameScrollX: number;
  renameInputRect: CanvasRect | null;
  renameInvalid: boolean;
  renameInvalidCharacters: Array<{ start: number; end: number; text: string }>;
  caretBlinkOn: boolean;
  renameCaretVisible: boolean;
  contextMenu: null | {
    scope: { type: "file"; path: string } | { type: "folder"; path: string } | { type: "root"; path: "/" } | { type: "editor"; groupId: string; docId: string } | { type: "gutter"; groupId: string; docId: string } | { type: "tab"; groupId: string; docId: string } | { type: "tabBar"; groupId: string } | { type: "tabOverflow"; groupId: string } | { type: "highlightDropdown"; groupId: string; docId: string } | { type: "settingsRoot" } | { type: "chatRoot" } | { type: "chatBubble"; messageId: string } | { type: "settingsDropdown"; key: string } | { type: "settingsNumber"; key: string } | { type: "textField"; field: string } | { type: "chatInput" } | { type: "rename"; path: string } | { type: "search" };
    rect: CanvasRect;
    items: Array<{ command: ContextCommand | string; label: string; rect: CanvasRect; enabled: boolean }>;
  };
  modal: null | {
    kind: "dirtyClose" | "dirtyDownload" | "deleteFolder" | "clearFileSystem" | "clearChat" | "zipImport" | "zipProgress" | "compactProgress" | "downloadReady" | "toolCallLimit" | "duplicateToolCall";
    title: string;
    message: string;
    detail: string;
    progress: number | null;
    pending: boolean;
    buttons: Array<{ action: ModalAction; label: string; rect: CanvasRect; enabled: boolean }>;
  };
  tabTargets: CanvasTarget[];
  tabCloseTargets: CanvasTarget[];
  tabOverflowTargets: Array<{ groupId: string; rect: CanvasRect }>;
  editorGutterTargets: Array<{ groupId: string; path: string; rect: CanvasRect }>;
  tabBarTargets: Array<{ groupId: string; rect: CanvasRect }>;
  editorGroups: Array<{ id: string; activePath: string | null; tabs: string[]; cursor: { line: number; col: number } | null; caretVisible: boolean; scrollX: number; scrollY: number; gutterWidth: number; frameRect: CanvasRect; editorRect: CanvasRect }>;
  visibleCarets: Array<{ groupId: string; path: string; cursor: { line: number; col: number }; rect: CanvasRect }>;
  mobileSelectionHandles: Array<{ edge: "start" | "end"; groupId: string; path: string; target: string; rect: CanvasRect }>;
  dockPreview: { groupId: string; zone: DockZone; rect: CanvasRect } | null;
  tabInsertionPreview: { groupId: string; index: number; rect: CanvasRect } | null;
  dragGhost: CanvasRect | null;
  dockOverlayTargets: Array<{ groupId: string; zone: DockZone; polygon: Array<{ x: number; y: number }>; previewRect: CanvasRect }>;
  statusWhitespaceTarget: CanvasRect | null;
  statusHighlightTarget: CanvasRect | null;
  sidebarResizeTarget: CanvasRect | null;
  dockSplitters: Array<{ splitId: string; index: number; direction: "row" | "column"; rect: CanvasRect }>;
  editorScrollbars: Array<{ axis: "vertical" | "horizontal"; groupId: string; path: string; rect: CanvasRect; trackRect: CanvasRect; thumbRect: CanvasRect }>;
  settingsScrollbar: null | { rect: CanvasRect; trackRect: CanvasRect; thumbRect: CanvasRect; viewportRect: CanvasRect };
  sidebarScrollbars: Array<{ panel: "files" | "search" | "settings"; rect: CanvasRect; trackRect: CanvasRect; thumbRect: CanvasRect }>;
  hoveredScrollbar: { axis: "vertical" | "horizontal"; groupId: string; path: string; overThumb: boolean } | null;
  renderer: RendererDiagnostics;
  canvas: { width: number; height: number; cssWidth: number; cssHeight: number };
};

type DockZone = "center" | "left" | "right" | "top" | "bottom";
type RendererDiagnostics = {
  backend: string;
  font: string;
  unitsPerEm: number;
  glyphCount: number;
  fonts: Array<{ name: string; unitsPerEm: number; glyphCount: number }>;
};

function targetFor(targets: CanvasTarget[], path: string): CanvasRect {
  const target = targets.find((item) => item.path === path);
  if (!target) throw new Error(`Missing canvas target: ${path}`);
  return target.rect;
}

function settingTarget(targets: SettingsTarget[], type: string, key: string): CanvasRect {
  const target = targets.find((item) => item.type === type && item.key === key);
  if (!target) throw new Error(`Missing settings target: ${type}:${key}`);
  return target.rect;
}

function center(rect: CanvasRect): { x: number; y: number } {
  return { x: rect.x + rect.w / 2, y: rect.y + rect.h / 2 };
}

function pointArgs(point: { x: number; y: number }): [number, number] {
  return [point.x, point.y];
}

function groupsByX(groups: CanvasTargets["editorGroups"]): CanvasTargets["editorGroups"] {
  return [...groups].sort((a, b) => a.frameRect.x - b.frameRect.x);
}

function groupsByY(groups: CanvasTargets["editorGroups"]): CanvasTargets["editorGroups"] {
  return [...groups].sort((a, b) => a.frameRect.y - b.frameRect.y);
}

async function activeGroupState(page: import("@playwright/test").Page, path: string): Promise<CanvasTargets["editorGroups"][number]> {
  const state = await appState<CanvasTargets>(page);
  const group = state.editorGroups.find((item) => item.activePath === path);
  if (!group) throw new Error(`Missing active group: ${path}`);
  return group;
}

async function canvasCursor(page: import("@playwright/test").Page): Promise<string> {
  return page.locator("#editor-canvas").evaluate((node) => (node as HTMLElement).style.cursor);
}

async function canvasPixel(page: import("@playwright/test").Page, cssX: number, cssY: number): Promise<number[]> {
  return page.evaluate(({ cssX, cssY }) => {
    (window as any).__slugApp?.draw?.();
    const canvas = document.querySelector("#editor-canvas") as HTMLCanvasElement;
    const gl = canvas.getContext("webgl2")!;
    const rect = canvas.getBoundingClientRect();
    const dprX = canvas.width / rect.width;
    const dprY = canvas.height / rect.height;
    const x = Math.floor(cssX * dprX);
    const y = Math.floor(canvas.height - cssY * dprY - 1);
    const pixel = new Uint8Array(4);
    gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
    return Array.from(pixel);
  }, { cssX, cssY });
}

async function canvasMaxChannel(page: import("@playwright/test").Page, cssRect: CanvasRect): Promise<number> {
  return page.evaluate(({ cssRect }) => {
    (window as any).__slugApp?.draw?.();
    const canvas = document.querySelector("#editor-canvas") as HTMLCanvasElement;
    const gl = canvas.getContext("webgl2")!;
    const rect = canvas.getBoundingClientRect();
    const dprX = canvas.width / rect.width;
    const dprY = canvas.height / rect.height;
    const x = Math.max(0, Math.floor(cssRect.x * dprX));
    const w = Math.max(1, Math.min(canvas.width - x, Math.ceil(cssRect.w * dprX)));
    const yBottom = Math.max(0, Math.floor(canvas.height - (cssRect.y + cssRect.h) * dprY));
    const h = Math.max(1, Math.min(canvas.height - yBottom, Math.ceil(cssRect.h * dprY)));
    const pixels = new Uint8Array(w * h * 4);
    gl.readPixels(x, yBottom, w, h, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    let max = 0;
    for (let i = 0; i < pixels.length; i += 4) max = Math.max(max, pixels[i]!, pixels[i + 1]!, pixels[i + 2]!);
    return max;
  }, { cssRect });
}

type DragFileInput = { name: string; type?: string; text?: string; bytes?: number[] };

async function zipBytes(entries: Record<string, string>): Promise<number[]> {
  const zip = new JSZip();
  for (const [path, text] of Object.entries(entries)) zip.file(path, text);
  return Array.from(await zip.generateAsync({ type: "uint8array" }));
}

async function dispatchCanvasDrag(page: import("@playwright/test").Page, type: "dragenter" | "dragover" | "dragleave" | "drop", files: DragFileInput[], point: { x: number; y: number } = { x: 64, y: 64 }): Promise<void> {
  await page.evaluate(({ type, files, point }) => {
    const canvas = document.querySelector("#editor-canvas") as HTMLCanvasElement;
    const dt = new DataTransfer();
    for (const file of files) {
      const body = file.bytes ? [new Uint8Array(file.bytes)] : [file.text ?? ""];
      dt.items.add(new File(body, file.name, { type: file.type ?? "text/plain" }));
    }
    canvas.dispatchEvent(new DragEvent(type, {
      bubbles: true,
      cancelable: true,
      clientX: point.x,
      clientY: point.y,
      dataTransfer: dt
    }));
    (window.__slugApp as any)?.draw?.();
  }, { type, files, point });
}

async function activateTab(page: import("@playwright/test").Page, path: string): Promise<void> {
  const state = await appState<CanvasTargets>(page);
  await page.mouse.click(...pointArgs(center(targetFor(state.tabTargets, path))));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).activePath).toBe(path);
}

async function openReadme(page: import("@playwright/test").Page): Promise<void> {
  await page.evaluate(() => window.__slugApp!.openFile("/README.md"));
  await expect.poll(async () => (await appState<CanvasTargets>(page)).tabTargets.map((item) => item.path)).toContain("/README.md");
}

async function placeCursorInDocument(page: import("@playwright/test").Page, path: string, line: number): Promise<void> {
  await activateTab(page, path);
  const state = await appState<CanvasTargets>(page);
  const group = state.editorGroups.find((item) => item.activePath === path);
  if (!group) throw new Error(`Missing active editor for ${path}`);
  const lineH = await page.evaluate(() => window.__slugApp!.renderer.lineHeight("code"));
  await page.mouse.click(group.editorRect.x + group.gutterWidth + 16, group.editorRect.y + line * lineH + lineH / 2);
  await expectVisibleCaret(page, path, line);
}

async function expectVisibleCaret(page: import("@playwright/test").Page, path: string, line: number): Promise<void> {
  await expect.poll(async () => {
    const state = await appState<CanvasTargets>(page);
    return state.visibleCarets.map((caret) => ({ path: caret.path, line: caret.cursor.line }));
  }).toEqual([{ path, line }]);
}

async function waitForContextMenu(page: import("@playwright/test").Page): Promise<CanvasTargets> {
  await expect.poll(async () => Boolean((await appState<CanvasTargets>(page)).contextMenu)).toBe(true);
  return appState<CanvasTargets>(page);
}

async function waitForModal(page: import("@playwright/test").Page): Promise<CanvasTargets> {
  await expect.poll(async () => {
    const modal = (await appState<CanvasTargets>(page)).modal;
    return Boolean(modal && modal.buttons.every((button) => button.rect.w > 0 && button.rect.h > 0));
  }).toBe(true);
  return appState<CanvasTargets>(page);
}

function menuItem(state: CanvasTargets, command: ContextCommand): { command: string; label: string; rect: CanvasRect; enabled: boolean } {
  const item = state.contextMenu?.items.find((candidate) => candidate.command === command);
  if (!item) throw new Error(`Missing menu item: ${command}`);
  return item;
}

async function clickMenuItem(page: import("@playwright/test").Page, state: CanvasTargets, command: ContextCommand): Promise<void> {
  const item = menuItem(state, command);
  await page.mouse.click(...pointArgs(center(item.rect)));
}

async function clickModalButton(page: import("@playwright/test").Page, state: CanvasTargets, action: ModalAction): Promise<void> {
  const item = state.modal?.buttons.find((candidate) => candidate.action === action);
  if (!item) throw new Error(`Missing modal button: ${action}`);
  await page.mouse.click(...pointArgs(center(item.rect)));
}

async function drag(page: import("@playwright/test").Page, from: { x: number; y: number }, to: { x: number; y: number }): Promise<void> {
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move(to.x, to.y, { steps: 6 });
  await page.mouse.up();
}

async function touchDrag(page: import("@playwright/test").Page, from: { x: number; y: number }, to: { x: number; y: number }): Promise<void> {
  await page.evaluate(async ({ from, to }) => {
    const canvas = document.querySelector("#editor-canvas") as HTMLCanvasElement;
    const pointerId = 9101;
    const dispatch = (target: EventTarget, type: string, point: { x: number; y: number }, buttons: number) => {
      target.dispatchEvent(new PointerEvent(type, {
        bubbles: true,
        cancelable: true,
        clientX: point.x,
        clientY: point.y,
        pointerId,
        pointerType: "touch",
        isPrimary: true,
        button: 0,
        buttons
      }));
    };
    dispatch(canvas, "pointerdown", from, 1);
    for (let i = 1; i <= 8; i++) {
      const t = i / 8;
      dispatch(canvas, "pointermove", { x: from.x + (to.x - from.x) * t, y: from.y + (to.y - from.y) * t }, 1);
    }
    dispatch(window, "pointerup", to, 0);
    await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));
  }, { from, to });
}

async function touchLongPress(page: import("@playwright/test").Page, point: { x: number; y: number }, holdMs = 650): Promise<void> {
  await page.evaluate(async ({ point, holdMs }) => {
    const canvas = document.querySelector("#editor-canvas") as HTMLCanvasElement;
    const pointerId = 9201;
    canvas.dispatchEvent(new PointerEvent("pointerdown", {
      bubbles: true,
      cancelable: true,
      clientX: point.x,
      clientY: point.y,
      pointerId,
      pointerType: "touch",
      isPrimary: true,
      button: 0,
      buttons: 1
    }));
    await new Promise<void>((resolve) => setTimeout(resolve, holdMs));
    window.dispatchEvent(new PointerEvent("pointerup", {
      bubbles: true,
      cancelable: true,
      clientX: point.x,
      clientY: point.y,
      pointerId,
      pointerType: "touch",
      isPrimary: true,
      button: 0,
      buttons: 0
    }));
    await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));
  }, { point, holdMs });
}

async function touchDragAndHold(page: import("@playwright/test").Page, from: { x: number; y: number }, to: { x: number; y: number }, holdMs: number): Promise<void> {
  await page.evaluate(async ({ from, to, holdMs }) => {
    const canvas = document.querySelector("#editor-canvas") as HTMLCanvasElement;
    const pointerId = 9301;
    const dispatch = (target: EventTarget, type: string, point: { x: number; y: number }, buttons: number) => {
      target.dispatchEvent(new PointerEvent(type, {
        bubbles: true,
        cancelable: true,
        clientX: point.x,
        clientY: point.y,
        pointerId,
        pointerType: "touch",
        isPrimary: true,
        button: 0,
        buttons
      }));
    };
    dispatch(canvas, "pointerdown", from, 1);
    for (let i = 1; i <= 8; i++) {
      const t = i / 8;
      dispatch(canvas, "pointermove", { x: from.x + (to.x - from.x) * t, y: from.y + (to.y - from.y) * t }, 1);
    }
    await new Promise<void>((resolve) => setTimeout(resolve, holdMs));
    dispatch(window, "pointerup", to, 0);
    await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));
  }, { from, to, holdMs });
}

function desktopShortcutModifier(): "Control" | "Meta" {
  return process.platform === "darwin" ? "Meta" : "Control";
}

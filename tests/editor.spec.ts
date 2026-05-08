import { expect, test } from "@playwright/test";

const SAMPLE_WORKSPACE_FILES: Array<{ path: string; text: string; mime: string }> = [
  { path: "/README.md", text: "# Carrot Editor\n\nThis workspace is stored in IndexedDB.\n\n- Open files from the left sidebar.\n- Edit text in the WebGL2 editor.\n- Use Search to scan files.\n- Use Chat for local assistant turns.\n", mime: "text/plain" },
  { path: "/src/main.ts", text: "export function greet(name: string): string {\n  return `hello ${name}`;\n}\n\nconsole.log(greet('Carrot'));\n", mime: "text/plain" },
  { path: "/notes/shortcuts.txt", text: "Ctrl/Cmd+C copy\nCtrl/Cmd+X cut\nCtrl/Cmd+V paste\nCtrl/Cmd+S save\nCtrl/Cmd+Shift+F project search\n", mime: "text/plain" }
];

test.beforeEach(async ({ page, context, browserName }) => {
  if (browserName === "chromium") {
    await context.grantPermissions(["clipboard-read", "clipboard-write"], { origin: "http://127.0.0.1:4173" });
  }
});

test.beforeEach(async ({ page }, testInfo) => {
  const dbName = testDatabaseName(testInfo);
  await page.goto("/");
  await page.evaluate(async (db) => {
    localStorage.removeItem("slug.settings");
    localStorage.removeItem("slug.aiProvider");
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
  await page.evaluate(async (files) => {
    const app = window.__slugApp! as any;
    for (const file of files) await app.vfs.writeFile(file.path, file.text, file.mime);
    await app.refreshFiles();
    app.draw();
  }, SAMPLE_WORKSPACE_FILES);
  await page.evaluate(() => window.__slugApp!.openFile("/README.md"));
});

test("edits WebGL-rendered document through hidden input bridge", async ({ page, browserName }) => {
  await clickEditor(page);
  await page.keyboard.press("Control+A");
  await page.keyboard.type("const answer = 42;\nconsole.log(answer);\n");

  let state = await page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string });
  expect(state.activeText).toContain("const answer = 42;");

  await page.keyboard.press("Control+A");
  if (browserName !== "chromium") {
    await page.keyboard.type("mobile replacement path\n");
    state = await page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string });
    expect(state.activeText).toContain("mobile replacement path");
    return;
  }

  const nativeMod = desktopShortcutModifier();
  await page.keyboard.press(`${nativeMod}+C`);
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toContain("const answer");

  await page.keyboard.press(`${nativeMod}+X`);
  state = await page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string });
  expect(state.activeText.trim()).toBe("");

  await page.keyboard.press(`${nativeMod}+V`);
  await expect.poll(async () => {
    const current = await page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string });
    return current.activeText;
  }).toContain("console.log");
});

test("save shortcut writes edited text back to the virtual filesystem", async ({ page, browserName }) => {
  await clickEditor(page);
  await page.keyboard.press("Control+A");
  await page.keyboard.type("saved through shortcut\n");
  const mod = browserName === "chromium" ? desktopShortcutModifier() : "Control";
  await page.keyboard.press(`${mod}+S`);

  await expect.poll(() => page.evaluate(async () => {
    const app = window.__slugApp!;
    return app.vfs.readText("/README.md");
  })).toContain("saved through shortcut");
});

test("tab inserts indentation instead of moving browser focus", async ({ page }) => {
  await clickEditor(page);
  await page.keyboard.press("Control+A");
  await page.keyboard.type("alpha");
  await page.keyboard.press("Tab");

  let state = await page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string; activeInputKind: string | null });
  expect(state.activeText).toBe("alpha\t");
  expect(state.activeInputKind).toBe("editor");

  await page.keyboard.press("Shift+Tab");
  state = await page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string; activeInputKind: string | null });
  expect(state.activeInputKind).toBe("editor");

  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settings = { ...app.settings, tabSpaces: 3, useTabStops: false };
    app.saveAndApplySettings();
  });
  await page.keyboard.press("Control+A");
  await page.keyboard.type("alpha");
  await page.keyboard.press("Tab");
  state = await page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string; activeInputKind: string | null });
  expect(state.activeText).toBe("alpha   ");
  expect(state.activeInputKind).toBe("editor");
});

test("literal tabs render as configurable whitespace tab stops", async ({ page }) => {
  await clickEditor(page);
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.settings = { ...app.settings, tabSpaces: 4, useTabStops: true };
    app.saveAndApplySettings();
    const doc = app.activeDoc();
    doc.selectAll();
    doc.replaceSelection("\tX\n    X");
    doc.setSelection({ line: 0, col: 1 });
    app.focusEditor();
  });
  const tabCaretX = await expectVisibleCaretX(page);
  await page.evaluate(() => {
    const app = window.__slugApp! as any;
    app.activeDoc().setSelection({ line: 1, col: 4 });
    app.focusEditor();
  });
  const spacesCaretX = await expectVisibleCaretX(page);
  expect(Math.abs(tabCaretX - spacesCaretX)).toBeLessThan(1.5);
});

test("typing past the viewport keeps the caret visible", async ({ page }) => {
  await clickEditor(page);
  await page.keyboard.press("Control+A");
  const text = Array.from({ length: 80 }, (_, index) => `line ${index + 1}`).join("\n");
  await page.keyboard.type(text);

  await expect.poll(async () => {
    const state = await page.evaluate(() => window.__slugApp!.getStateForTests() as CaretScrollState);
    const group = state.editorGroups[0]!;
    const caret = state.visibleCarets[0]?.rect;
    return {
      scrolled: group.scrollY > 0,
      visible: Boolean(caret && caret.y >= group.editorRect.y && caret.y + caret.h <= group.editorRect.y + group.editorRect.h)
    };
  }).toEqual({ scrolled: true, visible: true });
});

test("hidden input mirrors selections for native clipboard and accepts native paste input", async ({ page }) => {
  await clickEditor(page);
  await page.keyboard.press("Control+A");
  await page.keyboard.type("copy target");
  await page.keyboard.press("Control+A");

  const bridge = await page.evaluate(() => {
    const area = document.querySelector(".input-bridge") as HTMLTextAreaElement;
    return { active: document.activeElement === area, value: area.value, start: area.selectionStart, end: area.selectionEnd };
  });
  expect(bridge).toEqual({ active: true, value: "copy target", start: 0, end: "copy target".length });

  await page.evaluate(() => {
    const area = document.querySelector(".input-bridge") as HTMLTextAreaElement;
    area.dispatchEvent(new InputEvent("beforeinput", { inputType: "insertFromPaste", data: "native\r\npaste", bubbles: true, cancelable: true }));
  });
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "native\npaste" });

  await page.evaluate(() => {
    const area = document.querySelector(".input-bridge") as HTMLTextAreaElement;
    area.value = "\nfallback paste";
    area.setSelectionRange(area.value.length, area.value.length);
    area.dispatchEvent(new InputEvent("input", { inputType: "insertFromPaste", bubbles: true }));
  });
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "native\npastefallback paste" });
});

test("desktop double click selects a word and triple click selects a line", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Desktop mouse multi-click selection is covered in Chromium.");
  await clickEditor(page);
  await page.keyboard.press("Control+A");
  await page.keyboard.type("alpha beta\nsecond line here\nthird");

  let point = await editorTextPoint(page, 0);
  await page.mouse.dblclick(point.x + 2, point.y);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { selectedText: string })).toMatchObject({ selectedText: "alpha" });

  point = await editorTextPoint(page, 1);
  await page.mouse.click(point.x + 2, point.y, { clickCount: 3 });
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { selectedText: string })).toMatchObject({ selectedText: "second line here" });
});

test("right click keeps multiline selection and runs editor context menu commands", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "Clipboard-backed context menu is covered in desktop Chromium.");
  const mod = desktopShortcutModifier();
  const text = "one\ntwo\nthree";
  await clickEditor(page);
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.press("Backspace");
  await page.keyboard.type(text);
  await page.keyboard.press(`${mod}+A`);

  const point = await editorTextPoint(page, 1);
  await page.mouse.click(point.x, point.y, { button: "right" });
  let state = await waitForContextMenu(page);
  expect(state.selectedText).toBe(text);
  expect(menuItem(state, "cut").enabled).toBe(true);
  expect(menuItem(state, "copy").enabled).toBe(true);
  expect(menuItem(state, "paste").enabled).toBe(true);
  expect(menuItem(state, "undo").enabled).toBe(true);

  await clickMenuItem(page, state, "copy");
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe(text);

  await page.mouse.click(point.x, point.y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.selectedText).toBe(text);
  await clickMenuItem(page, state, "cut");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "" });
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe(text);

  await page.mouse.click(point.x, point.y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(menuItem(state, "undo").enabled).toBe(true);
  await clickMenuItem(page, state, "undo");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: text });

  await page.mouse.click(point.x, point.y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(menuItem(state, "redo").enabled).toBe(true);
  await clickMenuItem(page, state, "redo");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "" });

  const emptyPoint = await editorTextPoint(page, 0);
  await page.mouse.click(emptyPoint.x, emptyPoint.y, { button: "right" });
  state = await waitForContextMenu(page);
  expect(state.selectedText).toBe("");
  expect(menuItem(state, "cut").enabled).toBe(false);
  expect(menuItem(state, "copy").enabled).toBe(false);
  expect(menuItem(state, "paste").enabled).toBe(true);
  await clickMenuItem(page, state, "paste");
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: text });
});

test("undo groups nearby text edits and separates pauses", async ({ page, browserName }) => {
  await clickEditor(page);
  const mod = browserName === "chromium" ? desktopShortcutModifier() : "Control";
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.press("Backspace");
  await page.waitForTimeout(350);

  await page.keyboard.type("abc");
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "" });

  await page.keyboard.press(`${mod}+Shift+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "abc" });

  await page.waitForTimeout(350);
  await page.keyboard.type("d");
  await page.waitForTimeout(350);
  await page.keyboard.type("e");
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "abcd" });
});

test("undo splits words on whitespace boundaries", async ({ page, browserName }) => {
  await clickEditor(page);
  const mod = browserName === "chromium" ? desktopShortcutModifier() : "Control";
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.press("Backspace");
  await page.waitForTimeout(350);

  await page.keyboard.type("hello world");
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "hello " });
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "hello" });
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "" });

  await page.keyboard.type("a   b");
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "a   " });
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "a" });
});

test("undo treats tab and newline as command boundaries", async ({ page, browserName }) => {
  await clickEditor(page);
  const mod = browserName === "chromium" ? desktopShortcutModifier() : "Control";
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.press("Backspace");
  await page.waitForTimeout(350);

  await page.keyboard.type("a");
  await page.keyboard.press("Tab");
  await page.keyboard.type("b");
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "a\t" });
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "a" });

  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.press("Backspace");
  await page.waitForTimeout(350);
  await page.keyboard.type("a\nb");
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "a\n" });
  await page.keyboard.press(`${mod}+Z`);
  await expect.poll(() => page.evaluate(() => window.__slugApp!.getStateForTests() as { activeText: string })).toMatchObject({ activeText: "a" });
});

function testDatabaseName(testInfo: import("@playwright/test").TestInfo): string {
  return `${testInfo.project.name}-${testInfo.workerIndex}-${testInfo.titlePath.join("-")}`.replace(/[^a-zA-Z0-9_-]/g, "-").slice(0, 80);
}

async function clickEditor(page: import("@playwright/test").Page): Promise<void> {
  const viewport = page.viewportSize() ?? { width: 1280, height: 820 };
  await page.mouse.click(Math.min(420, viewport.width - 24), 96);
}

async function expectVisibleCaretX(page: import("@playwright/test").Page): Promise<number> {
  return expect.poll(async () => {
    const state = await page.evaluate(() => window.__slugApp!.getStateForTests() as { visibleCarets: Array<{ rect: { x: number } }> });
    return state.visibleCarets[0]?.rect.x ?? null;
  }).not.toBeNull().then(async () => {
    const state = await page.evaluate(() => window.__slugApp!.getStateForTests() as { visibleCarets: Array<{ rect: { x: number } }> });
    return state.visibleCarets[0]!.rect.x;
  });
}

type TestRect = { x: number; y: number; w: number; h: number };
type ContextCommand = "cut" | "copy" | "paste" | "undo" | "redo";
type CaretScrollState = {
  editorGroups: Array<{ editorRect: TestRect; scrollY: number }>;
  visibleCarets: Array<{ rect: TestRect }>;
};
type EditorStateForContextMenu = {
  activeText: string;
  selectedText: string;
  editorGroups: Array<{ editorRect: TestRect; gutterWidth: number }>;
  contextMenu: null | { items: Array<{ command: ContextCommand; rect: TestRect; enabled: boolean }> };
};

async function editorTextPoint(page: import("@playwright/test").Page, line: number): Promise<{ x: number; y: number }> {
  const state = await page.evaluate(() => window.__slugApp!.getStateForTests() as EditorStateForContextMenu);
  const rect = state.editorGroups[0]!.editorRect;
  const lineH = await page.evaluate(() => window.__slugApp!.renderer.lineHeight("code"));
  return { x: rect.x + state.editorGroups[0]!.gutterWidth + 16, y: rect.y + line * lineH + lineH / 2 };
}

async function waitForContextMenu(page: import("@playwright/test").Page): Promise<EditorStateForContextMenu> {
  await expect.poll(async () => {
    const state = await page.evaluate(() => window.__slugApp!.getStateForTests() as EditorStateForContextMenu);
    return Boolean(state.contextMenu);
  }).toBe(true);
  return page.evaluate(() => window.__slugApp!.getStateForTests() as EditorStateForContextMenu);
}

function menuItem(state: EditorStateForContextMenu, command: ContextCommand): { command: ContextCommand; rect: TestRect; enabled: boolean } {
  const item = state.contextMenu?.items.find((candidate) => candidate.command === command);
  if (!item) throw new Error(`Missing ${command} menu item`);
  return item;
}

async function clickMenuItem(page: import("@playwright/test").Page, state: EditorStateForContextMenu, command: ContextCommand): Promise<void> {
  const item = menuItem(state, command);
  await page.mouse.click(item.rect.x + item.rect.w / 2, item.rect.y + item.rect.h / 2);
}

function desktopShortcutModifier(): "Control" | "Meta" {
  return process.platform === "darwin" ? "Meta" : "Control";
}

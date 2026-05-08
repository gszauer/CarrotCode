import { EditorApp, importFilesForTests } from "./app/editor_app";
import { IndexedDbConnection } from "./platform/indexed_db";
import { IndexedVfs } from "./platform/indexed_vfs";
import type { FontSource } from "./renderer/webgl_renderer";

declare global {
  interface Window {
    __slugApp?: EditorApp;
    __slugImportFiles?: (files: File[]) => Promise<void>;
  }
}

async function main(): Promise<void> {
  const canvas = document.getElementById("editor-canvas");
  if (!(canvas instanceof HTMLCanvasElement)) throw new Error("Missing editor canvas");
  const fontSources = await loadFonts();
  const dbName = workspaceDatabaseName();
  const vfs = await IndexedVfs.openDefault(new IndexedDbConnection(dbName));
  const app = new EditorApp(canvas, vfs, fontSources);
  await app.start();
  window.__slugApp = app;
  window.__slugImportFiles = (files: File[]) => importFilesForTests(app, files);
  registerServiceWorker();
}

main().catch((error) => {
  const message = error instanceof Error ? error.message : String(error);
  document.body.textContent = `Failed to start carrot.code: ${message}`;
});

function workspaceDatabaseName(): string {
  const value = new URL(window.location.href).searchParams.get("db");
  if (!value) return "slug-editor";
  const slug = value.replace(/[^a-zA-Z0-9_-]/g, "").slice(0, 80);
  return slug ? `slug-editor-${slug}` : "slug-editor";
}

async function loadFonts(): Promise<FontSource[]> {
  return [
    { name: "Inter-Regular.ttf", buffer: await loadFont("Inter-Regular.ttf") },
    { name: "NotoEmoji-Regular.ttf", buffer: await loadFont("NotoEmoji-Regular.ttf") },
    { name: "MonaspaceNeon-Regular.ttf", buffer: await loadFont("MonaspaceNeon-Regular.ttf") }
  ];
}

async function loadFont(fileName: string): Promise<ArrayBuffer> {
  const response = await fetch(`./${fileName}`);
  if (!response.ok) throw new Error(`Could not load ${fileName}: ${response.status}`);
  return response.arrayBuffer();
}

function registerServiceWorker(): void {
  if (!("serviceWorker" in navigator)) return;
  if (window.location.protocol === "file:") return;

  const register = () => {
    navigator.serviceWorker.register("./sw.js").catch((error) => {
      console.warn("carrot.code service worker registration failed", error);
    });
  };

  if (document.readyState === "complete") register();
  else window.addEventListener("load", register, { once: true });
}

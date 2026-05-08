import { build } from "esbuild";
import { mkdir, copyFile, rm } from "node:fs/promises";

await rm("dist", { recursive: true, force: true });
await mkdir("dist", { recursive: true });

await build({
  entryPoints: ["src/main.ts"],
  outfile: "dist/app.js",
  bundle: true,
  format: "esm",
  target: "es2022",
  sourcemap: true,
  logLevel: "info"
});

await copyFile("index.html", "dist/index.html");
await copyFile("src/styles.css", "dist/styles.css");
await copyFile("assets/fonts/Inter-Regular.ttf", "dist/Inter-Regular.ttf");
await copyFile("assets/fonts/Inter-LICENSE.txt", "dist/Inter-LICENSE.txt");
await copyFile("assets/fonts/NotoEmoji-Regular.ttf", "dist/NotoEmoji-Regular.ttf");
await copyFile("assets/fonts/NotoEmoji-LICENSE.txt", "dist/NotoEmoji-LICENSE.txt");
await copyFile("assets/fonts/MonaspaceNeon-Regular.ttf", "dist/MonaspaceNeon-Regular.ttf");
await copyFile("assets/fonts/Monaspace-LICENSE.txt", "dist/Monaspace-LICENSE.txt");
await copyFile("assets/pwa/manifest.webmanifest", "dist/manifest.webmanifest");
await copyFile("assets/pwa/sw.js", "dist/sw.js");
await copyFile("assets/pwa/carrotcode.png", "dist/carrotcode.png");
await copyFile("assets/pwa/icon-180.png", "dist/icon-180.png");
await copyFile("assets/pwa/icon-192.png", "dist/icon-192.png");
await copyFile("assets/pwa/icon-512.png", "dist/icon-512.png");
await copyFile("assets/pwa/icon-maskable-512.png", "dist/icon-maskable-512.png");

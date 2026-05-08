import { copyFile, stat } from "node:fs/promises";
import { spawn } from "node:child_process";

const source = "assets/pwa/carrotcode.png";
const icons = [
  { path: "assets/pwa/icon-180.png", size: 180 },
  { path: "assets/pwa/icon-192.png", size: 192 },
  { path: "assets/pwa/icon-512.png", size: 512 }
];

await stat(source);
for (const icon of icons) await resizePng(source, icon.path, icon.size);
await copyFile("assets/pwa/icon-512.png", "assets/pwa/icon-maskable-512.png");

function resizePng(input, output, size) {
  return new Promise((resolve, reject) => {
    const child = spawn("sips", ["-z", String(size), String(size), input, "--out", output], { stdio: "ignore" });
    child.on("error", reject);
    child.on("exit", (code) => {
      if (code === 0) resolve();
      else reject(new Error(`sips failed while generating ${output}`));
    });
  });
}

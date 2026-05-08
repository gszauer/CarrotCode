import { createServer } from "node:http";
import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import { extname, join, normalize, resolve } from "node:path";

const root = resolve(process.argv[2] ?? "dist");
const port = Number(process.argv[3] ?? 4173);
const host = process.argv[4] ?? "127.0.0.1";

const mime = new Map([
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".map", "application/json; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".webmanifest", "application/manifest+json; charset=utf-8"],
  [".svg", "image/svg+xml; charset=utf-8"],
  [".png", "image/png"],
  [".ttf", "font/ttf"],
  [".txt", "text/plain; charset=utf-8"]
]);

function resolveRequestPath(url) {
  const rawPath = decodeURIComponent(new URL(url, "http://localhost").pathname);
  const candidate = normalize(rawPath === "/" ? "/index.html" : rawPath);
  const full = resolve(join(root, candidate));
  if (!full.startsWith(root)) return null;
  return full;
}

const server = createServer(async (req, res) => {
  const full = resolveRequestPath(req.url ?? "/");
  if (!full) {
    res.writeHead(403);
    res.end("Forbidden");
    return;
  }
  try {
    const info = await stat(full);
    if (!info.isFile()) throw new Error("not a file");
    res.writeHead(200, {
      "Content-Type": mime.get(extname(full)) ?? "application/octet-stream",
      "Cache-Control": "no-store"
    });
    createReadStream(full).pipe(res);
  } catch {
    res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("Not found");
  }
});

server.listen(port, host, () => {
  console.log(`Serving ${root} at http://${host}:${port}`);
});

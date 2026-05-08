export function normalizePath(input: string): string {
  const parts: string[] = [];
  const raw = input.replaceAll("\\", "/").split("/");
  for (const part of raw) {
    if (!part || part === ".") continue;
    if (part === "..") {
      parts.pop();
      continue;
    }
    parts.push(part);
  }
  return `/${parts.join("/")}`;
}

export function dirname(path: string): string {
  const p = normalizePath(path);
  if (p === "/") return "/";
  const idx = p.lastIndexOf("/");
  return idx <= 0 ? "/" : p.slice(0, idx);
}

export function basename(path: string): string {
  const p = normalizePath(path);
  if (p === "/") return "/";
  return p.slice(p.lastIndexOf("/") + 1);
}

export function joinPath(...parts: string[]): string {
  return normalizePath(parts.join("/"));
}

export function comparePath(a: string, b: string): number {
  return a.localeCompare(b, undefined, { sensitivity: "base" });
}

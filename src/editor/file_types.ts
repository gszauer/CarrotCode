import { basename } from "../vfs/path";

export const UNSUPPORTED_FILE_TEXT = "File type not supported";

const UNSUPPORTED_EXTENSIONS = new Set([
  "png", "jpg", "jpeg", "gif", "webp", "avif", "bmp", "ico", "icns", "tif", "tiff", "psd",
  "mp3", "wav", "ogg", "oga", "flac", "aac", "m4a", "wma", "aiff",
  "mp4", "mov", "m4v", "webm", "mkv", "avi", "wmv", "flv",
  "zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz", "br", "zst",
  "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods", "odp",
  "ttf", "otf", "woff", "woff2", "eot",
  "exe", "dll", "so", "dylib", "app", "bin", "wasm", "class", "jar",
  "sqlite", "sqlite3", "db", "mdb", "accdb", "realm",
  "pak", "dat", "asset", "bundle",
  "iso", "dmg", "img", "vhd", "vhdx"
]);

export function isUnsupportedFilePath(path: string): boolean {
  const name = basename(path);
  const dot = name.lastIndexOf(".");
  if (dot <= 0 || dot === name.length - 1) return false;
  return UNSUPPORTED_EXTENSIONS.has(name.slice(dot + 1).toLowerCase());
}

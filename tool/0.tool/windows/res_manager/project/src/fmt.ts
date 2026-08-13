export function fmtSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

export function fmtTime(ms: number): string {
  if (!ms) return "-";
  const d = new Date(ms);
  const p = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

export function extOf(name: string): string {
  const i = name.lastIndexOf(".");
  return i < 0 ? "" : name.slice(i + 1).toLowerCase();
}

const IMG_EXTS = new Set(["png", "jpg", "jpeg", "bmp", "gif", "webp", "svg"]);
const TEXT_EXTS = new Set([
  "json", "c", "h", "txt", "md", "ini", "ld", "bat", "ps1", "py",
  "inc", "cfg", "log", "evt", "xml", "yml", "yaml", "csv", "boot", "uart",
]);
const FONT_EXTS = new Set(["ttf", "otf", "ttc"]);

export type FileKind = "image" | "text" | "bin" | "font" | "other";

export function fileKind(name: string): FileKind {
  const ext = extOf(name);
  if (IMG_EXTS.has(ext)) return "image";
  if (TEXT_EXTS.has(ext)) return "text";
  if (ext === "bin") return "bin";
  if (FONT_EXTS.has(ext)) return "font";
  return "other";
}

/** 文件树扩展名徽标的配色分类 */
export function chipClass(name: string, isDir: boolean): string {
  if (isDir) return "dir";
  const ext = extOf(name);
  if (ext === "json") return "json";
  if (ext === "c") return "c";
  if (ext === "h") return "h";
  if (ext === "bin") return "bin";
  if (IMG_EXTS.has(ext)) return "img";
  if (FONT_EXTS.has(ext)) return "font";
  if (ext === "bat" || ext === "ps1" || ext === "py") return "script";
  if (ext === "exe") return "exe";
  return "misc";
}

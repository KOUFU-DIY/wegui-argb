import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import type {
  AppConfig,
  B64Content,
  CreatedConfig,
  DoneMsg,
  FontEntry,
  FsEntry,
  HexContent,
  LogMsg,
  NewFontOptions,
  ResolvedFont,
  RootInfo,
  RunResult,
  StageStats,
  TaskId,
  TextContent,
} from "./types";

export const locateToolRoot = (useSaved: boolean) =>
  invoke<RootInfo>("locate_tool_root", { useSaved });

export const setToolRoot = (path: string) => invoke<RootInfo>("set_tool_root", { path });

export const saveConfig = (config: AppConfig) => invoke<RootInfo>("save_config", { config });

export const defaultConfig = () => invoke<AppConfig>("default_config");

export const listDir = (path: string, exts?: string[]) =>
  invoke<FsEntry[]>("list_dir", { path, exts: exts ?? null });

export const readText = (path: string, maxBytes?: number) =>
  invoke<TextContent>("read_text", { path, maxBytes: maxBytes ?? null });

export const readFileBase64 = (path: string) =>
  invoke<B64Content>("read_file_base64", { path });

export const readHex = (path: string, offset = 0, len = 4096) =>
  invoke<HexContent>("read_hex", { path, offset, len });

export const stageStats = () => invoke<StageStats>("stage_stats");

export const openInExplorer = (path: string, select = false) =>
  invoke<void>("open_in_explorer", { path, select });

export const runTask = (task: TaskId, arg?: string) =>
  invoke<RunResult>("run_task", { task, arg: arg ?? null });

export const listFonts = () => invoke<FontEntry[]>("list_fonts");

export const readFontBase64 = (path: string) =>
  invoke<B64Content>("read_font_base64", { path });

export const resolveFontFile = (file: string) =>
  invoke<ResolvedFont>("resolve_font_file", { file });

export const createFontConfig = (options: NewFontOptions) =>
  invoke<CreatedConfig>("create_font_config", { options });

export const onToolLog = (cb: (m: LogMsg) => void): Promise<UnlistenFn> =>
  listen<LogMsg>("tool-log", (e) => cb(e.payload));

export const onToolDone = (cb: (m: DoneMsg) => void): Promise<UnlistenFn> =>
  listen<DoneMsg>("tool-done", (e) => cb(e.payload));

export function b64ToBytes(b64: string): Uint8Array {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

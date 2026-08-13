export interface ExeInfo {
  id: string;
  path: string;
  found: boolean;
  size: number;
}

export interface AppConfig {
  version: number;
  tools: Record<string, string>;
  fontDir: string;
  imgDir: string;
  mergeDir: string;
  mergeInputs: string[];
}

export interface RootInfo {
  root: string;
  platform: string;
  configPath: string;
  configExists: boolean;
  exes: ExeInfo[];
  config: AppConfig;
}

export interface FsEntry {
  name: string;
  path: string;
  isDir: boolean;
  size: number;
  mtimeMs: number;
}

export interface TextContent {
  text: string;
  encoding: string;
  truncated: boolean;
  size: number;
}

export interface B64Content {
  base64: string;
  mime: string;
  size: number;
}

export interface HexContent {
  base64: string;
  size: number;
  offset: number;
}

export interface StageStats {
  fontConfigs: number;
  fontFonts: number;
  fontOutputs: number;
  imgInputs: number;
  imgOutBins: number;
  imgOutCReady: boolean;
  mergedSize: number | null;
  mergedMtimeMs: number | null;
  embedReady: boolean;
}

export type TaskId =
  | "font_build_all"
  | "font_build_one"
  | "img_rgb565"
  | "img_rgb888"
  | "bin_merge"
  | "pipeline_all";

export interface RunResult {
  ok: boolean;
  failedSteps: number;
}

export interface LogMsg {
  stream: "sys" | "out" | "err";
  line: string;
}

export interface DoneMsg {
  ok: boolean;
  task: string;
  failedSteps: number;
}

export interface FontEntry {
  file: string;
  path: string;
  name: string;
  source: "project" | "user" | "system" | "other";
  size: number;
}

export interface NewFontOptions {
  fontPath: string;
  copyToProject: boolean;
  faceIndex: number;
  size: number;
  bpp: number;
  mode: "internal" | "external";
  symbol: string;
  ranges: [number, number][];
  chars: string;
  fileName: string;
  overwrite: boolean;
}

export interface CreatedConfig {
  jsonPath: string;
  copiedFont: boolean;
}

export interface ResolvedFont {
  path: string;
  source: "absolute" | "input" | "project" | "user" | "system";
}

export type PageId = "overview" | "font" | "img" | "bin" | "settings" | "about";

export const TASK_LABELS: Record<TaskId, string> = {
  font_build_all: "构建全部字体",
  font_build_one: "构建选中字体配置",
  img_rgb565: "图片取模 RGB565",
  img_rgb888: "图片取模 RGB888",
  bin_merge: "合并外挂 bin",
  pipeline_all: "一键构建 1→2→3",
};

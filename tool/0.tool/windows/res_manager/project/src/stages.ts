import type { AppConfig, TaskId } from "./types";

export interface StageGroup {
  label: string;
  path: string;
  exts?: string[];
  hint?: string;
}

export interface StageAction {
  task: TaskId;
  label: string;
  primary?: boolean;
}

export interface StageDesc {
  id: "font" | "img" | "bin";
  num: string;
  title: string;
  desc: string;
  dir: string;
  actions: StageAction[];
  groups: StageGroup[];
}

/** 统一用 / 拼接路径（后端两种分隔符都接受） */
export const joinPath = (a: string, b: string) => a.replace(/[\\/]+$/, "") + "/" + b;

/** 路径归一化后用于前缀比较（Windows 不区分大小写） */
export const normPath = (p: string) => p.replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();

const baseName = (p: string) => {
  const n = p.replace(/\\/g, "/").replace(/\/+$/, "");
  const i = n.lastIndexOf("/");
  return i < 0 ? n : n.slice(i + 1);
};

/** 阶段描述由配置动态生成（目录不写死） */
export function buildStages(cfg: AppConfig): Record<"font" | "img" | "bin", StageDesc> {
  return {
    font: {
      id: "font",
      num: "1",
      title: "字体取模",
      desc: "ttf / otf / ttc → 内置 .c/.h 字库，或外挂 .bin + 索引 .c/.h",
      dir: cfg.fontDir,
      actions: [{ task: "font_build_all", label: "构建全部字体", primary: true }],
      groups: [
        {
          label: "取模配置",
          path: joinPath(cfg.fontDir, "input"),
          exts: ["json"],
          hint: "每个 json 对应一种字体，选中可单独构建",
        },
        { label: "字体文件", path: joinPath(cfg.fontDir, "fonts"), hint: "项目内随包分发的字体" },
        { label: "输出产物", path: joinPath(cfg.fontDir, "output") },
      ],
    },
    img: {
      id: "img",
      num: "2",
      title: "图片取模",
      desc: "png / jpg / bmp → raw / indexqoi / indexqoimask(A8 蒙版，推荐)；_2bin 外挂储存，_2c 内置数组",
      dir: cfg.imgDir,
      actions: [
        { task: "img_rgb565", label: "转换 RGB565", primary: true },
        { task: "img_rgb888", label: "转换 RGB888" },
      ],
      groups: [
        {
          label: "输入图片",
          path: joinPath(cfg.imgDir, "input"),
          hint: "按 像素格式/压缩/去向 分桶存放",
        },
        { label: "输出 · 外挂 bin", path: joinPath(cfg.imgDir, "output/bin") },
        { label: "输出 · 内置 C", path: joinPath(cfg.imgDir, "output/c") },
      ],
    },
    bin: {
      id: "bin",
      num: "3",
      title: "外挂 bin 合并",
      desc: "把各来源目录的 *.bin 合并 → merged_bin.bin/.c/.h（embed 版供烧录工程编译）",
      dir: cfg.mergeDir,
      actions: [{ task: "bin_merge", label: "合并 bin", primary: true }],
      groups: [
        { label: "合并输出", path: joinPath(cfg.mergeDir, "output") },
        ...cfg.mergeInputs.map((p, i) => ({
          label: `来源 ${i + 1} · ${baseName(p)}`,
          path: p,
          exts: ["bin"],
        })),
      ],
    },
  };
}

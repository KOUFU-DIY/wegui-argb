import { useEffect, useState } from "react";
import { stageStats } from "../api";
import { fmtSize, fmtTime } from "../fmt";
import { IconArrow, IconFont, IconImage, IconPackage, IconPlay, Spinner } from "../Icons";
import type { PageId, RootInfo, StageStats, TaskId } from "../types";

interface Props {
  root: RootInfo;
  refreshTick: number;
  running: string | null;
  onRun: (task: TaskId, arg?: string) => void;
  onNavigate: (p: PageId) => void;
}

export default function OverviewPage({ root, refreshTick, running, onRun, onNavigate }: Props) {
  const [stats, setStats] = useState<StageStats | null>(null);

  useEffect(() => {
    let alive = true;
    stageStats()
      .then((s) => alive && setStats(s))
      .catch(() => undefined);
    return () => {
      alive = false;
    };
  }, [refreshTick, root]);

  const busy = !!running;

  return (
    <div className="page-scroll">
      <div className="ov-head">
        {root.exes.map((e) => (
          <span key={e.id} className={"chip " + (e.found ? "ok" : "bad")} title={e.path}>
            <span className="dot" />
            {e.id}
            {e.found ? "" : "（缺失）"}
          </span>
        ))}
        <span style={{ flex: 1 }} />
        <button className="btn primary" disabled={busy} onClick={() => onRun("pipeline_all")}>
          {running === "一键构建 1→2→3" ? <Spinner size={12} /> : <IconPlay size={13} />}
          一键构建 1→2→3
        </button>
      </div>

      <div className="pipeline">
        <div className="pipe-col">
          <div className="pipe-card">
            <div className="pc-head">
              <span className="pc-num">1</span>
              <IconFont size={16} />
              字体取模
            </div>
            <div className="pc-stats">
              <span>{stats ? `${stats.fontConfigs} 个取模配置 · ${stats.fontFonts} 个字体文件` : "…"}</span>
              <span className="dim">{stats ? `输出 ${stats.fontOutputs} 个文件` : ""}</span>
              <span className="dim">{root.config.fontDir}</span>
            </div>
            <div className="pc-foot">
              <button className="btn small" onClick={() => onNavigate("font")}>
                进入
              </button>
              <button className="btn small" disabled={busy} onClick={() => onRun("font_build_all")}>
                <IconPlay size={11} /> 构建
              </button>
            </div>
          </div>
          <div className="pipe-card">
            <div className="pc-head">
              <span className="pc-num">2</span>
              <IconImage size={16} />
              图片取模
            </div>
            <div className="pc-stats">
              <span>{stats ? `${stats.imgInputs} 张输入图片` : "…"}</span>
              <span className="dim">
                {stats
                  ? `输出 ${stats.imgOutBins} 个外挂 bin · res_img.c ${stats.imgOutCReady ? "已生成" : "未生成"}`
                  : ""}
              </span>
              <span className="dim">{root.config.imgDir}</span>
            </div>
            <div className="pc-foot">
              <button className="btn small" onClick={() => onNavigate("img")}>
                进入
              </button>
              <button className="btn small" disabled={busy} onClick={() => onRun("img_rgb565")}>
                <IconPlay size={11} /> RGB565
              </button>
            </div>
          </div>
        </div>

        <div className="arrow">
          <IconArrow size={20} />
        </div>

        <div className="pipe-col">
          <div className="pipe-card">
            <div className="pc-head">
              <span className="pc-num">3</span>
              <IconPackage size={16} />
              外挂 bin 合并
            </div>
            <div className="pc-stats">
              <span>
                {stats
                  ? stats.mergedSize != null
                    ? `merged_bin.bin ${fmtSize(stats.mergedSize)}`
                    : "merged_bin.bin 未生成"
                  : "…"}
              </span>
              <span className="dim">
                {stats && stats.mergedMtimeMs != null ? `更新于 ${fmtTime(stats.mergedMtimeMs)}` : ""}
              </span>
              <span className="dim">
                {stats ? `embed 数据版：${stats.embedReady ? "已生成" : "未生成"}` : ""}
              </span>
              <span className="dim">合并 {root.config.mergeInputs.length} 个来源目录</span>
            </div>
            <div className="pc-foot">
              <button className="btn small" onClick={() => onNavigate("bin")}>
                进入
              </button>
              <button className="btn small" disabled={busy} onClick={() => onRun("bin_merge")}>
                <IconPlay size={11} /> 合并
              </button>
            </div>
          </div>
        </div>
      </div>

      <div className="ov-info">
        <div className="card">
          <h3>流水线怎么走</h3>
          <ol>
            <li>
              <b>1 字体取模</b>：把 <code className="inline">input/*.json</code> 配置构建成字库（internal
              全内置 / external 外挂 bin + 内置索引）
            </li>
            <li>
              <b>2 图片取模</b>：input 分桶里的图片按 像素格式 × 压缩 × 去向 转成外挂 bin 或内置
              <code className="inline">res_img.c/.h</code>
            </li>
            <li>
              <b>3 合并</b>：各来源目录的 bin 拼成 <code className="inline">merged_bin.bin</code>
              ，并生成地址表 .c/.h 与 embed 数据版
            </li>
            <li>
              <b>使用</b>：内置产物直接参与固件编译；外挂 bin 按各自平台方式写入外挂储存
              （模拟器则直接读 exe 旁的 merged_bin.bin）
            </li>
          </ol>
        </div>
        <div className="card">
          <h3>使用提示</h3>
          <ul>
            <li>各阶段页面左侧可浏览输入 / 输出文件，点击即可预览（图片、bin 头解析、代码、字体）</li>
            <li>字体配置 json 选中后可单独构建，不必全量重跑</li>
            <li>目录与工具路径可在「设置」页自定义，保存于根目录 res_manager.json</li>
            <li>本工具只调度取模 exe，不内置转换逻辑；运行日志见底部控制台</li>
          </ul>
        </div>
      </div>
    </div>
  );
}

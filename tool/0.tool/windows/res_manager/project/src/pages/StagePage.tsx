import { useState } from "react";
import { openInExplorer } from "../api";
import FileBrowser from "../components/FileBrowser";
import NewFontDialog from "../components/NewFontDialog";
import PreviewPane from "../components/PreviewPane";
import { IconFolderOpen, IconPlay, IconPlus, Spinner } from "../Icons";
import { joinPath, type StageDesc } from "../stages";
import { TASK_LABELS, type AppConfig, type FsEntry, type TaskId } from "../types";

interface Props {
  stage: StageDesc;
  cfg: AppConfig;
  refreshTick: number;
  running: string | null;
  onRun: (task: TaskId, arg?: string) => void;
  onRefresh: () => void;
}

export default function StagePage({ stage, cfg, refreshTick, running, onRun, onRefresh }: Props) {
  const [sel, setSel] = useState<FsEntry | null>(null);
  const [newFontOpen, setNewFontOpen] = useState(false);

  return (
    <div className="stage">
      <div className="stage-toolbar">
        <div>
          <h2>
            <span className="stage-num">{stage.num}</span>
            {stage.title}
          </h2>
          <p>{stage.desc}</p>
        </div>
        <div className="actions">
          {stage.id === "font" && (
            <button className="btn" onClick={() => setNewFontOpen(true)}>
              <IconPlus size={13} /> 新建字体配置
            </button>
          )}
          {stage.actions.map((a) => (
            <button
              key={a.task}
              className={"btn" + (a.primary ? " primary" : "")}
              disabled={!!running}
              onClick={() => onRun(a.task)}
            >
              {running === TASK_LABELS[a.task] ? <Spinner size={12} /> : <IconPlay size={13} />}
              {a.label}
            </button>
          ))}
          <button className="btn" onClick={() => openInExplorer(stage.dir).catch(() => undefined)}>
            <IconFolderOpen size={14} /> 打开目录
          </button>
        </div>
      </div>
      <div className="stage-split">
        <FileBrowser
          groups={stage.groups}
          refreshTick={refreshTick}
          selectedPath={sel?.path}
          onSelect={setSel}
        />
        <PreviewPane
          sel={sel}
          running={running}
          fontInputDir={stage.id === "font" ? joinPath(cfg.fontDir, "input") : undefined}
          onRun={onRun}
        />
      </div>
      {stage.id === "font" && (
        <NewFontDialog
          open={newFontOpen}
          running={running}
          onClose={() => setNewFontOpen(false)}
          onSaved={(jsonPath, build) => {
            onRefresh();
            if (build) onRun("font_build_one", jsonPath);
          }}
        />
      )}
    </div>
  );
}

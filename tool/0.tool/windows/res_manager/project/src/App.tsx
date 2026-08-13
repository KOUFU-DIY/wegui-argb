import { useCallback, useEffect, useMemo, useState, type ReactElement } from "react";
import type { UnlistenFn } from "@tauri-apps/api/event";
import { locateToolRoot, onToolDone, onToolLog, openInExplorer, runTask } from "./api";
import ConsoleDrawer from "./components/ConsoleDrawer";
import {
  IconFolderOpen,
  IconFont,
  IconGear,
  IconGrid,
  IconImage,
  IconInfo,
  IconPackage,
  IconPlay,
  Logo,
  Spinner,
} from "./Icons";
import AboutPage from "./pages/AboutPage";
import OverviewPage from "./pages/OverviewPage";
import SettingsPage from "./pages/SettingsPage";
import StagePage from "./pages/StagePage";
import { buildStages } from "./stages";
import { TASK_LABELS, type LogMsg, type PageId, type RootInfo, type TaskId } from "./types";

const NAV: { id: PageId; label: string; num?: string; icon: (p: { size?: number }) => ReactElement }[] = [
  { id: "overview", label: "总览", icon: IconGrid },
  { id: "font", label: "字体取模", num: "1", icon: IconFont },
  { id: "img", label: "图片取模", num: "2", icon: IconImage },
  { id: "bin", label: "bin 合并", num: "3", icon: IconPackage },
  { id: "settings", label: "设置", icon: IconGear },
  { id: "about", label: "关于", icon: IconInfo },
];

export default function App() {
  const [root, setRoot] = useState<RootInfo | null>(null);
  const [rootErr, setRootErr] = useState<string | null>(null);
  const [page, setPage] = useState<PageId>("overview");
  const [running, setRunning] = useState<string | null>(null);
  const [logs, setLogs] = useState<LogMsg[]>([]);
  const [consoleOpen, setConsoleOpen] = useState(false);
  const [refreshTick, setRefreshTick] = useState(0);

  const detect = useCallback(() => {
    setRootErr(null);
    locateToolRoot(true)
      .then(setRoot)
      .catch((e) => setRootErr(String(e)));
  }, []);

  useEffect(detect, [detect]);

  useEffect(() => {
    let disposed = false;
    const unls: UnlistenFn[] = [];
    onToolLog((m) =>
      setLogs((ls) => {
        const next = ls.length >= 1500 ? ls.slice(-1000) : ls.slice();
        next.push(m);
        return next;
      }),
    ).then((u) => (disposed ? u() : unls.push(u)));
    onToolDone(() => {
      setRunning(null);
      setRefreshTick((t) => t + 1);
    }).then((u) => (disposed ? u() : unls.push(u)));
    return () => {
      disposed = true;
      unls.forEach((u) => u());
    };
  }, []);

  const handleRun = useCallback(async (task: TaskId, arg?: string) => {
    const label = TASK_LABELS[task];
    setRunning(label);
    setConsoleOpen(true);
    setLogs((ls) => [...ls, { stream: "sys", line: `━━ 开始：${label} ━━` }]);
    try {
      await runTask(task, arg);
    } catch (e) {
      setLogs((ls) => [...ls, { stream: "err", line: String(e) }]);
      setRunning(null);
    }
  }, []);

  const handleRootInfo = useCallback((r: RootInfo) => {
    setRoot(r);
    setRefreshTick((t) => t + 1);
  }, []);

  const stages = useMemo(() => (root ? buildStages(root.config) : null), [root]);

  if (rootErr) {
    return (
      <div className="splash">
        <div className="err-card">
          <Logo size={44} />
          <h3 style={{ margin: 0 }}>未找到工具链根目录</h3>
          <p>{rootErr}</p>
          <p style={{ color: "var(--tx2)", fontSize: 12 }}>
            自动探测标记：目录含 res_manager.json，或含 0.tool 与 1.font2c
          </p>
          <button className="btn primary" onClick={detect}>
            重新检测
          </button>
        </div>
      </div>
    );
  }

  if (!root || !stages) {
    return (
      <div className="splash">
        <Logo size={44} />
        <Spinner size={20} />
      </div>
    );
  }

  return (
    <div className="app">
      <header className="titlebar">
        <div className="brand">
          <Logo size={26} />
          WeGui 资源管理器
          <span className="ver">v0.1.0</span>
        </div>
        <div
          className="root-chip"
          title="在资源管理器中打开"
          onClick={() => openInExplorer("").catch(() => undefined)}
        >
          <IconFolderOpen size={13} />
          {root.root}
        </div>
        <div className="spacer" />
        {running && (
          <span className="chip run">
            <Spinner size={11} /> {running}
          </span>
        )}
        <button className="btn primary" disabled={!!running} onClick={() => handleRun("pipeline_all")}>
          <IconPlay size={13} /> 一键构建
        </button>
      </header>

      <div className="app-body">
        <nav className="rail">
          {NAV.map((n) => (
            <button
              key={n.id}
              className={"rail-item" + (page === n.id ? " active" : "")}
              onClick={() => setPage(n.id)}
            >
              {n.num && <span className="num">{n.num}</span>}
              <n.icon size={20} />
              {n.label}
            </button>
          ))}
        </nav>

        <main className="content">
          {page === "overview" && (
            <OverviewPage
              root={root}
              refreshTick={refreshTick}
              running={running}
              onRun={handleRun}
              onNavigate={setPage}
            />
          )}
          {(page === "font" || page === "img" || page === "bin") && (
            <StagePage
              key={page + "|" + JSON.stringify(root.config)}
              stage={stages[page]}
              cfg={root.config}
              refreshTick={refreshTick}
              running={running}
              onRun={handleRun}
              onRefresh={() => setRefreshTick((t) => t + 1)}
            />
          )}
          {page === "settings" && <SettingsPage root={root} onRootInfo={handleRootInfo} />}
          {page === "about" && <AboutPage root={root} />}
        </main>
      </div>

      <ConsoleDrawer
        logs={logs}
        open={consoleOpen}
        running={running}
        onToggle={() => setConsoleOpen(!consoleOpen)}
        onClear={() => setLogs([])}
      />
    </div>
  );
}

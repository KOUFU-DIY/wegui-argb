import { useCallback, useEffect, useRef, useState, type ReactNode } from "react";
import { listDir, openInExplorer } from "../api";
import { chipClass, extOf, fmtSize } from "../fmt";
import { IconChevron, IconFolderOpen, IconRefresh } from "../Icons";
import type { StageGroup } from "../stages";
import type { FsEntry } from "../types";

interface BrowserProps {
  groups: StageGroup[];
  refreshTick: number;
  selectedPath?: string;
  onSelect: (e: FsEntry) => void;
}

export default function FileBrowser({ groups, refreshTick, selectedPath, onSelect }: BrowserProps) {
  return (
    <div className="browser">
      {groups.map((g) => (
        <TreeGroup
          key={g.path + "|" + g.label}
          group={g}
          refreshTick={refreshTick}
          selectedPath={selectedPath}
          onSelect={onSelect}
        />
      ))}
    </div>
  );
}

interface GroupProps {
  group: StageGroup;
  refreshTick: number;
  selectedPath?: string;
  onSelect: (e: FsEntry) => void;
}

function TreeGroup({ group, refreshTick, selectedPath, onSelect }: GroupProps) {
  const [open, setOpen] = useState(true);
  const [entries, setEntries] = useState<FsEntry[] | null>(null);
  const [children, setChildren] = useState<Record<string, FsEntry[]>>({});
  const expandedRef = useRef<Set<string>>(new Set());
  const [, bump] = useState(0);

  const load = useCallback(() => {
    let alive = true;
    listDir(group.path, group.exts)
      .then((es) => {
        if (!alive) return;
        setEntries(es);
        // 刷新已展开子目录
        expandedRef.current.forEach((p) => {
          listDir(p).then((cs) => {
            if (alive) setChildren((m) => ({ ...m, [p]: cs }));
          });
        });
      })
      .catch(() => alive && setEntries([]));
    return () => {
      alive = false;
    };
  }, [group.path, group.exts]);

  useEffect(load, [load, refreshTick]);

  const toggleDir = (p: string) => {
    const set = expandedRef.current;
    if (set.has(p)) {
      set.delete(p);
    } else {
      set.add(p);
      if (!children[p]) {
        listDir(p).then((cs) => setChildren((m) => ({ ...m, [p]: cs })));
      }
    }
    bump((n) => n + 1);
  };

  const renderRows = (list: FsEntry[], depth: number): ReactNode =>
    list.map((e) => {
      const expanded = expandedRef.current.has(e.path);
      return (
        <div key={e.path}>
          <div
            className={"tree-row" + (e.path === selectedPath ? " sel" : "")}
            style={{ paddingLeft: 10 + depth * 16 }}
            onClick={() => (e.isDir ? toggleDir(e.path) : onSelect(e))}
            title={e.path}
          >
            {e.isDir ? (
              <IconChevron size={12} className={"caret" + (expanded ? " open" : "")} />
            ) : (
              <span className={"ext-chip " + chipClass(e.name, false)}>
                {(extOf(e.name) || "?").slice(0, 4)}
              </span>
            )}
            <span className="tree-name">{e.name}</span>
            {!e.isDir && <span className="tree-size">{fmtSize(e.size)}</span>}
          </div>
          {e.isDir && expanded && (
            <div>
              {children[e.path] ? (
                children[e.path].length > 0 ? (
                  renderRows(children[e.path], depth + 1)
                ) : (
                  <div className="tree-empty" style={{ paddingLeft: 28 + depth * 16 }}>
                    空目录
                  </div>
                )
              ) : (
                <div className="tree-empty" style={{ paddingLeft: 28 + depth * 16 }}>
                  加载中…
                </div>
              )}
            </div>
          )}
        </div>
      );
    });

  return (
    <div>
      <div className="tree-group-head" onClick={() => setOpen(!open)}>
        <IconChevron size={12} className={"caret" + (open ? " open" : "")} />
        <span>{group.label}</span>
        <span className="count">{entries === null ? "…" : entries.length}</span>
        <span className="tools" onClick={(ev) => ev.stopPropagation()}>
          <button className="icon-btn" title="刷新" onClick={load}>
            <IconRefresh size={13} />
          </button>
          <button
            className="icon-btn"
            title="在资源管理器中打开"
            onClick={() => openInExplorer(group.path).catch(() => undefined)}
          >
            <IconFolderOpen size={13} />
          </button>
        </span>
      </div>
      {open && (
        <div>
          {group.hint && <div className="tree-hint">{group.hint}</div>}
          {entries && entries.length === 0 && <div className="tree-empty">（无内容）</div>}
          {entries && renderRows(entries, 1)}
        </div>
      )}
    </div>
  );
}

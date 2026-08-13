import { useEffect, useRef } from "react";
import { IconChevron, IconTerminal, IconTrash, Spinner } from "../Icons";
import type { LogMsg } from "../types";

interface Props {
  logs: LogMsg[];
  open: boolean;
  running: string | null;
  onToggle: () => void;
  onClear: () => void;
}

export default function ConsoleDrawer({ logs, open, running, onToggle, onClear }: Props) {
  const bodyRef = useRef<HTMLDivElement>(null);
  const stickRef = useRef(true);

  useEffect(() => {
    if (open && stickRef.current && bodyRef.current) {
      bodyRef.current.scrollTop = bodyRef.current.scrollHeight;
    }
  }, [logs, open]);

  return (
    <div className="console">
      <div className="console-head" onClick={onToggle}>
        <IconTerminal size={15} />
        <span>控制台</span>
        {running ? (
          <span className="chip run">
            <Spinner size={11} /> {running}
          </span>
        ) : (
          <span className="chip">空闲</span>
        )}
        <span style={{ flex: 1 }} />
        <button
          className="icon-btn"
          title="清空"
          onClick={(e) => {
            e.stopPropagation();
            onClear();
          }}
        >
          <IconTrash size={14} />
        </button>
        <IconChevron
          size={14}
          className="caret"
          style={{ transform: open ? "rotate(90deg)" : "rotate(-90deg)" }}
        />
      </div>
      {open && (
        <div
          className="console-body"
          ref={bodyRef}
          onScroll={(e) => {
            const el = e.currentTarget;
            stickRef.current = el.scrollHeight - el.scrollTop - el.clientHeight < 30;
          }}
        >
          {logs.length === 0 && <div className="console-empty">运行任务后输出会显示在这里…</div>}
          {logs.map((l, i) => (
            <div key={i} className={"console-line " + l.stream}>
              {l.line}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

import { useEffect, useState } from "react";
import { open as openDialog } from "@tauri-apps/plugin-dialog";
import { defaultConfig, locateToolRoot, saveConfig, setToolRoot } from "../api";
import { IconFolderOpen, IconRefresh } from "../Icons";
import type { AppConfig, RootInfo } from "../types";

interface Props {
  root: RootInfo;
  onRootInfo: (r: RootInfo) => void;
}

const TOOL_LABELS: Record<string, string> = {
  font2c: "字体取模 font2c",
  img2bin_raw: "图片无压缩 img2bin_raw",
  img2bin_indexqoi: "图片压缩 img2bin_indexqoi",
  img2bin_indexqoimask: "A8 蒙版压缩 img2bin_indexqoimask",
  bin2c: "bin 合并 bin2c",
};

/** 归一化显示：反斜杠转 /，末尾去 / */
const norm = (p: string) => p.replace(/\\/g, "/").replace(/\/+$/, "");

/** 选到根目录内的路径时转成相对路径（可移植），否则保留绝对路径 */
function toRel(rootPath: string, abs: string): string {
  const r = norm(rootPath);
  const a = norm(abs);
  if (a.toLowerCase() === r.toLowerCase()) return "";
  if (a.toLowerCase().startsWith(r.toLowerCase() + "/")) return a.slice(r.length + 1);
  return a;
}

export default function SettingsPage({ root, onRootInfo }: Props) {
  const [form, setForm] = useState<AppConfig>(root.config);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => {
    setForm(root.config);
  }, [root]);

  const dirty = JSON.stringify(form) !== JSON.stringify(root.config);

  const flash = (ok: boolean, text: string) => {
    setMsg({ ok, text });
    setTimeout(() => setMsg(null), 3000);
  };

  const pickRoot = async () => {
    const picked = await openDialog({ directory: true, defaultPath: root.root, title: "选择工具链根目录" });
    if (typeof picked === "string") {
      try {
        onRootInfo(await setToolRoot(picked));
        flash(true, "已切换根目录");
      } catch (e) {
        flash(false, String(e));
      }
    }
  };

  const redetect = async () => {
    try {
      onRootInfo(await locateToolRoot(false));
      flash(true, "已重新自动检测根目录");
    } catch (e) {
      flash(false, String(e));
    }
  };

  const pickDir = async (cur: string, set: (v: string) => void) => {
    const picked = await openDialog({ directory: true, defaultPath: root.root, title: "选择目录" });
    if (typeof picked === "string") set(toRel(root.root, picked) || cur);
  };

  const pickExe = async (set: (v: string) => void) => {
    const picked = await openDialog({
      defaultPath: root.root,
      title: "选择工具可执行文件",
      filters: root.platform === "windows" ? [{ name: "可执行文件", extensions: ["exe"] }] : undefined,
    });
    if (typeof picked === "string") set(toRel(root.root, picked));
  };

  const doSave = async () => {
    try {
      onRootInfo(await saveConfig(form));
      flash(true, "配置已保存到 " + root.configPath);
    } catch (e) {
      flash(false, String(e));
    }
  };

  const doReset = async () => {
    setForm(await defaultConfig());
  };

  return (
    <div className="page-scroll">
      <div className="doc-page">
        <h2>设置</h2>

        <div className="card">
          <h3>工具链根目录</h3>
          <p>
            所有相对路径都基于此目录解析。当前：<code className="inline">{root.root}</code>
          </p>
          <p style={{ display: "flex", gap: 8 }}>
            <button className="btn small" onClick={pickRoot}>
              <IconFolderOpen size={13} /> 选择根目录…
            </button>
            <button className="btn small" onClick={redetect}>
              <IconRefresh size={13} /> 重新自动检测
            </button>
          </p>
          <p style={{ color: "var(--tx2)" }}>
            配置文件：<code className="inline">{root.configPath}</code>
            {root.configExists ? "（已存在）" : "（尚未创建，当前为默认配置，保存后生成）"}
          </p>
        </div>

        <div className="card">
          <h3>取模工具路径</h3>
          <p style={{ color: "var(--tx2)" }}>相对根目录或绝对路径均可；状态点表示文件是否存在。</p>
          {root.exes.map((e) => (
            <div className="cfg-row" key={e.id}>
              <span className="cfg-label">
                <span className={"dot-mini " + (e.found ? "ok" : "bad")} />
                {TOOL_LABELS[e.id] ?? e.id}
              </span>
              <input
                className="text-input"
                value={form.tools[e.id] ?? e.path}
                onChange={(ev) =>
                  setForm({ ...form, tools: { ...form.tools, [e.id]: ev.target.value } })
                }
                spellCheck={false}
              />
              <button
                className="btn small"
                onClick={() =>
                  pickExe((v) => setForm((f) => ({ ...f, tools: { ...f.tools, [e.id]: v } })))
                }
              >
                浏览…
              </button>
            </div>
          ))}
        </div>

        <div className="card">
          <h3>阶段目录</h3>
          <div className="cfg-row">
            <span className="cfg-label">字体取模目录</span>
            <input
              className="text-input"
              value={form.fontDir}
              onChange={(e) => setForm({ ...form, fontDir: e.target.value })}
              spellCheck={false}
            />
            <button
              className="btn small"
              onClick={() => pickDir(form.fontDir, (v) => setForm((f) => ({ ...f, fontDir: v })))}
            >
              浏览…
            </button>
          </div>
          <p className="cfg-hint">目录内约定包含 input/（json 配置）、fonts/（字体）、output/（产物）</p>
          <div className="cfg-row">
            <span className="cfg-label">图片取模目录</span>
            <input
              className="text-input"
              value={form.imgDir}
              onChange={(e) => setForm({ ...form, imgDir: e.target.value })}
              spellCheck={false}
            />
            <button
              className="btn small"
              onClick={() => pickDir(form.imgDir, (v) => setForm((f) => ({ ...f, imgDir: v })))}
            >
              浏览…
            </button>
          </div>
          <p className="cfg-hint">目录内约定包含 input/&lt;像素格式&gt;/&lt;分桶&gt; 与 output/bin、output/c</p>
          <div className="cfg-row">
            <span className="cfg-label">bin 合并目录</span>
            <input
              className="text-input"
              value={form.mergeDir}
              onChange={(e) => setForm({ ...form, mergeDir: e.target.value })}
              spellCheck={false}
            />
            <button
              className="btn small"
              onClick={() => pickDir(form.mergeDir, (v) => setForm((f) => ({ ...f, mergeDir: v })))}
            >
              浏览…
            </button>
          </div>
          <p className="cfg-hint">合并结果输出到该目录的 output/ 与 output/embed/</p>
        </div>

        <div className="card">
          <h3>合并来源目录</h3>
          <p style={{ color: "var(--tx2)" }}>
            每行一个目录，其中的 *.bin 按目录顺序合并（目录内部按文件名排序）。
          </p>
          <textarea
            className="text-input cfg-textarea"
            value={form.mergeInputs.join("\n")}
            onChange={(e) =>
              setForm({
                ...form,
                mergeInputs: e.target.value
                  .split("\n")
                  .map((s) => s.trim())
                  .filter((s) => s !== ""),
              })
            }
            spellCheck={false}
            rows={Math.max(3, form.mergeInputs.length + 1)}
          />
          <p style={{ display: "flex", gap: 8, marginTop: 8 }}>
            <button
              className="btn small"
              onClick={() =>
                pickDir("", (v) => setForm((f) => ({ ...f, mergeInputs: [...f.mergeInputs, v] })))
              }
            >
              <IconFolderOpen size={13} /> 添加目录…
            </button>
          </p>
        </div>

        <div style={{ display: "flex", gap: 10, alignItems: "center", marginBottom: 24 }}>
          <button className="btn primary" disabled={!dirty} onClick={doSave}>
            保存配置
          </button>
          <button className="btn" onClick={doReset}>
            恢复默认值
          </button>
          {dirty && <span style={{ color: "var(--warn)", fontSize: 12 }}>有未保存的修改</span>}
          {msg && (
            <span style={{ color: msg.ok ? "var(--ok)" : "var(--err)", fontSize: 12 }}>{msg.text}</span>
          )}
        </div>
      </div>
    </div>
  );
}

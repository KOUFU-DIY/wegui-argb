import { useCallback, useEffect, useMemo, useState } from "react";
import { open as openDialog } from "@tauri-apps/plugin-dialog";
import { createFontConfig, listFonts } from "../api";
import { fmtSize } from "../fmt";
import { IconClose, IconFolderOpen, IconPlay, Spinner } from "../Icons";
import FontSample, { useFontFace } from "./FontSample";
import type { FontEntry, NewFontOptions } from "../types";

interface Props {
  open: boolean;
  running: string | null;
  onClose: () => void;
  /** 保存成功：jsonPath 为新配置绝对路径；build 表示随后要构建 */
  onSaved: (jsonPath: string, build: boolean) => void;
}

const SRC_LABEL: Record<string, string> = {
  project: "项目",
  user: "用户",
  system: "系统",
  other: "外部",
};

const PRESETS: { key: string; label: string; warn?: string; ranges: [number, number][] }[] = [
  { key: "ascii", label: "ASCII 可见字符 U+0020-007E（95 字）", ranges: [[0x20, 0x7e]] },
  { key: "digits", label: "数字 0-9", ranges: [[0x30, 0x39]] },
  {
    key: "letters",
    label: "英文大小写字母 A-Z a-z",
    ranges: [
      [0x41, 0x5a],
      [0x61, 0x7a],
    ],
  },
  {
    key: "cnpunct",
    label: "常用中文标点 / 全角符号（，。！？：；“”…等）",
    ranges: [
      [0xb7, 0xb7],
      [0x2014, 0x2014],
      [0x2018, 0x2019],
      [0x201c, 0x201d],
      [0x2026, 0x2026],
      [0x3000, 0x3003],
      [0x3005, 0x3005],
      [0x3007, 0x3011],
      [0x3014, 0x3017],
      [0xff01, 0xff5e],
    ],
  },
  {
    key: "cjk",
    label: "CJK 基本汉字全集 U+4E00-9FA5",
    warn: "20902 字，体积很大",
    ranges: [[0x4e00, 0x9fa5]],
  },
  { key: "latin1", label: "Latin-1 补充 U+00A0-00FF（西欧重音）", ranges: [[0xa0, 0xff]] },
];

function parseRanges(spec: string): { ranges: [number, number][]; err?: string } {
  const out: [number, number][] = [];
  for (const raw of spec.split(",")) {
    const t = raw.trim();
    if (!t) continue;
    const m = /^(?:[Uu]\+)?([0-9A-Fa-f]{1,6})(?:\s*-\s*(?:[Uu]\+)?([0-9A-Fa-f]{1,6}))?$/.exec(t);
    if (!m) return { ranges: out, err: `无法解析“${t}”，格式如 U+4E00-U+9FA5 或 30-39` };
    const a = parseInt(m[1], 16);
    const b = m[2] ? parseInt(m[2], 16) : a;
    if (a > b) return { ranges: out, err: `区间“${t}”起点大于终点` };
    if (b > 0x10ffff || (a <= 0xdfff && b >= 0xd800)) {
      return { ranges: out, err: `区间“${t}”含非法 Unicode 码点` };
    }
    out.push([a, b]);
  }
  return { ranges: out };
}

function autoSymbol(file: string, size: number, bpp: number): string {
  let base = file
    .replace(/\.[^.]*$/, "")
    .toLowerCase()
    .replace(/[^a-z0-9_]/g, "_")
    .replace(/_+/g, "_")
    .replace(/^_+|_+$/g, "");
  if (!base) base = "font";
  if (/^\d/.test(base)) base = "f" + base;
  return `${base}_${size}_${bpp}bpp`;
}

export default function NewFontDialog({ open, running, onClose, onSaved }: Props) {
  const [fonts, setFonts] = useState<FontEntry[]>([]);
  const [filter, setFilter] = useState("");
  const [srcFilter, setSrcFilter] = useState<string>("all");
  const [selected, setSelected] = useState<FontEntry | null>(null);

  const { family, error: fontErr, loading: fontLoading } = useFontFace(
    open && selected ? selected.path : null,
  );

  const [size, setSize] = useState(16);
  const [bpp, setBpp] = useState(2);
  const [faceIndex, setFaceIndex] = useState(0);
  const [mode, setMode] = useState<"internal" | "external">("internal");
  const [presetsOn, setPresetsOn] = useState<Set<string>>(new Set(["ascii"]));
  const [rangesSpec, setRangesSpec] = useState("");
  const [chars, setChars] = useState("");
  const [symbol, setSymbol] = useState("");
  const [symbolTouched, setSymbolTouched] = useState(false);
  const [fileName, setFileName] = useState("");
  const [fileTouched, setFileTouched] = useState(false);
  const [copyToProject, setCopyToProject] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);

  // 打开时加载字体列表并重置状态
  useEffect(() => {
    if (!open) return;
    setErr(null);
    setSelected(null);
    setSymbolTouched(false);
    setFileTouched(false);
    listFonts()
      .then(setFonts)
      .catch((e) => setErr(String(e)));
  }, [open]);

  // 自动 symbol / 文件名
  useEffect(() => {
    if (!selected) return;
    if (!symbolTouched) setSymbol(autoSymbol(selected.file, size, bpp));
  }, [selected, size, bpp, symbolTouched]);
  useEffect(() => {
    if (!fileTouched && symbol) setFileName(symbol + ".json");
  }, [symbol, fileTouched]);
  useEffect(() => {
    if (selected) setCopyToProject(selected.source === "user" || selected.source === "other");
  }, [selected]);

  const shownFonts = useMemo(() => {
    const f = filter.trim().toLowerCase();
    return fonts.filter(
      (e) =>
        (srcFilter === "all" || e.source === srcFilter) &&
        (f === "" || e.file.toLowerCase().includes(f) || e.name.toLowerCase().includes(f)),
    );
  }, [fonts, filter, srcFilter]);

  const customParsed = useMemo(() => parseRanges(rangesSpec), [rangesSpec]);

  const glyphEstimate = useMemo(() => {
    let n = 0;
    for (const p of PRESETS) if (presetsOn.has(p.key)) for (const [a, b] of p.ranges) n += b - a + 1;
    for (const [a, b] of customParsed.ranges) n += b - a + 1;
    n += new Set(Array.from(chars)).size;
    return n;
  }, [presetsOn, customParsed, chars]);

  const pickExternal = async () => {
    const picked = await openDialog({
      title: "选择字体文件",
      filters: [{ name: "字体文件", extensions: ["ttf", "otf", "ttc"] }],
    });
    if (typeof picked === "string") {
      const file = picked.replace(/\\/g, "/").split("/").pop() ?? picked;
      const entry: FontEntry = { file, path: picked, name: "", source: "other", size: 0 };
      setFonts((fs) => [entry, ...fs.filter((f) => f.path !== picked)]);
      setSelected(entry);
    }
  };

  const doSave = useCallback(
    async (build: boolean, overwrite = false) => {
      if (!selected) {
        setErr("请先在左侧选择字体");
        return;
      }
      if (customParsed.err) {
        setErr(customParsed.err);
        return;
      }
      const ranges: [number, number][] = [];
      for (const p of PRESETS) if (presetsOn.has(p.key)) ranges.push(...p.ranges);
      ranges.push(...customParsed.ranges);
      if (ranges.length === 0 && chars.trim() === "") {
        setErr("字符集不能为空：至少勾选一个预设、填一个区间或输入若干字符");
        return;
      }
      if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(symbol)) {
        setErr("symbol 必须是合法 C 标识符（字母/数字/下划线，不能以数字开头）");
        return;
      }
      const fname = fileName.toLowerCase().endsWith(".json") ? fileName : fileName + ".json";
      const opts: NewFontOptions = {
        fontPath: selected.path,
        copyToProject,
        faceIndex,
        size,
        bpp,
        mode,
        symbol,
        ranges,
        chars,
        fileName: fname,
        overwrite,
      };
      setSaving(true);
      setErr(null);
      try {
        const res = await createFontConfig(opts);
        setSaving(false);
        onSaved(res.jsonPath, build);
        onClose();
      } catch (e) {
        setSaving(false);
        const msg = String(e);
        if (!overwrite && msg.includes("配置已存在")) {
          if (window.confirm(`${fname} 已存在，覆盖它？`)) {
            await doSave(build, true);
            return;
          }
        }
        setErr(msg);
      }
    },
    [selected, customParsed, presetsOn, chars, symbol, fileName, copyToProject, faceIndex, size, bpp, mode, onSaved, onClose],
  );

  if (!open) return null;

  const isTtc = selected?.file.toLowerCase().endsWith(".ttc") ?? false;

  return (
    <div className="modal-mask" onMouseDown={(e) => e.target === e.currentTarget && onClose()}>
      <div className="modal">
        <div className="modal-head">
          新建字体取模配置
          <button className="icon-btn" onClick={onClose} title="关闭">
            <IconClose size={16} />
          </button>
        </div>

        <div className="modal-body">
          <div className="modal-left">
            <div className="search-bar">
              <input
                className="text-input"
                placeholder="搜索字体文件名 / 名称…"
                value={filter}
                onChange={(e) => setFilter(e.target.value)}
                spellCheck={false}
              />
              <div className="src-filters">
                {["all", "project", "user", "system"].map((s) => (
                  <span
                    key={s}
                    className={"chip" + (srcFilter === s ? " on" : "")}
                    onClick={() => setSrcFilter(s)}
                  >
                    {s === "all" ? "全部" : SRC_LABEL[s]}
                  </span>
                ))}
              </div>
            </div>
            <div className="font-list">
              {shownFonts.length === 0 && <div className="tree-empty">（无匹配字体）</div>}
              {shownFonts.map((f) => (
                <div
                  key={f.path}
                  className={"font-row" + (selected?.path === f.path ? " sel" : "")}
                  onClick={() => setSelected(f)}
                  title={f.path}
                >
                  <span className={"src-tag " + f.source}>{SRC_LABEL[f.source]}</span>
                  <span className="f-main">
                    <span className="f-file">{f.file}</span>
                    {f.name && <span className="f-name">{f.name}</span>}
                  </span>
                  {f.size > 0 && <span className="f-size">{fmtSize(f.size)}</span>}
                </div>
              ))}
            </div>
            <div className="left-foot">
              <button className="btn small" style={{ width: "100%" }} onClick={pickExternal}>
                <IconFolderOpen size={13} /> 浏览其他字体文件…
              </button>
            </div>
          </div>

          <div className="modal-right">
            <div className="sect">
              <h4>预览{selected ? `：${selected.file}` : "（先在左侧选择字体）"}</h4>
              {fontErr && (
                <div style={{ color: "var(--warn)", fontSize: 11.5 }}>
                  {fontErr}，保存仍可正常取模
                </div>
              )}
              {fontLoading && (
                <div style={{ padding: 8 }}>
                  <Spinner size={16} />
                </div>
              )}
              {family && <FontSample family={family} size={size} bpp={bpp} />}
            </div>

            <div className="sect">
              <h4>渲染参数</h4>
              <div className="form-line">
                <label>字号 (px)</label>
                <input
                  className="text-input num-input"
                  type="number"
                  min={1}
                  max={4096}
                  value={size}
                  onChange={(e) => setSize(Math.max(1, Math.min(4096, Number(e.target.value) || 16)))}
                />
                <label style={{ width: "auto" }}>灰度位深</label>
                <span className="seg">
                  {[1, 2, 4, 8].map((b) => (
                    <button key={b} className={bpp === b ? "on" : ""} onClick={() => setBpp(b)}>
                      {b}bpp
                    </button>
                  ))}
                </span>
                {isTtc && (
                  <>
                    <label style={{ width: "auto" }}>face_index</label>
                    <input
                      className="text-input num-input"
                      type="number"
                      min={0}
                      value={faceIndex}
                      onChange={(e) => setFaceIndex(Math.max(0, Number(e.target.value) || 0))}
                    />
                  </>
                )}
              </div>
              <div className="form-line">
                <label>部署模式</label>
                <span className="seg">
                  <button className={mode === "internal" ? "on" : ""} onClick={() => setMode("internal")}>
                    internal · 编译进固件
                  </button>
                  <button className={mode === "external" ? "on" : ""} onClick={() => setMode("external")}>
                    external · 外挂 bin
                  </button>
                </span>
              </div>
            </div>

            <div className="sect">
              <h4>字符集（约 {glyphEstimate} 字形）</h4>
              {PRESETS.map((p) => (
                <label key={p.key} className="preset-item">
                  <input
                    type="checkbox"
                    checked={presetsOn.has(p.key)}
                    onChange={(e) => {
                      const next = new Set(presetsOn);
                      if (e.target.checked) next.add(p.key);
                      else next.delete(p.key);
                      setPresetsOn(next);
                    }}
                  />
                  {p.label}
                  {p.warn && <span className="pl-warn">（{p.warn}）</span>}
                </label>
              ))}
              <div className="form-line" style={{ marginTop: 10 }}>
                <label>自定义区间</label>
                <input
                  className="text-input"
                  placeholder="如 U+4E00-U+9FA5, 30-39（逗号分隔，可留空）"
                  value={rangesSpec}
                  onChange={(e) => setRangesSpec(e.target.value)}
                  spellCheck={false}
                />
              </div>
              {customParsed.err && (
                <div style={{ color: "var(--warn)", fontSize: 11.5, marginLeft: 94 }}>{customParsed.err}</div>
              )}
              <div className="form-line">
                <label>直接字符</label>
                <textarea
                  className="text-input cfg-textarea"
                  rows={2}
                  placeholder="把需要的字符原文直接粘贴到这里（自动去重，可留空）"
                  value={chars}
                  onChange={(e) => setChars(e.target.value)}
                  spellCheck={false}
                />
              </div>
              <div className="sim-cap">缺字将以空心方框占位导出（missing_glyph = box），构建时 font2c 会列出明细</div>
            </div>

            <div className="sect">
              <h4>输出</h4>
              <div className="form-line">
                <label>C 符号</label>
                <input
                  className="text-input"
                  value={symbol}
                  onChange={(e) => {
                    setSymbol(e.target.value);
                    setSymbolTouched(true);
                  }}
                  spellCheck={false}
                />
              </div>
              <div className="form-line">
                <label>json 文件名</label>
                <input
                  className="text-input"
                  value={fileName}
                  onChange={(e) => {
                    setFileName(e.target.value);
                    setFileTouched(true);
                  }}
                  spellCheck={false}
                />
              </div>
              <label className="preset-item" style={{ marginLeft: 94 }}>
                <input
                  type="checkbox"
                  checked={copyToProject}
                  onChange={(e) => setCopyToProject(e.target.checked)}
                />
                把字体文件复制到项目 fonts/（用户目录 / 外部字体建议开启，便于工程移植）
              </label>
            </div>
          </div>
        </div>

        <div className="modal-foot">
          <span className="err">{err ?? ""}</span>
          <button className="btn" onClick={onClose}>
            取消
          </button>
          <button className="btn" disabled={saving || !selected} onClick={() => doSave(false)}>
            {saving ? <Spinner size={12} /> : null} 保存配置
          </button>
          <button
            className="btn primary"
            disabled={saving || !selected || !!running}
            onClick={() => doSave(true)}
          >
            {saving ? <Spinner size={12} /> : <IconPlay size={12} />} 保存并构建
          </button>
        </div>
      </div>
    </div>
  );
}

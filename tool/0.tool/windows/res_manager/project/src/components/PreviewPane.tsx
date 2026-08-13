import { useEffect, useMemo, useState, type ReactNode } from "react";
import { b64ToBytes, openInExplorer, readFileBase64, readHex, readText, resolveFontFile } from "../api";
import { fileKind, fmtSize, fmtTime } from "../fmt";
import { normPath } from "../stages";
import FontSample, { useFontFace } from "./FontSample";
import type { ResolvedFont } from "../types";
import { IconFile, IconFolderOpen, IconPlay, Spinner } from "../Icons";
import type { FsEntry, TaskId } from "../types";

interface PaneProps {
  sel: FsEntry | null;
  running: string | null;
  /** 字体取模配置目录（用于识别可单独构建的 json），归一化前缀比较 */
  fontInputDir?: string;
  onRun: (task: TaskId, arg?: string) => void;
}

export default function PreviewPane({ sel, running, fontInputDir, onRun }: PaneProps) {
  if (!sel) {
    return (
      <div className="preview">
        <div className="preview-empty">
          <IconFile size={56} className="big-icon" />
          <div>从左侧选择文件进行预览</div>
        </div>
      </div>
    );
  }
  const kind = fileKind(sel.name);
  const isFontConfig =
    !!fontInputDir &&
    normPath(sel.path).startsWith(normPath(fontInputDir) + "/") &&
    sel.name.toLowerCase().endsWith(".json");
  return (
    <div className="preview">
      <div className="preview-head">
        <span className="fname" title={sel.path}>
          {sel.name}
        </span>
        <span className="fmeta">
          {fmtSize(sel.size)} · {fmtTime(sel.mtimeMs)}
        </span>
        <span className="actions">
          {isFontConfig && (
            <button
              className="btn small primary"
              disabled={!!running}
              onClick={() => onRun("font_build_one", sel.path)}
            >
              <IconPlay size={12} /> 构建此配置
            </button>
          )}
          <button
            className="btn small ghost"
            onClick={() => openInExplorer(sel.path, true).catch(() => undefined)}
          >
            <IconFolderOpen size={13} /> 定位文件
          </button>
        </span>
      </div>
      {kind === "text" && <TextPreview key={sel.path} sel={sel} isFontConfig={isFontConfig} />}
      {kind === "image" && <ImagePreview key={sel.path} sel={sel} />}
      {kind === "bin" && <BinPreview key={sel.path} sel={sel} />}
      {kind === "font" && <FontFilePreview key={sel.path} sel={sel} />}
      {kind === "other" && (
        <div className="preview-body">
          <InfoCard
            title="文件信息"
            items={[
              ["文件名", sel.name],
              ["大小", fmtSize(sel.size)],
              ["修改时间", fmtTime(sel.mtimeMs)],
              ["路径", sel.path],
            ]}
          />
          <div className="preview-note" style={{ border: "none", background: "none" }}>
            暂不支持预览该类型文件
          </div>
        </div>
      )}
    </div>
  );
}

function Loading() {
  return (
    <div className="preview-empty">
      <Spinner size={22} />
    </div>
  );
}

function ErrNote({ msg }: { msg: string }) {
  return (
    <div className="preview-note">
      <span className="tag warn">读取失败</span>
      <span style={{ userSelect: "text" }}>{msg}</span>
    </div>
  );
}

function InfoCard({
  title,
  items,
  accent,
}: {
  title: string;
  items: [string, string][];
  accent?: Set<string>;
}) {
  return (
    <div className="kv-card">
      <div className="kv-title">{title}</div>
      <div className="kv-grid">
        {items.map(([k, v]) => (
          <div className="kv-item" key={k}>
            <div className="k">{k}</div>
            <div className={"v" + (accent?.has(k) ? " accent" : "")}>{v}</div>
          </div>
        ))}
      </div>
    </div>
  );
}

/* ---------------- 文本 / 字体配置 ---------------- */

interface FontCfg {
  symbol?: string;
  font?: { file?: string; size?: number; face_index?: number };
  render?: { bpp?: number; missing_glyph?: string };
  charset?: { ranges?: unknown[]; chars?: string };
  deploy?: { mode?: string };
}

function TextPreview({ sel, isFontConfig }: { sel: FsEntry; isFontConfig: boolean }) {
  const [data, setData] = useState<{ text: string; encoding: string; truncated: boolean } | null>(null);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    let alive = true;
    readText(sel.path)
      .then((t) => alive && setData(t))
      .catch((e) => alive && setErr(String(e)));
    return () => {
      alive = false;
    };
  }, [sel.path]);

  const cfg: FontCfg | null = useMemo(() => {
    if (!isFontConfig || !data) return null;
    try {
      return JSON.parse(data.text) as FontCfg;
    } catch {
      return null;
    }
  }, [isFontConfig, data]);

  if (err) return <ErrNote msg={err} />;
  if (!data) return <Loading />;

  return (
    <>
      <div className="preview-note">
        <span className="tag">{data.encoding}</span>
        {data.truncated && <span className="tag warn">文件较大，仅显示前 512 KB</span>}
        <span>{data.text.split("\n").length} 行</span>
      </div>
      <div className="preview-body">
        {cfg && (
          <InfoCard
            title="字体取模配置"
            accent={new Set(["部署模式"])}
            items={[
              ["C 符号", cfg.symbol ?? "-"],
              ["字体文件", cfg.font?.file ?? "-"],
              ["字号", cfg.font?.size != null ? `${cfg.font.size} px` : "-"],
              ["灰度位深", cfg.render?.bpp != null ? `${cfg.render.bpp} bpp` : "-"],
              [
                "部署模式",
                cfg.deploy?.mode === "external"
                  ? "external（外挂 bin）"
                  : cfg.deploy?.mode === "internal"
                    ? "internal（内置数组）"
                    : (cfg.deploy?.mode ?? "-"),
              ],
              ["码点区间", `${cfg.charset?.ranges?.length ?? 0} 个`],
              ["离散字符", `${cfg.charset?.chars?.length ?? 0} 个`],
              ["缺字处理", cfg.render?.missing_glyph === "box" ? "空心方框占位" : "默认"],
            ]}
          />
        )}
        {cfg && <FontConfigFontPreview cfg={cfg} />}
        <pre className="code">{data.text}</pre>
      </div>
    </>
  );
}

/** json 配置的字体实况预览：按 font2c 搜索顺序解析 font.file，用配置的字号/bpp 渲染 */
const RESOLVE_SRC_LABEL: Record<string, string> = {
  absolute: "绝对路径",
  input: "json 目录",
  project: "项目 fonts/",
  user: "用户字体目录",
  system: "系统字体目录",
};

function FontConfigFontPreview({ cfg }: { cfg: FontCfg }) {
  const [resolved, setResolved] = useState<ResolvedFont | null>(null);
  const [resolveErr, setResolveErr] = useState<string | null>(null);

  const file = cfg.font?.file ?? "";
  useEffect(() => {
    let alive = true;
    setResolved(null);
    setResolveErr(null);
    resolveFontFile(file)
      .then((r) => alive && setResolved(r))
      .catch((e) => alive && setResolveErr(String(e)));
    return () => {
      alive = false;
    };
  }, [file]);

  const { family, error: fontErr, loading } = useFontFace(resolved?.path ?? null);

  return (
    <div className="kv-card">
      <div className="kv-title">
        字体预览
        {resolved && (
          <span style={{ fontWeight: 400, color: "var(--tx2)", fontSize: 11 }}>
            {file} · {RESOLVE_SRC_LABEL[resolved.source] ?? resolved.source}
          </span>
        )}
      </div>
      <div style={{ padding: "10px 14px" }}>
        {resolveErr && (
          <div style={{ color: "var(--warn)", fontSize: 12 }}>{resolveErr}</div>
        )}
        {fontErr && (
          <div style={{ color: "var(--warn)", fontSize: 12 }}>{fontErr}，不影响 font2c 构建</div>
        )}
        {loading && <Spinner size={16} />}
        {family && (
          <FontSample family={family} size={cfg.font?.size ?? 16} bpp={cfg.render?.bpp ?? 2} />
        )}
      </div>
    </div>
  );
}

/* ---------------- 图片 ---------------- */

function ImagePreview({ sel }: { sel: FsEntry }) {
  const [url, setUrl] = useState<string | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [dim, setDim] = useState<{ w: number; h: number } | null>(null);
  const [zoom, setZoom] = useState<number | "fit">("fit");

  useEffect(() => {
    let alive = true;
    readFileBase64(sel.path)
      .then((d) => alive && setUrl(`data:${d.mime};base64,${d.base64}`))
      .catch((e) => alive && setErr(String(e)));
    return () => {
      alive = false;
    };
  }, [sel.path]);

  if (err) return <ErrNote msg={err} />;
  if (!url) return <Loading />;

  const zooms: (number | "fit")[] = ["fit", 1, 2, 4, 8];
  return (
    <div className="img-stage-wrap">
      <div className="img-toolbar">
        <span>
          {dim ? `${dim.w} × ${dim.h} px` : "…"} · {fmtSize(sel.size)}
        </span>
        <span style={{ flex: 1 }} />
        {zooms.map((z) => (
          <button
            key={String(z)}
            className={"btn small" + (zoom === z ? " primary" : " ghost")}
            onClick={() => setZoom(z)}
          >
            {z === "fit" ? "适应" : `${z}×`}
          </button>
        ))}
      </div>
      <div className="img-canvas">
        <img
          src={url}
          alt={sel.name}
          onLoad={(e) => setDim({ w: e.currentTarget.naturalWidth, h: e.currentTarget.naturalHeight })}
          style={
            zoom === "fit"
              ? { maxWidth: "100%", maxHeight: "100%" }
              : { width: dim ? dim.w * zoom : undefined }
          }
        />
      </div>
    </div>
  );
}

/* ---------------- bin ---------------- */

const COMP_NAMES: Record<number, string> = {
  0: "raw 无压缩",
  1: "rle",
  2: "imprle",
  3: "qoi",
  4: "indexqoi",
  5: "qoif",
  6: "indexqoimask (A8 蒙版)",
};
const PIX_NAMES: Record<number, string> = {
  0: "RGB565",
  1: "RGB888",
  4: "RGB332",
  5: "ARGB8888",
  6: "ARGB6666",
  7: "ARGB4444",
  8: "ARGB8565",
  9: "ARGB2222",
  10: "RAGB5155",
  11: "A8 (alpha)",
  12: "A4 (alpha)",
  13: "A2 (alpha)",
  14: "A1 (alpha)",
  15: "OLED 位图",
};

function hexDump(bytes: Uint8Array, base = 0): string {
  const lines: string[] = [];
  for (let o = 0; o < bytes.length; o += 16) {
    const chunk = bytes.subarray(o, o + 16);
    const hex = Array.from(chunk)
      .map((b) => b.toString(16).padStart(2, "0"))
      .join(" ");
    const ascii = Array.from(chunk)
      .map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : "·"))
      .join("");
    lines.push(`${(base + o).toString(16).padStart(8, "0")}  ${hex.padEnd(47)}  ${ascii}`);
  }
  return lines.join("\n");
}

function BinPreview({ sel }: { sel: FsEntry }) {
  const [bytes, setBytes] = useState<Uint8Array | null>(null);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    let alive = true;
    readHex(sel.path, 0, 4096)
      .then((d) => alive && setBytes(b64ToBytes(d.base64)))
      .catch((e) => alive && setErr(String(e)));
    return () => {
      alive = false;
    };
  }, [sel.path]);

  if (err) return <ErrNote msg={err} />;
  if (!bytes) return <Loading />;

  const isMerged = sel.name.toLowerCase() === "merged_bin.bin";
  let headerCard: ReactNode = null;
  if (bytes.length >= 6 && bytes[0] === 0x00) {
    const fmt = bytes[1];
    const comp = (fmt >> 4) & 0xf;
    const pix = fmt & 0xf;
    const w = (bytes[2] << 8) | bytes[3];
    const h = (bytes[4] << 8) | bytes[5];
    headerCard = (
      <InfoCard
        title="图片资源头（6 字节，v2 格式）"
        accent={new Set(["像素格式", "压缩算法"])}
        items={[
          ["资源类型", "0x00 图片"],
          ["格式字节", "0x" + fmt.toString(16).padStart(2, "0")],
          ["压缩算法", COMP_NAMES[comp] ?? `未知 (0x${comp.toString(16)})`],
          ["像素格式", PIX_NAMES[pix] ?? `未知 (0x${pix.toString(16)})`],
          ["尺寸", `${w} × ${h} px`],
          ["负载大小", fmtSize(Math.max(0, sel.size - 6))],
        ]}
      />
    );
  } else if (bytes.length >= 6 && bytes[0] === 0x02 && bytes[1] === 0x00) {
    const crc = Array.from(bytes.subarray(2, 6))
      .map((b) => b.toString(16).padStart(2, "0"))
      .join("");
    headerCard = (
      <InfoCard
        title="字体外挂 blob（font2c external）"
        accent={new Set(["CRC-32"])}
        items={[
          ["标识", "0x02 0x00"],
          ["CRC-32", "0x" + crc.toUpperCase()],
          ["glyph 描述表偏移", "6"],
          ["总大小", fmtSize(sel.size)],
        ]}
      />
    );
  }

  return (
    <div className="preview-body">
      {isMerged && (
        <div className="preview-note">
          <span className="tag">合并镜像</span>
          <span>多个资源 bin 顺序拼接而成，资源地址见同目录 merged_bin.h（首个资源头如下）</span>
        </div>
      )}
      {headerCard ?? (
        <InfoCard
          title="二进制文件"
          items={[
            ["大小", fmtSize(sel.size)],
            ["头字节", bytes.length ? "0x" + bytes[0].toString(16).padStart(2, "0") : "-"],
            ["识别", "未匹配已知资源头"],
          ]}
        />
      )}
      <div className="preview-note">
        <span className="tag">hex</span>
        <span>{sel.size > 4096 ? "仅显示前 4 KB" : "完整内容"}</span>
      </div>
      <pre className="code">{hexDump(bytes)}</pre>
    </div>
  );
}

/* ---------------- 字体文件 ---------------- */

let fontSeq = 0;

function FontFilePreview({ sel }: { sel: FsEntry }) {
  const [family, setFamily] = useState<string | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [sample, setSample] = useState("WeGui 嵌入式 GUI 字体预览 — 永字八法 AaBbGg 0123456789");

  useEffect(() => {
    let alive = true;
    let face: FontFace | null = null;
    const name = `res_prev_font_${++fontSeq}`;
    readFileBase64(sel.path)
      .then(async (d) => {
        const buf = b64ToBytes(d.base64).buffer as ArrayBuffer;
        face = new FontFace(name, buf);
        await face.load();
        if (!alive) return;
        document.fonts.add(face);
        setFamily(name);
      })
      .catch((e) => alive && setErr(`浏览器无法解析该字体（${e instanceof Error ? e.message : e}）`));
    return () => {
      alive = false;
      if (face) document.fonts.delete(face);
    };
  }, [sel.path]);

  return (
    <div className="preview-body">
      <InfoCard
        title="字体文件"
        items={[
          ["文件名", sel.name],
          ["大小", fmtSize(sel.size)],
          ["修改时间", fmtTime(sel.mtimeMs)],
        ]}
      />
      {err && <ErrNote msg={err} />}
      {!err && !family && <Loading />}
      {family && (
        <div className="font-sample">
          <input value={sample} onChange={(e) => setSample(e.target.value)} placeholder="输入预览文本…" />
          {[14, 22, 34].map((px) => (
            <div key={px} style={{ fontFamily: `"${family}"`, fontSize: px, lineHeight: 1.4 }}>
              {sample || " "}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

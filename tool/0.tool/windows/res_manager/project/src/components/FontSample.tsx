import { useEffect, useRef, useState } from "react";
import { b64ToBytes, readFontBase64 } from "../api";

export const DEFAULT_SAMPLE = "WeGui 温度 25.6℃ AaBb 0123";

/** 取模模拟：白字黑底渲染后按 bpp 量化灰度（近似 font2c 的量化公式） */
export function drawSim(
  canvas: HTMLCanvasElement,
  family: string,
  text: string,
  sizePx: number,
  bpp: number,
) {
  const pad = 4;
  const probe = canvas.getContext("2d");
  if (!probe) return;
  probe.font = `${sizePx}px "${family}"`;
  const w = Math.max(10, Math.ceil(probe.measureText(text).width) + pad * 2);
  const h = Math.ceil(sizePx * 1.45) + pad * 2;
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, w, h);
  ctx.font = `${sizePx}px "${family}"`;
  ctx.textBaseline = "top";
  ctx.fillStyle = "#fff";
  ctx.fillText(text, pad, pad + sizePx * 0.12);
  const img = ctx.getImageData(0, 0, w, h);
  const d = img.data;
  const levels = (1 << bpp) - 1;
  for (let i = 0; i < d.length; i += 4) {
    const level = Math.round((d[i] * levels) / 255);
    const q = Math.round((level * 255) / levels);
    d[i] = d[i + 1] = d[i + 2] = q;
    d[i + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  canvas.style.width = `${w * 3}px`;
  canvas.style.height = `${h * 3}px`;
}

let fontSeq = 0;

/** 从绝对路径加载字体为临时 FontFace，返回可用的 font-family 名 */
export function useFontFace(path: string | null): {
  family: string | null;
  error: string | null;
  loading: boolean;
} {
  const [family, setFamily] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    setFamily(null);
    setError(null);
    if (!path) return;
    let alive = true;
    let face: FontFace | null = null;
    const name = `resmgr_font_${++fontSeq}`;
    readFontBase64(path)
      .then(async (d) => {
        const buf = b64ToBytes(d.base64).buffer as ArrayBuffer;
        face = new FontFace(name, buf);
        await face.load();
        if (!alive) return;
        document.fonts.add(face);
        setFamily(name);
      })
      .catch((e) =>
        alive && setError(`浏览器无法解析该字体（${e instanceof Error ? e.message : e}）`),
      );
    return () => {
      alive = false;
      if (face) document.fonts.delete(face);
    };
  }, [path]);

  return { family, error, loading: !!path && !family && !error };
}

interface SampleProps {
  family: string;
  size: number;
  bpp: number;
}

/** 可编辑样张 + 实况渲染 + 取模模拟画布 */
export default function FontSample({ family, size, bpp }: SampleProps) {
  const [sample, setSample] = useState(DEFAULT_SAMPLE);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    if (canvasRef.current) drawSim(canvasRef.current, family, sample || " ", size, bpp);
  }, [family, sample, size, bpp]);

  return (
    <>
      <div className="form-line">
        <input
          className="text-input"
          value={sample}
          onChange={(e) => setSample(e.target.value)}
          placeholder="输入预览文本…"
          spellCheck={false}
        />
      </div>
      <div className="font-live" style={{ fontFamily: `"${family}"`, fontSize: Math.max(size, 10) }}>
        {sample || " "}
      </div>
      <div className="sim-wrap">
        <canvas ref={canvasRef} />
      </div>
      <div className="sim-cap">
        取模模拟：{size}px · {bpp}bpp（灰度量化近似，实际以 font2c 输出为准，放大 3 倍显示）
      </div>
    </>
  );
}

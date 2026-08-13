import { openUrl } from "@tauri-apps/plugin-opener";
import { IconLink } from "../Icons";
import type { RootInfo } from "../types";

interface Props {
  root: RootInfo;
}

const REPOS: [string, string][] = [
  ["we_font2c 字体取模", "https://github.com/KOUFU-DIY/we_font2c"],
  ["we_Img2bin 图片取模", "https://github.com/KOUFU-DIY/we_Img2bin"],
  ["we_bin2c bin 合并/转 C", "https://github.com/KOUFU-DIY/we_bin2c"],
];

export default function AboutPage({ root }: Props) {
  return (
    <div className="page-scroll">
      <div className="doc-page">
        <h2>关于</h2>

        <div className="card">
          <h3>WeGui 资源管理器 v0.1.0</h3>
          <p>
            WeGui 取模工具链的图形前端：预览字体 / 图片 / bin 取模资料，一键批处理取模。转换本身完全由
            外部取模 exe 完成，本工具不内置任何转换逻辑。
          </p>
          <p>
            当前平台：<code className="inline">{root.platform}</code> · 根目录：
            <code className="inline">{root.root}</code>
          </p>
        </div>

        <div className="card">
          <h3>取模工具源码</h3>
          <ul>
            {REPOS.map(([name, url]) => (
              <li key={url}>
                {name} ·{" "}
                <span className="link" onClick={() => openUrl(url).catch(() => undefined)}>
                  <IconLink size={11} /> {url}
                </span>
              </li>
            ))}
          </ul>
        </div>

        <div className="card">
          <h3>目录与移植性</h3>
          <p>
            工具路径、阶段目录、合并来源都可在「设置」页自定义，保存于根目录的
            <code className="inline">res_manager.json</code>（相对根目录或绝对路径均可）。
          </p>
          <p>
            默认布局下工具位于 <code className="inline">0.tool/&lt;平台&gt;/&lt;工具&gt;/</code>，当前随附
            windows 版；后续提供 macOS 版工具时放入 <code className="inline">0.tool/macos/</code>{" "}
            即可。批处理步骤已在程序内部用跨平台方式实现，不依赖 .bat。
          </p>
        </div>
      </div>
    </div>
  );
}

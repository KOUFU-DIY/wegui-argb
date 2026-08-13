//! 新建字体配置：字体枚举（项目 / 用户 / 系统）、字体文件读取（预览用）、
//! 生成 font2c 的 input/*.json 配置（布局与 1.manage_input.bat 向导一致）。

use base64::{engine::general_purpose::STANDARD as B64, Engine};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use crate::fsx::B64Content;
use crate::paths::{current_ctx, path_starts_with_ci, resolve_path};
use crate::AppState;

const FONT_EXTS: [&str; 3] = ["ttf", "otf", "ttc"];
const FONT_B64_CAP: u64 = 64 * 1024 * 1024;

fn is_font_file(p: &Path) -> bool {
    p.extension()
        .map(|e| {
            let e = e.to_string_lossy().to_lowercase();
            FONT_EXTS.contains(&e.as_str())
        })
        .unwrap_or(false)
}

/// 项目 fonts 目录（<fontDir>/fonts）
fn project_fonts_dir(root: &Path, font_dir: &str) -> PathBuf {
    resolve_path(root, font_dir).join("fonts")
}

/// 系统 / 用户字体目录（按平台）
fn os_font_dirs() -> Vec<(PathBuf, &'static str)> {
    let mut v: Vec<(PathBuf, &'static str)> = Vec::new();
    #[cfg(windows)]
    {
        if let Ok(local) = std::env::var("LOCALAPPDATA") {
            v.push((
                Path::new(&local).join("Microsoft").join("Windows").join("Fonts"),
                "user",
            ));
        }
        if let Ok(windir) = std::env::var("WINDIR") {
            v.push((Path::new(&windir).join("Fonts"), "system"));
        }
    }
    #[cfg(target_os = "macos")]
    {
        if let Ok(home) = std::env::var("HOME") {
            v.push((Path::new(&home).join("Library").join("Fonts"), "user"));
        }
        v.push((PathBuf::from("/Library/Fonts"), "system"));
        v.push((PathBuf::from("/System/Library/Fonts"), "system"));
    }
    v
}

/// Windows 注册表里的字体显示名（文件名小写 -> 友好名）
#[cfg(windows)]
fn registry_font_names() -> HashMap<String, String> {
    use winreg::enums::{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    use winreg::RegKey;
    let mut map = HashMap::new();
    for hive in [HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER] {
        let Ok(key) = RegKey::predef(hive)
            .open_subkey("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts")
        else {
            continue;
        };
        for name in key.enum_values().flatten().map(|(n, _)| n) {
            let Ok(value) = key.get_value::<String, _>(&name) else {
                continue;
            };
            if value.is_empty() {
                continue;
            }
            let base = Path::new(&value)
                .file_name()
                .map(|s| s.to_string_lossy().to_lowercase())
                .unwrap_or_default();
            if base.is_empty() {
                continue;
            }
            let mut friendly = name.clone();
            for suffix in [
                " (TrueType)",
                " (OpenType)",
                " (VGA res)",
                " (All res)",
                " (8514a res)",
            ] {
                if let Some(s) = friendly.strip_suffix(suffix) {
                    friendly = s.to_string();
                    break;
                }
            }
            map.entry(base).or_insert(friendly);
        }
    }
    map
}

#[cfg(not(windows))]
fn registry_font_names() -> HashMap<String, String> {
    HashMap::new()
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FontEntry {
    pub file: String,
    pub path: String,
    pub name: String,
    pub source: String, // project | user | system | other
    pub size: u64,
}

/// 枚举可选字体：项目 fonts/ → 用户字体目录 → 系统字体目录
#[tauri::command]
pub fn list_fonts(state: tauri::State<'_, AppState>) -> Result<Vec<FontEntry>, String> {
    let (root, cfg) = current_ctx(&state)?;
    let names = registry_font_names();
    let mut dirs: Vec<(PathBuf, &str)> = vec![(project_fonts_dir(&root, &cfg.font_dir), "project")];
    for (d, tag) in os_font_dirs() {
        dirs.push((d, tag));
    }
    let mut out = Vec::new();
    for (dir, source) in dirs {
        let Ok(rd) = fs::read_dir(&dir) else { continue };
        let mut batch: Vec<FontEntry> = rd
            .flatten()
            .filter_map(|e| {
                let p = e.path();
                if !p.is_file() || !is_font_file(&p) {
                    return None;
                }
                let file = e.file_name().to_string_lossy().into_owned();
                let size = e.metadata().map(|m| m.len()).unwrap_or(0);
                Some(FontEntry {
                    name: names.get(&file.to_lowercase()).cloned().unwrap_or_default(),
                    path: p.to_string_lossy().into_owned(),
                    file,
                    source: source.to_string(),
                    size,
                })
            })
            .collect();
        batch.sort_by(|a, b| a.file.to_lowercase().cmp(&b.file.to_lowercase()));
        out.extend(batch);
    }
    Ok(out)
}

/// 读取字体文件（绝对路径）供前端 FontFace 预览。
/// 只允许 ttf/otf/ttc 扩展名，避免变成任意文件读取接口。
#[tauri::command]
pub fn read_font_base64(path: String) -> Result<B64Content, String> {
    let p = PathBuf::from(&path);
    if !is_font_file(&p) {
        return Err("仅支持 ttf / otf / ttc".into());
    }
    let md = fs::metadata(&p).map_err(|e| e.to_string())?;
    if md.len() > FONT_B64_CAP {
        return Err(format!("字体文件过大（{} MB）", md.len() >> 20));
    }
    let data = fs::read(&p).map_err(|e| e.to_string())?;
    let ext = p
        .extension()
        .map(|s| s.to_string_lossy().to_lowercase())
        .unwrap_or_default();
    Ok(B64Content {
        base64: B64.encode(&data),
        mime: match ext.as_str() {
            "otf" => "font/otf".into(),
            "ttc" => "font/collection".into(),
            _ => "font/ttf".into(),
        },
        size: md.len(),
    })
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ResolvedFont {
    pub path: String,
    pub source: String, // absolute | input | project | user | system
}

/// 按 font2c 的搜索顺序解析配置里的 font.file：
/// 绝对路径 → json 所在目录（fontDir/input）→ 项目 fonts/ → 系统/用户字体目录（按文件名）
#[tauri::command]
pub fn resolve_font_file(
    state: tauri::State<'_, AppState>,
    file: String,
) -> Result<ResolvedFont, String> {
    let (root, cfg) = current_ctx(&state)?;
    let f = file.trim();
    if f.is_empty() {
        return Err("配置未指定字体文件".into());
    }
    let p = Path::new(f);
    if p.is_absolute() {
        if p.is_file() {
            return Ok(ResolvedFont {
                path: p.to_string_lossy().into_owned(),
                source: "absolute".into(),
            });
        }
        return Err(format!("找不到字体文件: {f}"));
    }
    let font_dir = resolve_path(&root, &cfg.font_dir);
    let cand = font_dir.join("input").join(f);
    if cand.is_file() {
        return Ok(ResolvedFont {
            path: cand.to_string_lossy().into_owned(),
            source: "input".into(),
        });
    }
    let cand = font_dir.join("fonts").join(f);
    if cand.is_file() {
        return Ok(ResolvedFont {
            path: cand.to_string_lossy().into_owned(),
            source: "project".into(),
        });
    }
    let base = p
        .file_name()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_default();
    for (dir, tag) in os_font_dirs() {
        let cand = dir.join(&base);
        if cand.is_file() {
            return Ok(ResolvedFont {
                path: cand.to_string_lossy().into_owned(),
                source: tag.to_string(),
            });
        }
    }
    Err(format!("找不到字体文件: {f}（构建时 font2c 同样会报错）"))
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NewFontOptions {
    /// 字体文件绝对路径
    pub font_path: String,
    /// 复制字体到项目 fonts/（用户目录 / 其他位置的字体建议开启）
    pub copy_to_project: bool,
    pub face_index: u32,
    pub size: u32,
    pub bpp: u32,
    pub mode: String, // internal | external
    pub symbol: String,
    /// 码点区间 [start, end]（闭区间）
    pub ranges: Vec<(u32, u32)>,
    /// 直接给出的字符
    pub chars: String,
    /// 输出 json 文件名（写入 <fontDir>/input/）
    pub file_name: String,
    pub overwrite: bool,
}

fn valid_symbol(s: &str) -> bool {
    !s.is_empty()
        && s.chars().next().map(|c| c.is_ascii_alphabetic() || c == '_').unwrap_or(false)
        && s.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
}

fn fmt_u(cp: u32) -> String {
    if cp > 0xFFFF {
        format!("U+{cp:X}")
    } else {
        format!("U+{cp:04X}")
    }
}

/// 生成与向导一致的 JSON 文本（2 空格缩进，LF，末尾换行）
fn config_json_text(o: &NewFontOptions, font_field: &str, chars: &str) -> String {
    let mut l: Vec<String> = Vec::new();
    l.push("{".into());
    l.push("  \"version\": 1,".into());
    l.push(format!(
        "  \"symbol\": {},",
        serde_json::to_string(&o.symbol).unwrap_or_default()
    ));
    l.push("  \"font\": {".into());
    l.push(format!(
        "    \"file\": {},",
        serde_json::to_string(font_field).unwrap_or_default()
    ));
    if o.face_index > 0 {
        l.push(format!("    \"size\": {},", o.size));
        l.push(format!("    \"face_index\": {}", o.face_index));
    } else {
        l.push(format!("    \"size\": {}", o.size));
    }
    l.push("  },".into());
    l.push("  \"render\": {".into());
    l.push(format!("    \"bpp\": {},", o.bpp));
    l.push("    \"missing_glyph\": \"box\"".into());
    l.push("  },".into());
    l.push("  \"charset\": {".into());
    let has_ranges = !o.ranges.is_empty();
    let has_chars = !chars.is_empty();
    if has_ranges {
        l.push("    \"ranges\": [".into());
        for (i, (a, b)) in o.ranges.iter().enumerate() {
            let comma = if i + 1 < o.ranges.len() { "," } else { "" };
            l.push(format!("      [\"{}\", \"{}\"]{comma}", fmt_u(*a), fmt_u(*b)));
        }
        l.push(format!("    ]{}", if has_chars { "," } else { "" }));
    }
    if has_chars {
        l.push(format!(
            "    \"chars\": {}",
            serde_json::to_string(chars).unwrap_or_default()
        ));
    }
    l.push("  },".into());
    l.push("  \"deploy\": {".into());
    l.push(format!(
        "    \"mode\": {}",
        serde_json::to_string(&o.mode).unwrap_or_default()
    ));
    l.push("  }".into());
    l.push("}".into());
    l.join("\n") + "\n"
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CreatedConfig {
    pub json_path: String,
    pub copied_font: bool,
}

/// 生成 font2c 取模配置 json（必要时把字体复制进项目 fonts/）
#[tauri::command]
pub fn create_font_config(
    state: tauri::State<'_, AppState>,
    options: NewFontOptions,
) -> Result<CreatedConfig, String> {
    let (root, cfg) = current_ctx(&state)?;
    let o = &options;

    // ---- 校验 ----
    let font_src = PathBuf::from(&o.font_path);
    if !font_src.is_file() || !is_font_file(&font_src) {
        return Err("字体文件无效".into());
    }
    if o.size < 1 || o.size > 4096 {
        return Err("字号必须在 1-4096 之间".into());
    }
    if ![1u32, 2, 4, 8].contains(&o.bpp) {
        return Err("bpp 只能是 1 / 2 / 4 / 8".into());
    }
    if o.mode != "internal" && o.mode != "external" {
        return Err("部署模式只能是 internal 或 external".into());
    }
    if !valid_symbol(&o.symbol) {
        return Err("symbol 必须是合法 C 标识符".into());
    }
    if o.file_name.is_empty()
        || o.file_name.contains(['/', '\\', ':'])
        || !o.file_name.to_lowercase().ends_with(".json")
    {
        return Err("json 文件名无效".into());
    }
    for (a, b) in &o.ranges {
        if a > b || *b > 0x10FFFF || (*a <= 0xDFFF && *b >= 0xD800) {
            return Err(format!("码点区间无效: {}-{}", fmt_u(*a), fmt_u(*b)));
        }
    }
    // 字符去重（按码点，保持顺序）
    let mut seen = std::collections::HashSet::new();
    let chars: String = o.chars.chars().filter(|c| seen.insert(*c)).collect();
    if o.ranges.is_empty() && chars.is_empty() {
        return Err("字符集不能为空：至少选择一个区间或输入若干字符".into());
    }

    let font_base = font_src
        .file_name()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_default();

    // ---- 目标路径 ----
    let input_dir = resolve_path(&root, &cfg.font_dir).join("input");
    fs::create_dir_all(&input_dir).map_err(|e| e.to_string())?;
    let json_path = input_dir.join(&o.file_name);
    if json_path.exists() && !o.overwrite {
        return Err(format!("配置已存在: {}", o.file_name));
    }

    // ---- 字体文件去向：决定 font.file 字段 ----
    let proj_fonts = project_fonts_dir(&root, &cfg.font_dir);
    let mut copied = false;
    let font_field: String = if o.copy_to_project {
        fs::create_dir_all(&proj_fonts).map_err(|e| e.to_string())?;
        let dst = proj_fonts.join(&font_base);
        if !path_starts_with_ci(&font_src, &proj_fonts) {
            fs::copy(&font_src, &dst).map_err(|e| format!("复制字体失败: {e}"))?;
            copied = true;
        }
        font_base.clone()
    } else {
        // 项目 fonts/ 或系统字体目录内：写文件名（font2c 会搜索）；其他位置写绝对路径
        let in_search_scope = path_starts_with_ci(&font_src, &proj_fonts)
            || os_font_dirs()
                .iter()
                .any(|(d, tag)| *tag == "system" && path_starts_with_ci(&font_src, d));
        if in_search_scope {
            font_base.clone()
        } else {
            font_src.to_string_lossy().replace('\\', "/")
        }
    };

    let text = config_json_text(o, &font_field, &chars);
    fs::write(&json_path, text.as_bytes()).map_err(|e| e.to_string())?;

    Ok(CreatedConfig {
        json_path: json_path.to_string_lossy().into_owned(),
        copied_font: copied,
    })
}

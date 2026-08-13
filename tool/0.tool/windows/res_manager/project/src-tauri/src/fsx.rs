//! 文件浏览与预览读取（限制在根目录及各配置目录内）。

use base64::{engine::general_purpose::STANDARD as B64, Engine};
use serde::Serialize;
use std::fs;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;
use std::time::UNIX_EPOCH;

use crate::paths::{checked_path, current_ctx, resolve_path};
use crate::AppState;

/// 文本预览上限（超出截断显示）
const TEXT_CAP: usize = 512 * 1024;
/// base64 整文件读取上限（图片 / 字体预览）
const B64_CAP: u64 = 64 * 1024 * 1024;
/// hex 预览单次读取上限
const HEX_CAP: usize = 64 * 1024;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FsEntry {
    pub name: String,
    pub path: String,
    pub is_dir: bool,
    pub size: u64,
    pub mtime_ms: u64,
}

fn mtime_ms(md: &fs::Metadata) -> u64 {
    md.modified()
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

/// 列出目录内容；`exts` 只过滤文件（子目录总是保留），扩展名不带点。
/// 返回条目的 path 沿用调用方传入的 path 拼接，便于前端继续访问。
#[tauri::command]
pub fn list_dir(
    state: tauri::State<'_, AppState>,
    path: String,
    exts: Option<Vec<String>>,
) -> Result<Vec<FsEntry>, String> {
    let dir = checked_path(&state, &path)?;
    if !dir.is_dir() {
        return Ok(vec![]);
    }
    let parent = path.trim_end_matches(['/', '\\']).to_string();
    let mut out = Vec::new();
    for e in fs::read_dir(&dir).map_err(|e| e.to_string())? {
        let Ok(e) = e else { continue };
        let Ok(md) = e.metadata() else { continue };
        let name = e.file_name().to_string_lossy().into_owned();
        let is_dir = md.is_dir();
        if !is_dir {
            if let Some(list) = &exts {
                let ext = Path::new(&name)
                    .extension()
                    .map(|s| s.to_string_lossy().to_lowercase())
                    .unwrap_or_default();
                if !list.iter().any(|x| x.eq_ignore_ascii_case(&ext)) {
                    continue;
                }
            }
        }
        out.push(FsEntry {
            path: format!("{parent}/{name}"),
            name,
            is_dir,
            size: if is_dir { 0 } else { md.len() },
            mtime_ms: mtime_ms(&md),
        });
    }
    out.sort_by(|a, b| {
        b.is_dir
            .cmp(&a.is_dir)
            .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
    });
    Ok(out)
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TextContent {
    pub text: String,
    pub encoding: String,
    pub truncated: bool,
    pub size: u64,
}

/// 解码字节为文本：UTF-8 优先，失败回退 GBK（bat 等历史文件），再退 lossy
fn decode_text(buf: &[u8], truncated: bool) -> (String, String) {
    let b = if buf.starts_with(&[0xEF, 0xBB, 0xBF]) {
        &buf[3..]
    } else {
        buf
    };
    match std::str::from_utf8(b) {
        Ok(s) => return (s.to_string(), "utf-8".into()),
        Err(e) => {
            // 截断时可能把多字节字符切断，此时取合法前缀仍按 UTF-8 处理
            if truncated && e.error_len().is_none() {
                let valid = &b[..e.valid_up_to()];
                return (String::from_utf8_lossy(valid).into_owned(), "utf-8".into());
            }
        }
    }
    let (cow, _, had_err) = encoding_rs::GBK.decode(b);
    if !had_err {
        return (cow.into_owned(), "gbk".into());
    }
    (String::from_utf8_lossy(b).into_owned(), "utf-8?".into())
}

#[tauri::command]
pub fn read_text(
    state: tauri::State<'_, AppState>,
    path: String,
    max_bytes: Option<usize>,
) -> Result<TextContent, String> {
    let abs = checked_path(&state, &path)?;
    let md = fs::metadata(&abs).map_err(|e| e.to_string())?;
    let cap = max_bytes.unwrap_or(TEXT_CAP);
    let mut buf = Vec::new();
    fs::File::open(&abs)
        .map_err(|e| e.to_string())?
        .take(cap as u64)
        .read_to_end(&mut buf)
        .map_err(|e| e.to_string())?;
    let truncated = md.len() > buf.len() as u64;
    let (text, encoding) = decode_text(&buf, truncated);
    Ok(TextContent {
        text,
        encoding,
        truncated,
        size: md.len(),
    })
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct B64Content {
    pub base64: String,
    pub mime: String,
    pub size: u64,
}

fn mime_of(name: &str) -> &'static str {
    let ext = Path::new(name)
        .extension()
        .map(|s| s.to_string_lossy().to_lowercase())
        .unwrap_or_default();
    match ext.as_str() {
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "bmp" => "image/bmp",
        "gif" => "image/gif",
        "webp" => "image/webp",
        "svg" => "image/svg+xml",
        "ttf" => "font/ttf",
        "otf" => "font/otf",
        "ttc" => "font/collection",
        _ => "application/octet-stream",
    }
}

/// 整文件读出为 base64（图片 / 字体预览用）
#[tauri::command]
pub fn read_file_base64(
    state: tauri::State<'_, AppState>,
    path: String,
) -> Result<B64Content, String> {
    let abs = checked_path(&state, &path)?;
    let md = fs::metadata(&abs).map_err(|e| e.to_string())?;
    if md.len() > B64_CAP {
        return Err(format!("文件过大（{} MB），无法预览", md.len() >> 20));
    }
    let data = fs::read(&abs).map_err(|e| e.to_string())?;
    Ok(B64Content {
        base64: B64.encode(&data),
        mime: mime_of(&path).to_string(),
        size: md.len(),
    })
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HexContent {
    pub base64: String,
    pub size: u64,
    pub offset: u64,
}

/// 读取文件片段（hex 预览用）
#[tauri::command]
pub fn read_hex(
    state: tauri::State<'_, AppState>,
    path: String,
    offset: u64,
    len: usize,
) -> Result<HexContent, String> {
    let abs = checked_path(&state, &path)?;
    let md = fs::metadata(&abs).map_err(|e| e.to_string())?;
    let mut f = fs::File::open(&abs).map_err(|e| e.to_string())?;
    f.seek(SeekFrom::Start(offset)).map_err(|e| e.to_string())?;
    let mut buf = Vec::new();
    f.take(len.min(HEX_CAP) as u64)
        .read_to_end(&mut buf)
        .map_err(|e| e.to_string())?;
    Ok(HexContent {
        base64: B64.encode(&buf),
        size: md.len(),
        offset,
    })
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StageStats {
    pub font_configs: u32,
    pub font_fonts: u32,
    pub font_outputs: u32,
    pub img_inputs: u32,
    pub img_out_bins: u32,
    pub img_out_c_ready: bool,
    pub merged_size: Option<u64>,
    pub merged_mtime_ms: Option<u64>,
    pub embed_ready: bool,
}

fn count_files(dir: &Path, exts: Option<&[&str]>) -> u32 {
    let Ok(rd) = fs::read_dir(dir) else { return 0 };
    rd.flatten()
        .filter(|e| {
            let p = e.path();
            if !p.is_file() {
                return false;
            }
            match exts {
                None => true,
                Some(list) => p
                    .extension()
                    .map(|s| {
                        let ext = s.to_string_lossy().to_lowercase();
                        list.contains(&ext.as_str())
                    })
                    .unwrap_or(false),
            }
        })
        .count() as u32
}

fn count_files_recursive(dir: &Path, exts: &[&str], depth: u32) -> u32 {
    if depth > 6 {
        return 0;
    }
    let Ok(rd) = fs::read_dir(dir) else { return 0 };
    let mut n = 0;
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            n += count_files_recursive(&p, exts, depth + 1);
        } else if p
            .extension()
            .map(|s| {
                let ext = s.to_string_lossy().to_lowercase();
                exts.contains(&ext.as_str())
            })
            .unwrap_or(false)
        {
            n += 1;
        }
    }
    n
}

/// 总览页统计数据（按配置目录统计）
#[tauri::command]
pub fn stage_stats(state: tauri::State<'_, AppState>) -> Result<StageStats, String> {
    let (root, cfg) = current_ctx(&state)?;
    let font_dir = resolve_path(&root, &cfg.font_dir);
    let img_dir = resolve_path(&root, &cfg.img_dir);
    let merge_dir = resolve_path(&root, &cfg.merge_dir);
    let img_exts: &[&str] = &["png", "jpg", "jpeg", "bmp", "gif", "webp"];
    let merged = merge_dir.join("output").join("merged_bin.bin");
    let merged_md = fs::metadata(&merged).ok();
    Ok(StageStats {
        font_configs: count_files(&font_dir.join("input"), Some(&["json"])),
        font_fonts: count_files(&font_dir.join("fonts"), Some(&["ttf", "otf", "ttc"])),
        font_outputs: count_files(&font_dir.join("output"), None),
        img_inputs: count_files_recursive(&img_dir.join("input"), img_exts, 0),
        img_out_bins: count_files(&img_dir.join("output").join("bin"), Some(&["bin"])),
        img_out_c_ready: img_dir.join("output").join("c").join("res_img.c").is_file(),
        merged_size: merged_md.as_ref().map(|m| m.len()),
        merged_mtime_ms: merged_md.as_ref().map(mtime_ms),
        embed_ready: merge_dir
            .join("output")
            .join("embed")
            .join("merged_bin.c")
            .is_file(),
    })
}

/// 在系统文件管理器中打开目录 / 定位文件
#[tauri::command]
pub fn open_in_explorer(
    state: tauri::State<'_, AppState>,
    path: String,
    select: bool,
) -> Result<(), String> {
    let abs = checked_path(&state, &path)?;
    if !abs.exists() {
        return Err(format!("路径不存在: {path}"));
    }
    #[cfg(windows)]
    {
        let mut cmd = std::process::Command::new("explorer");
        if select && abs.is_file() {
            cmd.arg(format!("/select,{}", abs.display()));
        } else {
            cmd.arg(&abs);
        }
        cmd.spawn().map_err(|e| e.to_string())?;
    }
    #[cfg(target_os = "macos")]
    {
        let mut cmd = std::process::Command::new("open");
        if select && abs.is_file() {
            cmd.arg("-R");
        }
        cmd.arg(&abs);
        cmd.spawn().map_err(|e| e.to_string())?;
    }
    #[cfg(all(unix, not(target_os = "macos")))]
    {
        let _ = select;
        std::process::Command::new("xdg-open")
            .arg(if abs.is_file() {
                abs.parent().unwrap_or(&abs).to_path_buf()
            } else {
                abs.clone()
            })
            .spawn()
            .map_err(|e| e.to_string())?;
    }
    Ok(())
}

//! 根目录定位、可配置路径（res_manager.json）与路径安全解析。
//!
//! 目录不再写死：工具 exe 路径、三个阶段目录、合并来源目录都来自配置文件
//! `<root>/res_manager.json`（缺省时使用与现有 tool/ 目录一致的默认值）。
//! 配置里的路径既可以是相对根目录的相对路径，也可以是绝对路径。
//! 可移植性：默认工具路径为 `0.tool/<platform>/<tool>/<tool>[.exe]`，macOS 版只需
//! 在 `0.tool/macos/` 放入对应可执行文件。

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::fs;
use std::path::{Component, Path, PathBuf};
use tauri::Manager;

use crate::AppState;

pub const CONFIG_FILE: &str = "res_manager.json";
pub const TOOL_IDS: [&str; 5] = [
    "font2c",
    "img2bin_raw",
    "img2bin_indexqoi",
    "img2bin_indexqoimask",
    "bin2c",
];

fn platform_dir() -> &'static str {
    if cfg!(target_os = "macos") {
        "macos"
    } else {
        "windows"
    }
}

fn default_tool_path(id: &str) -> String {
    let ext = if cfg!(windows) { ".exe" } else { "" };
    format!("0.tool/{}/{id}/{id}{ext}", platform_dir())
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct AppConfig {
    pub version: u32,
    /// 工具 id -> 可执行文件路径（相对根目录或绝对路径）
    pub tools: BTreeMap<String, String>,
    /// 字体取模目录（内部约定含 input/ fonts/ output/）
    pub font_dir: String,
    /// 图片取模目录（内部约定含 input/<分桶> 与 output/bin、output/c）
    pub img_dir: String,
    /// bin 合并目录（输出到其 output/ 与 output/embed/）
    pub merge_dir: String,
    /// 合并来源目录列表（其中的 *.bin 按顺序合并）
    pub merge_inputs: Vec<String>,
}

impl Default for AppConfig {
    fn default() -> Self {
        AppConfig {
            version: 1,
            tools: TOOL_IDS
                .iter()
                .map(|id| (id.to_string(), default_tool_path(id)))
                .collect(),
            font_dir: "1.font2c".into(),
            img_dir: "2.img2c".into(),
            merge_dir: "3.bin2c".into(),
            merge_inputs: vec!["1.font2c/output".into(), "2.img2c/output/bin".into()],
        }
    }
}

/// 取某工具的配置路径（配置缺项时回退默认）
pub fn tool_path(cfg: &AppConfig, id: &str) -> String {
    cfg.tools
        .get(id)
        .cloned()
        .unwrap_or_else(|| default_tool_path(id))
}

/// 词法归一化：消去 `.` 与 `..`，不访问文件系统
pub fn normalize(p: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for c in p.components() {
        match c {
            Component::ParentDir => {
                out.pop();
            }
            Component::CurDir => {}
            other => out.push(other.as_os_str()),
        }
    }
    out
}

/// 配置路径解析：绝对路径原样用，相对路径拼到根目录下，然后归一化
pub fn resolve_path(root: &Path, p: &str) -> PathBuf {
    let pp = Path::new(p);
    if pp.is_absolute() {
        normalize(pp)
    } else {
        normalize(&root.join(pp))
    }
}

/// 大小写不敏感（Windows 习惯）的前缀判断
pub fn path_starts_with_ci(p: &Path, base: &Path) -> bool {
    let ps = p.to_string_lossy().to_lowercase().replace('\\', "/");
    let bs = base.to_string_lossy().to_lowercase().replace('\\', "/");
    let bs = bs.trim_end_matches('/').to_string();
    ps == bs || ps.starts_with(&format!("{bs}/"))
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ExeInfo {
    pub id: String,
    pub path: String,
    pub found: bool,
    pub size: u64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RootInfo {
    pub root: String,
    pub platform: String,
    pub config_path: String,
    pub config_exists: bool,
    pub exes: Vec<ExeInfo>,
    pub config: AppConfig,
}

fn exe_infos(root: &Path, cfg: &AppConfig) -> Vec<ExeInfo> {
    TOOL_IDS
        .iter()
        .map(|id| {
            let p = tool_path(cfg, id);
            let abs = resolve_path(root, &p);
            let size = fs::metadata(&abs).map(|m| m.len()).unwrap_or(0);
            ExeInfo {
                id: id.to_string(),
                path: p,
                found: abs.is_file(),
                size,
            }
        })
        .collect()
}

fn make_root_info(root: &Path, cfg: &AppConfig, config_exists: bool) -> RootInfo {
    RootInfo {
        root: root.to_string_lossy().into_owned(),
        platform: platform_dir().into(),
        config_path: root.join(CONFIG_FILE).to_string_lossy().into_owned(),
        config_exists,
        exes: exe_infos(root, cfg),
        config: cfg.clone(),
    }
}

/// 自动探测的根目录标记：有 res_manager.json，或老布局（0.tool + 1.font2c）
fn is_marked_root(p: &Path) -> bool {
    p.join(CONFIG_FILE).is_file() || (p.join("0.tool").is_dir() && p.join("1.font2c").is_dir())
}

fn auto_detect() -> Option<PathBuf> {
    if let Ok(exe) = std::env::current_exe() {
        if let Some(start) = exe.parent() {
            for dir in start.ancestors() {
                if is_marked_root(dir) {
                    return Some(dir.to_path_buf());
                }
            }
        }
    }
    if let Ok(cwd) = std::env::current_dir() {
        for dir in cwd.ancestors() {
            if is_marked_root(dir) {
                return Some(dir.to_path_buf());
            }
        }
    }
    None
}

fn last_root_file(app: &tauri::AppHandle) -> Option<PathBuf> {
    app.path()
        .app_config_dir()
        .ok()
        .map(|d| d.join("last_root.txt"))
}

fn load_last_root(app: &tauri::AppHandle) -> Option<PathBuf> {
    let f = last_root_file(app)?;
    let s = fs::read_to_string(f).ok()?;
    let p = PathBuf::from(s.trim());
    if p.is_dir() {
        Some(p)
    } else {
        None
    }
}

fn save_last_root(app: &tauri::AppHandle, root: &Path) {
    if let Some(f) = last_root_file(app) {
        if let Some(dir) = f.parent() {
            let _ = fs::create_dir_all(dir);
        }
        let _ = fs::write(f, root.to_string_lossy().as_bytes());
    }
}

fn load_config(root: &Path) -> (AppConfig, bool) {
    let f = root.join(CONFIG_FILE);
    match fs::read_to_string(&f) {
        Ok(text) => match serde_json::from_str::<AppConfig>(&text) {
            Ok(cfg) => (cfg, true),
            Err(_) => (AppConfig::default(), true), // 文件存在但解析失败：回退默认
        },
        Err(_) => (AppConfig::default(), false),
    }
}

fn finish_set_root(
    app: &tauri::AppHandle,
    state: &tauri::State<'_, AppState>,
    root: PathBuf,
) -> Result<RootInfo, String> {
    let root = normalize(&root);
    let (cfg, exists) = load_config(&root);
    save_last_root(app, &root);
    let info = make_root_info(&root, &cfg, exists);
    *state.ctx.lock().unwrap() = Some((root, cfg));
    Ok(info)
}

/// 定位根目录。`use_saved=true` 优先使用上次记住的目录（应用启动时），
/// `use_saved=false` 强制重新自动探测（设置页「重新检测」）。
#[tauri::command]
pub fn locate_tool_root(
    app: tauri::AppHandle,
    state: tauri::State<'_, AppState>,
    use_saved: bool,
) -> Result<RootInfo, String> {
    let mut root = None;
    if use_saved {
        root = load_last_root(&app);
    }
    if root.is_none() {
        root = auto_detect();
    }
    let root = root.ok_or_else(|| {
        "未能自动找到工具链根目录：请把 res_manager 放在工具链目录内运行（目录含 res_manager.json，\
         或含 0.tool 与 1.font2c），或手动选择根目录。"
            .to_string()
    })?;
    finish_set_root(&app, &state, root)
}

/// 手动指定根目录（任意存在的目录都接受；缺省配置随后可在设置页保存）
#[tauri::command]
pub fn set_tool_root(
    app: tauri::AppHandle,
    state: tauri::State<'_, AppState>,
    path: String,
) -> Result<RootInfo, String> {
    let p = PathBuf::from(&path);
    if !p.is_dir() {
        return Err(format!("目录不存在: {path}"));
    }
    finish_set_root(&app, &state, p)
}

/// 保存配置到 <root>/res_manager.json 并热更新
#[tauri::command]
pub fn save_config(
    state: tauri::State<'_, AppState>,
    config: AppConfig,
) -> Result<RootInfo, String> {
    let (root, _) = current_ctx(&state)?;
    let text = serde_json::to_string_pretty(&config).map_err(|e| e.to_string())? + "\n";
    fs::write(root.join(CONFIG_FILE), text).map_err(|e| e.to_string())?;
    let info = make_root_info(&root, &config, true);
    *state.ctx.lock().unwrap() = Some((root, config));
    Ok(info)
}

/// 默认配置（设置页「恢复默认」用）
#[tauri::command]
pub fn default_config() -> AppConfig {
    AppConfig::default()
}

/// 取当前 (根目录, 配置)
pub fn current_ctx(state: &tauri::State<'_, AppState>) -> Result<(PathBuf, AppConfig), String> {
    state
        .ctx
        .lock()
        .unwrap()
        .clone()
        .ok_or_else(|| "尚未定位工具链根目录".to_string())
}

/// 前端传入路径的安全解析：相对路径拼根目录；结果必须落在根目录
/// 或任一配置目录（阶段目录 / 合并来源 / 工具所在目录）内。
pub fn checked_path(state: &tauri::State<'_, AppState>, p: &str) -> Result<PathBuf, String> {
    let (root, cfg) = current_ctx(state)?;
    let abs = resolve_path(&root, p);
    let mut bases: Vec<PathBuf> = vec![root.clone()];
    for d in [&cfg.font_dir, &cfg.img_dir, &cfg.merge_dir] {
        bases.push(resolve_path(&root, d));
    }
    for d in &cfg.merge_inputs {
        bases.push(resolve_path(&root, d));
    }
    for id in TOOL_IDS {
        let e = resolve_path(&root, &tool_path(&cfg, id));
        if let Some(parent) = e.parent() {
            bases.push(parent.to_path_buf());
        }
    }
    if bases.iter().any(|b| path_starts_with_ci(&abs, b)) {
        Ok(abs)
    } else {
        Err(format!("路径不在允许的目录范围内: {p}"))
    }
}

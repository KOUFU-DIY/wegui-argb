mod fonts;
mod fsx;
mod paths;
mod runner;

use paths::AppConfig;
use std::path::PathBuf;
use std::sync::atomic::AtomicBool;
use std::sync::Mutex;

/// 全局状态：(根目录, 配置) + 任务运行互斥标志
pub struct AppState {
    pub ctx: Mutex<Option<(PathBuf, AppConfig)>>,
    pub running: AtomicBool,
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(AppState {
            ctx: Mutex::new(None),
            running: AtomicBool::new(false),
        })
        .invoke_handler(tauri::generate_handler![
            paths::locate_tool_root,
            paths::set_tool_root,
            paths::save_config,
            paths::default_config,
            fsx::list_dir,
            fsx::read_text,
            fsx::read_file_base64,
            fsx::read_hex,
            fsx::stage_stats,
            fsx::open_in_explorer,
            fonts::list_fonts,
            fonts::read_font_base64,
            fonts::resolve_font_file,
            fonts::create_font_config,
            runner::run_task,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

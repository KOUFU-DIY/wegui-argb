//! 批处理任务运行器。
//!
//! 任务步骤用 Rust 复刻 tool 目录下各 .bat 的逻辑（清目录 → 依次调用 exe → 清理临时物），
//! 不直接调用 .bat：避免 GBK 控制台编码问题，同时保持 macOS 可移植性。
//! 所有目录与工具路径都来自 res_manager.json 配置（见 paths.rs），不写死。
//! 与 .bat 一致：单个步骤失败不中断整个任务，只计数并在结尾汇总。

use serde::Serialize;
use std::fs;
use std::io::{BufRead, BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::Ordering;
use tauri::Emitter;

use crate::paths::{current_ctx, path_starts_with_ci, resolve_path, tool_path, AppConfig};
use crate::AppState;

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LogMsg {
    pub stream: String, // "sys" | "out" | "err"
    pub line: String,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DoneMsg {
    pub ok: bool,
    pub task: String,
    pub failed_steps: u32,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RunResult {
    pub ok: bool,
    pub failed_steps: u32,
}

enum Step {
    /// 控制台分节提示
    Note(String),
    /// 确保目录存在
    EnsureDir(PathBuf),
    /// 删除目录下的文件（不递归；ext 为 Some 时只删该扩展名）
    ClearFiles { dir: PathBuf, ext: Option<String> },
    /// 递归删除目录
    RemoveDirAll(PathBuf),
    /// 调用外部取模工具
    Exec {
        cwd: PathBuf,
        exe: PathBuf,
        tool_id: String,
        args: Vec<String>,
    },
}

fn sys(app: &tauri::AppHandle, line: String) {
    let _ = app.emit(
        "tool-log",
        LogMsg {
            stream: "sys".into(),
            line,
        },
    );
}

/// 日志里尽量显示相对根目录的短路径
fn disp(root: &Path, p: &Path) -> String {
    p.strip_prefix(root)
        .map(|r| r.to_string_lossy().replace('\\', "/"))
        .unwrap_or_else(|_| p.to_string_lossy().into_owned())
}

fn decode_line(bytes: &[u8]) -> String {
    let b = bytes.strip_suffix(b"\n").unwrap_or(bytes);
    let b = b.strip_suffix(b"\r").unwrap_or(b);
    match std::str::from_utf8(b) {
        Ok(s) => s.to_string(),
        Err(_) => encoding_rs::GBK.decode(b).0.into_owned(),
    }
}

fn pump_stream(app: tauri::AppHandle, stream: &'static str, r: impl Read) {
    let mut br = BufReader::new(r);
    let mut buf = Vec::new();
    loop {
        buf.clear();
        match br.read_until(b'\n', &mut buf) {
            Ok(0) | Err(_) => break,
            Ok(_) => {
                let line = decode_line(&buf);
                if !line.trim().is_empty() {
                    let _ = app.emit(
                        "tool-log",
                        LogMsg {
                            stream: stream.into(),
                            line,
                        },
                    );
                }
            }
        }
    }
}

fn clear_files(dir: &Path, ext: Option<&str>) -> std::io::Result<u32> {
    if !dir.is_dir() {
        return Ok(0);
    }
    let mut n = 0;
    for e in fs::read_dir(dir)? {
        let p = e?.path();
        if !p.is_file() {
            continue;
        }
        if let Some(x) = ext {
            let hit = p
                .extension()
                .map(|s| s.eq_ignore_ascii_case(x))
                .unwrap_or(false);
            if !hit {
                continue;
            }
        }
        fs::remove_file(&p)?;
        n += 1;
    }
    Ok(n)
}

fn run_exec(
    app: &tauri::AppHandle,
    tool_id: &str,
    exe: &Path,
    cwd: &Path,
    args: &[String],
) -> bool {
    sys(app, format!("▶ {} {}", tool_id, args.join(" ")));
    if !exe.is_file() {
        sys(app, format!("✖ 找不到工具: {}", exe.display()));
        return false;
    }
    let mut cmd = Command::new(exe);
    cmd.args(args)
        .current_dir(cwd)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(e) => {
            sys(app, format!("✖ 启动失败: {e}"));
            return false;
        }
    };
    let err_handle = child.stderr.take().map(|r| {
        let app2 = app.clone();
        std::thread::spawn(move || pump_stream(app2, "err", r))
    });
    if let Some(out) = child.stdout.take() {
        pump_stream(app.clone(), "out", out);
    }
    if let Some(h) = err_handle {
        let _ = h.join();
    }
    match child.wait() {
        Ok(st) if st.success() => true,
        Ok(st) => {
            sys(app, format!("✖ 退出码 {}", st.code().unwrap_or(-1)));
            false
        }
        Err(e) => {
            sys(app, format!("✖ 等待进程失败: {e}"));
            false
        }
    }
}

fn run_steps(app: &tauri::AppHandle, root: &Path, steps: Vec<Step>) -> RunResult {
    let mut failed = 0u32;
    for step in steps {
        match step {
            Step::Note(s) => sys(app, s),
            Step::EnsureDir(dir) => {
                if let Err(e) = fs::create_dir_all(&dir) {
                    sys(app, format!("✖ 创建目录 {} 失败: {e}", disp(root, &dir)));
                    failed += 1;
                }
            }
            Step::ClearFiles { dir, ext } => match clear_files(&dir, ext.as_deref()) {
                Ok(n) => {
                    if n > 0 {
                        let what = ext.map(|x| format!("*.{x}")).unwrap_or_else(|| "*".into());
                        sys(
                            app,
                            format!("· 清理 {} ({what}, {n} 个文件)", disp(root, &dir)),
                        );
                    }
                }
                Err(e) => {
                    sys(app, format!("✖ 清理 {} 失败: {e}", disp(root, &dir)));
                    failed += 1;
                }
            },
            Step::RemoveDirAll(dir) => {
                if dir.is_dir() {
                    if let Err(e) = fs::remove_dir_all(&dir) {
                        sys(app, format!("✖ 删除 {} 失败: {e}", disp(root, &dir)));
                        failed += 1;
                    }
                }
            }
            Step::Exec {
                cwd,
                exe,
                tool_id,
                args,
            } => {
                if !run_exec(app, &tool_id, &exe, &cwd, &args) {
                    failed += 1;
                }
            }
        }
    }
    RunResult {
        ok: failed == 0,
        failed_steps: failed,
    }
}

/// 构建 Exec 步骤的小工具
fn exec_step(root: &Path, cfg: &AppConfig, cwd: &Path, tool_id: &str, args: Vec<String>) -> Step {
    Step::Exec {
        cwd: cwd.to_path_buf(),
        exe: resolve_path(root, &tool_path(cfg, tool_id)),
        tool_id: tool_id.to_string(),
        args,
    }
}

/// 图片取模步骤（复刻 img2c_rgb565.bat / img2c_rgb888.bat，目录来自配置）
fn img_steps(root: &Path, cfg: &AppConfig, is_888: bool, steps: &mut Vec<Step>) {
    let base = resolve_path(root, &cfg.img_dir);
    let (dir, argb, rgb) = if is_888 {
        ("rgb888", "argb8888", "rgb888")
    } else {
        ("rgb565", "argb8565", "rgb565")
    };
    // (工具 id, 前置参数, bucket 基名) —— _2bin 和 _2c 两轮共用
    // alpha 分桶：A8 raw（后续旋转图支持预留）+ A8 indexqoimask（推荐压缩，默认 6bit 量化）
    let fmt_args = |f: &str| vec!["--format".to_string(), f.to_string()];
    let buckets: Vec<(&str, Vec<String>, String)> = vec![
        (
            "img2bin_indexqoi",
            fmt_args(argb),
            format!("input/{dir}/{argb}_indexqoi"),
        ),
        (
            "img2bin_indexqoi",
            fmt_args(rgb),
            format!("input/{dir}/{rgb}_indexqoi"),
        ),
        (
            "img2bin_raw",
            fmt_args(rgb),
            format!("input/{dir}/{rgb}_raw"),
        ),
        (
            "img2bin_raw",
            fmt_args(argb),
            format!("input/{dir}/{argb}_raw"),
        ),
        ("img2bin_raw", fmt_args("a8"), "input/alpha/A8_raw".into()),
        (
            "img2bin_indexqoimask",
            vec!["--quantize-bits".into(), "6".into()],
            "input/alpha/A8_indexqoimask".into(),
        ),
    ];

    steps.push(Step::Note(format!("—— 图片取模 {dir} → 外挂 bin ——")));
    steps.push(Step::ClearFiles {
        dir: base.join("output").join("bin"),
        ext: None,
    });
    steps.push(Step::EnsureDir(base.join("output").join("bin")));
    for (tool, pre_args, bucket) in &buckets {
        let mut args = pre_args.clone();
        args.extend([
            "--input".into(),
            format!("./{bucket}_2bin"),
            "--output".into(),
            "./output/bin".into(),
        ]);
        steps.push(exec_step(root, cfg, &base, tool, args));
    }
    steps.push(Step::ClearFiles {
        dir: base.join("output").join("bin"),
        ext: Some("json".into()),
    });

    steps.push(Step::Note(format!("—— 图片取模 {dir} → 内置 .c/.h ——")));
    steps.push(Step::ClearFiles {
        dir: base.join("output").join("c"),
        ext: None,
    });
    for (tool, pre_args, bucket) in &buckets {
        let mut args = pre_args.clone();
        args.extend([
            "--input".into(),
            format!("./{bucket}_2c"),
            "--output".into(),
            "./output/c/temp".into(),
        ]);
        steps.push(exec_step(root, cfg, &base, tool, args));
    }
    steps.push(exec_step(
        root,
        cfg,
        &base,
        "bin2c",
        vec![
            "--output-c".into(),
            "res_img".into(),
            "--no-size-macro".into(),
            "--input".into(),
            "./output/c/temp".into(),
            "--output-path".into(),
            "./output/c".into(),
        ],
    ));
    steps.push(Step::RemoveDirAll(base.join("output").join("c").join("temp")));
}

/// 字体构建步骤（font2c build-all input -o output，目录来自配置）
fn font_steps(root: &Path, cfg: &AppConfig, steps: &mut Vec<Step>) {
    let base = resolve_path(root, &cfg.font_dir);
    steps.push(Step::Note("—— 字体取模（全部配置） ——".into()));
    steps.push(Step::EnsureDir(base.join("output")));
    steps.push(exec_step(
        root,
        cfg,
        &base,
        "font2c",
        vec![
            "build-all".into(),
            "input".into(),
            "-o".into(),
            "output".into(),
        ],
    ));
}

/// 外挂 bin 合并步骤（合并来源目录来自配置，可任意增减）
fn bin_steps(root: &Path, cfg: &AppConfig, steps: &mut Vec<Step>) {
    let base = resolve_path(root, &cfg.merge_dir);
    let mut inputs: Vec<String> = Vec::new();
    for d in &cfg.merge_inputs {
        inputs.push("--input".into());
        inputs.push(resolve_path(root, d).to_string_lossy().into_owned());
    }
    steps.push(Step::Note("—— 外挂储存 bin 合并 ——".into()));
    steps.push(Step::EnsureDir(base.join("output")));
    steps.push(Step::ClearFiles {
        dir: base.join("output"),
        ext: None,
    });
    let mut args1 = inputs.clone();
    args1.extend(["--output-path".into(), "./output".into()]);
    steps.push(exec_step(root, cfg, &base, "bin2c", args1));

    steps.push(Step::Note("—— 嵌入数据版（供外挂储存烧录工程编译） ——".into()));
    let mut args2 = inputs;
    args2.extend([
        "--embed-data".into(),
        "--output-path".into(),
        "./output/embed".into(),
    ]);
    steps.push(exec_step(root, cfg, &base, "bin2c", args2));
}

fn task_steps(
    root: &Path,
    cfg: &AppConfig,
    task: &str,
    arg: Option<&str>,
) -> Result<Vec<Step>, String> {
    let mut steps = Vec::new();
    match task {
        "font_build_all" => font_steps(root, cfg, &mut steps),
        "font_build_one" => {
            let rel = arg.ok_or("缺少配置文件参数")?;
            let abs = resolve_path(root, rel);
            let font_input = resolve_path(root, &cfg.font_dir).join("input");
            if !path_starts_with_ci(&abs, &font_input)
                || !abs
                    .extension()
                    .map(|e| e.eq_ignore_ascii_case("json"))
                    .unwrap_or(false)
            {
                return Err("参数必须是字体取模目录 input 下的 .json 配置".into());
            }
            let base = resolve_path(root, &cfg.font_dir);
            let fname = abs
                .file_name()
                .map(|s| s.to_string_lossy().into_owned())
                .unwrap_or_default();
            steps.push(Step::Note(format!("—— 字体取模（{fname}） ——")));
            steps.push(Step::EnsureDir(base.join("output")));
            steps.push(exec_step(
                root,
                cfg,
                &base,
                "font2c",
                vec![
                    "build".into(),
                    abs.to_string_lossy().into_owned(),
                    "-o".into(),
                    "output".into(),
                ],
            ));
        }
        "img_rgb565" => img_steps(root, cfg, false, &mut steps),
        "img_rgb888" => img_steps(root, cfg, true, &mut steps),
        "bin_merge" => bin_steps(root, cfg, &mut steps),
        "pipeline_all" => {
            steps.push(Step::Note(
                "━━ 一键构建：字体 → 图片(RGB565) → bin 合并 ━━".into(),
            ));
            font_steps(root, cfg, &mut steps);
            img_steps(root, cfg, false, &mut steps);
            bin_steps(root, cfg, &mut steps);
        }
        other => return Err(format!("未知任务: {other}")),
    }
    Ok(steps)
}

#[tauri::command]
pub async fn run_task(
    app: tauri::AppHandle,
    state: tauri::State<'_, AppState>,
    task: String,
    arg: Option<String>,
) -> Result<RunResult, String> {
    let (root, cfg) = current_ctx(&state)?;
    if state
        .running
        .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
        .is_err()
    {
        return Err("已有任务在运行，请等待完成".into());
    }
    let steps = match task_steps(&root, &cfg, &task, arg.as_deref()) {
        Ok(s) => s,
        Err(e) => {
            state.running.store(false, Ordering::SeqCst);
            return Err(e);
        }
    };
    let app2 = app.clone();
    let root2 = root.clone();
    let joined = tauri::async_runtime::spawn_blocking(move || run_steps(&app2, &root2, steps)).await;
    state.running.store(false, Ordering::SeqCst);
    let result = joined.map_err(|e| e.to_string())?;
    if result.ok {
        sys(&app, "✔ 任务完成".into());
    } else {
        sys(&app, format!("✖ 任务结束，{} 个步骤失败", result.failed_steps));
    }
    let _ = app.emit(
        "tool-done",
        DoneMsg {
            ok: result.ok,
            task,
            failed_steps: result.failed_steps,
        },
    );
    Ok(result)
}

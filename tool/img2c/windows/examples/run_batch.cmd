@echo off
setlocal
rem =====================================================================
rem 自动批处理示例：处理仓库根目录下的 input2* 文件夹
rem
rem 1. 把图片放进根目录的 input2raw / input2rle / input2imprle /
rem    input2qoi / input2qoif / input2indexqoi 等文件夹
rem 2. 双击本脚本
rem 3. 结果输出到根目录 output\ (含 .bin、img_resources.c/.h、manifest)
rem
rem 参数说明见: ..\img2bin_pack.exe --help
rem 自定义配置示例见: img2bin_pack.example.json
rem =====================================================================

"%~dp0..\img2bin_pack.exe" --root "%~dp0..\.."

echo.
pause

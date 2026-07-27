@echo off
setlocal
set "PACK=%~dp0..\img2bin_pack.exe"
set "ROOT=%~dp0..\.."
set "BIN2C=%~dp0..\tools\bin2c.exe"
if not exist "%BIN2C%" set "BIN2C=E:\OneDrive\APP\bin2c\bin2c.exe"
rem =====================================================================
rem 预设: 取模 + 合并资源包 (配合独立的 bin2c 工具)
rem
rem 第一步: 全部 input2* 转出散 .bin (不生成 .c/.h)
rem 第二步: bin2c 把 output 里的散 .bin 合并成单一资源包:
rem         output\merged\res_pack.bin  烧写用合并包
rem         output\merged\res_pack.c/.h 单数组 + ID 枚举 + 地址表
rem
rem 注意: bin2c 的 res_pack.h 与 pack 的 img_resources.h 符号同源,
rem       下位机二选一 include, 不要同时包含
rem 注意: bin2c 输出放在 output\merged 子目录, 避免重复运行时
rem       把上次的合并包自己也合并进去
rem =====================================================================

if not exist "%BIN2C%" (
  echo bin2c.exe not found. Edit the BIN2C path in this script.
  pause
  exit /b 1
)

"%PACK%" --root "%ROOT%" --emit bin
"%BIN2C%" --input "%ROOT%\output" --output-path "%ROOT%\output\merged" --output-name res_pack

echo.
pause

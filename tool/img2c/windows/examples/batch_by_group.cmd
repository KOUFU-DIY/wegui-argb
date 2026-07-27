@echo off
setlocal
set "PACK=%~dp0..\img2bin_pack.exe"
set "ROOT=%~dp0..\.."
rem =====================================================================
rem 预设: 分批用不同参数处理, 每行一组文件夹 + 一组参数
rem
rem 第一批: raw/rle/imprle 转 rgb565, 只出 .bin
rem 第二批: qoi/qoif/indexqoi 转 argb8888, 并在最后生成 .c/.h
rem
rem 说明: codegen 扫描的是输出目录里全部合规 .bin, 所以只需要
rem 最后一批带 --emit ch, 两批的资源都会进 img_resources.c/.h
rem =====================================================================

"%PACK%" --root "%ROOT%" --folders input2raw,input2rle,input2imprle --format rgb565 --emit bin
"%PACK%" --root "%ROOT%" --folders input2qoi,input2qoif,input2indexqoi --format argb8888 --index-interval 512 --emit ch

echo.
pause

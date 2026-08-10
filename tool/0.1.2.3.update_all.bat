@echo off
:: 一键资源流水线：字体 -> 图片(rgb565) -> 外挂合并。各步也可单独双击运行。
:: 本仓库三个构建目标均为 RGB565；RGB888 目标请单独运行 2.img2c\img2c_rgb888.bat。
pushd 1.font2c
call 2.build.bat
popd
pushd 2.img2c
call img2c_rgb565.bat
popd
pushd 3.bin2c
call build_bin.bat
popd
echo.
echo update_all done.
pause

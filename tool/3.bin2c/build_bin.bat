@echo off

:: 清空output目录
del /q ".\output\*.*"

:: 合并字体bin图片bin
..\0.tool\windows\bin2c\bin2c.exe --input ..\1.font2c\output --input ..\2.img2c\output\bin --output-path .\output

:: 嵌数据版（供 4.STM32F103_ex_flash_download 烧录工程编译打包，应用工程勿用）
..\0.tool\windows\bin2c\bin2c.exe --input ..\1.font2c\output --input ..\2.img2c\output\bin --embed-data --output-path .\output\embed
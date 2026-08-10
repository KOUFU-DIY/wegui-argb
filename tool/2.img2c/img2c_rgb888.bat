@echo off

:: 清空output\bin目录
del /q ".\output\bin\*.*"
:: 图片取bin
..\0.tool\windows\img2bin_indexqoi\img2bin_indexqoi.exe --format argb8888 --input .\input\rgb888\argb8888_indexqoi_2bin --output .\output\bin
..\0.tool\windows\img2bin_indexqoi\img2bin_indexqoi.exe --format rgb888 --input .\input\rgb888\rgb888_indexqoi_2bin --output .\output\bin
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format rgb888 --input .\input\rgb565\rgb888_raw_2bin --output .\output\bin
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format argb8888 --input .\input\rgb888\argb8888_raw_2bin --output .\output\bin
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format a1 --input .\input\alpha\a1_raw_2bin --output .\output\bin
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format a2 --input .\input\alpha\a2_raw_2bin --output .\output\bin
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format a4 --input .\input\alpha\a4_raw_2bin --output .\output\bin
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format a8 --input .\input\alpha\a8_raw_2bin --output .\output\bin
:: 清空取模bin的日志
del /q ".\output\bin\*.json"

:: 清空output\c目录
del /q ".\output\c\*.*"
:: 图片取bin到temp临时目录
..\0.tool\windows\img2bin_indexqoi\img2bin_indexqoi.exe --format argb8888 --input .\input\rgb888\argb8888_indexqoi_2c --output .\output\c\temp
..\0.tool\windows\img2bin_indexqoi\img2bin_indexqoi.exe --format rgb888 --input .\input\rgb888\rgb888_indexqoi_2c --output .\output\c\temp
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format rgb888 --input .\input\rgb888\rgb888_raw_2c --output .\output\c\temp
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format argb8888 --input .\input\rgb888\argb8888_raw_2c --output .\output\c\temp
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format a1 --input .\input\alpha\a1_raw_2c --output .\output\c\temp
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format a2 --input .\input\alpha\a2_raw_2c --output .\output\c\temp
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format a4 --input .\input\alpha\a4_raw_2c --output .\output\c\temp
..\0.tool\windows\img2bin_raw\img2bin_raw.exe --format a8 --input .\input\alpha\a8_raw_2c --output .\output\c\temp
:: 将temp下的bin转换成单个.c.h
..\0.tool\windows\bin2c\bin2c.exe --output-c res_img --no-size-macro --input .\output\c\temp --output-path .\output\c
:: 清除temp临时目录
rd /s /q ".\output\c\temp"

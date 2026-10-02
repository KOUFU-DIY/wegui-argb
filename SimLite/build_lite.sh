#!/bin/sh
# SimLite 轻量模拟器构建（macOS / Linux）
#   macOS: 系统 clang + Cocoa（xcode command line tools 即可）
#   Linux: cc + libx11-dev
# 用法: sh SimLite/build_lite.sh          正式版 wegui_lite（demo 由 main_lite.c 的 DEMO_ID 宏决定）
#       sh SimLite/build_lite.sh --dev    开发者版 wegui_lite_dev（需本地 SimLite/debug/，不随仓库发布）
set -e
LITE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(dirname "$LITE")"
BUILD="$LITE/build"
mkdir -p "$BUILD"

DEV=0
for a in "$@"; do
  [ "$a" = "--dev" ] && DEV=1
done

# 正式版：SimLite 顶层 .c；开发版：换用 debug/ 下的入口与注册表
if [ "$DEV" = "1" ] && [ ! -f "$LITE/debug/debug_main.c" ]; then
  echo "开发者版源码 SimLite/debug/ 仅本地维护、不随仓库发布；请去掉 --dev 构建正式版" >&2
  exit 1
fi
if [ "$DEV" = "1" ]; then
  LITE_SRCS="$(find "$LITE" -maxdepth 1 -name '*.c' ! -name 'main_lite.c')
$(find "$LITE/debug" -maxdepth 1 -name '*.c')"
  OUT="$BUILD/wegui_lite_dev"
else
  LITE_SRCS="$(find "$LITE" -maxdepth 1 -name '*.c')"
  OUT="$BUILD/wegui_lite"
fi

SRCS="$LITE_SRCS
$(find "$REPO/Core" -maxdepth 1 -name '*.c' ! -name '*_bckup.c')
$(find "$REPO/Core/components" -maxdepth 1 -name '*.c')
$(find "$REPO/Core/widgets" -mindepth 2 -maxdepth 2 -name '*.c' ! -name '*_bckup.c')
$(find "$REPO/Core/widgets_preview" -mindepth 2 -maxdepth 2 -name '*.c')
$(find "$REPO/Demo/preview" -maxdepth 1 -name '*.c')"

# 稳定 demo 清单唯一事实源 Demo/demo_manifest.txt（新增 demo 只改清单一处；模拟器编译全部条目）
cr=$(printf '\r')
while read -r f _tags; do
  f=${f%"$cr"}
  case "$f" in *.c) SRCS="$SRCS
$REPO/Demo/$f" ;; esac
done < "$REPO/Demo/demo_manifest.txt"

SRCS="$SRCS
$REPO/tool/1.font2c/output/simli_16_2bpp.c
$REPO/tool/1.font2c/output/simsun_12_2bpp.c
$REPO/tool/1.font2c/output/msyh_16_4bpp_ime.c
$REPO/tool/1.font2c/output/gbsn00lp_2_16_4bpp.c
$REPO/tool/2.img2c/output/c/res_img.c
$REPO/tool/3.bin2c/output/merged_bin.c"

case "$(uname)" in
  Darwin) LIBS="-framework Cocoa" ;;
  *)      LIBS="-lX11" ;;
esac

# shellcheck disable=SC2086
cc -O2 -DWE_SIMULATOR \
   -I"$LITE" -I"$REPO" -I"$REPO/Core" -I"$REPO/Core/widgets" \
   -I"$REPO/Demo" -I"$REPO/Demo/preview" \
   -I"$REPO/tool/1.font2c/output" -I"$REPO/tool/2.img2c/output/c" -I"$REPO/tool/3.bin2c/output" \
   $SRCS $LIBS -lm -o "$OUT"

cp -f "$REPO/tool/3.bin2c/output/merged_bin.bin" "$BUILD/" 2>/dev/null || true
echo "built: $OUT"

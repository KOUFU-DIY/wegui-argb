#!/bin/sh
# SimLite 轻量模拟器构建（macOS / Linux）
#   macOS: 系统 clang + Cocoa（xcode command line tools 即可）
#   Linux: cc + libx11-dev
# 用法: sh SimLite/build_lite.sh          正式版 wegui_lite（demo 由 main_lite.c 的 DEMO_ID 宏决定）
#       sh SimLite/build_lite.sh --dev    开发者版 wegui_lite_dev（运行时选 demo / --shot / --list）
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
$(find "$REPO/Core/widgets" -mindepth 2 -maxdepth 2 -name '*.c' ! -name '*_bckup.c')
$(find "$REPO/Core/widgets_preview" -mindepth 2 -maxdepth 2 -name '*.c')
$(find "$REPO/Demo/preview" -maxdepth 1 -name '*.c')"

for d in demo_showcase demo_common demo_label demo_btn demo_img demo_img_alpha demo_img_ex demo_arc \
         demo_concentric_arc demo_group demo_slideshow demo_checkbox demo_label_ex demo_chart \
         demo_toggle demo_progress demo_msgbox demo_flash_img demo_flash_font demo_slider \
         demo_scroll_panel demo_dropdown demo_stepper demo_indicator demo_line demo_box \
         demo_gauge demo_list demo_roller demo_marquee demo_toast demo_focus demo_focus2 \
         demo_imgbtn demo_segdisp; do
  SRCS="$SRCS
$REPO/Demo/$d.c"
done

SRCS="$SRCS
$REPO/tool/1.font2c/output/simli_16_2bpp.c
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

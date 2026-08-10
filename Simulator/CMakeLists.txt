cmake_minimum_required(VERSION 3.10)
project(WeGui_ARGB_Simulator)

set(CMAKE_C_STANDARD 11)

add_definitions(-DWE_SIMULATOR)

set(SDL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/SDL2")

include_directories(
    "${SDL2_ROOT}/include"
    "${SDL2_ROOT}/x86_64-w64-mingw32/include"
    "${SDL2_ROOT}/x86_64-w64-mingw32/include/SDL2"
    ".."
    "../Core"
    "../Core/widgets"
    "../Demo"
    "../Demo/preview"
    "../tool/1.font2c/output"
    "../tool/2.img2c/output/c"
    "../tool/3.bin2c/output"
    "."
)

if(MSVC)
    link_directories("${SDL2_ROOT}/lib/x64")
    set(SDL2_LIBS "SDL2.lib" "SDL2main.lib" "shell32.lib")
else()
    link_directories(
        "${SDL2_ROOT}/x86_64-w64-mingw32/lib"
        "${SDL2_ROOT}/lib"
    )
    set(SDL2_LIBS "mingw32" "SDL2main" "SDL2")
endif()

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "../Core/*.c" "../Core/widgets/*/*.c")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*_bckup\\.c$")
set(DEMO_SOURCES
    "../Demo/demo_common.c"
    "../Demo/demo_label.c"
    "../Demo/demo_btn.c"
    "../Demo/demo_img.c"
    "../Demo/demo_img_alpha.c"
    "../Demo/demo_img_ex.c"
    "../Demo/demo_arc.c"
    "../Demo/demo_concentric_arc.c"
    "../Demo/demo_group.c"
    "../Demo/demo_slideshow.c"
    "../Demo/demo_checkbox.c"
    "../Demo/demo_label_ex.c"
    "../Demo/demo_chart.c"
    "../Demo/demo_toggle.c"
    "../Demo/demo_progress.c"
    "../Demo/demo_msgbox.c"
    "../Demo/demo_flash_img.c"
    "../Demo/demo_flash_font.c"
    "../Demo/demo_slider.c"
    "../Demo/demo_scroll_panel.c"
    "../Demo/demo_dropdown.c"
    "../Demo/demo_stepper.c"
    "../Demo/demo_indicator.c"
    "../Demo/demo_line.c"
    "../Demo/demo_box.c"
    "../Demo/demo_gauge.c"
    "../Demo/demo_list.c"
    "../Demo/demo_roller.c"
    "../Demo/demo_marquee.c"
    "../Demo/demo_toast.c"
    "../Demo/demo_focus.c"
    "../Demo/demo_focus2.c"
    "../Demo/demo_showcase.c"
    "../tool/1.font2c/output/simli_16_2bpp.c"
    "../tool/1.font2c/output/msyh_16_4bpp_ime.c"
    "../tool/1.font2c/output/gbsn00lp_2_16_4bpp.c"
    "../tool/2.img2c/output/c/res_img.c"
    "../tool/3.bin2c/output/merged_bin.c"
)
file(GLOB SIMULATOR_SOURCES CONFIGURE_DEPENDS "*.c")

# preview 孵化区：实验控件与 demo（仅模拟器编译，不进 Keil 工程，随时可下架）
file(GLOB PREVIEW_CORE_SOURCES CONFIGURE_DEPENDS "../Core/widgets_preview/*/*.c")
file(GLOB PREVIEW_DEMO_SOURCES CONFIGURE_DEPENDS "../Demo/preview/*.c")

add_executable(wegui_sim
    ${CORE_SOURCES}
    ${DEMO_SOURCES}
    ${PREVIEW_CORE_SOURCES}
    ${PREVIEW_DEMO_SOURCES}
    ${SIMULATOR_SOURCES}
)

# autotest：允许命令行覆盖 DEMO_ID（main_sim.c 用 #ifndef 守卫），
# autotest.ps1 借此逐 demo 重建并跑 headless 基准哈希比对。
if(DEFINED WE_DEMO_ID)
    target_compile_definitions(wegui_sim PRIVATE DEMO_ID=${WE_DEMO_ID})
endif()

# 回归脚手架编译开关（sim_autotest.h 默认 0，日常构建不含回归测试代码）：
# autotest.ps1 配置时传 -DWE_SIM_AUTOTEST=1 打开。
if(DEFINED WE_SIM_AUTOTEST)
    target_compile_definitions(wegui_sim PRIVATE WE_SIM_AUTOTEST=${WE_SIM_AUTOTEST})
endif()

if(MSVC)
    target_link_libraries(wegui_sim ${SDL2_LIBS})
    set_target_properties(wegui_sim PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
else()
    target_link_libraries(wegui_sim ${SDL2_LIBS})
    target_link_options(wegui_sim PRIVATE -mwindows)
endif()

if(MSVC)
    set(DLL_SRC "${SDL2_ROOT}/lib/x64/SDL2.dll")
else()
    if(EXISTS "${SDL2_ROOT}/x86_64-w64-mingw32/bin/SDL2.dll")
        set(DLL_SRC "${SDL2_ROOT}/x86_64-w64-mingw32/bin/SDL2.dll")
    elseif(EXISTS "${SDL2_ROOT}/lib/SDL2.dll")
        set(DLL_SRC "${SDL2_ROOT}/lib/SDL2.dll")
    elseif(EXISTS "${SDL2_ROOT}/bin/SDL2.dll")
        set(DLL_SRC "${SDL2_ROOT}/bin/SDL2.dll")
    endif()
endif()

add_custom_command(TARGET wegui_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${DLL_SRC}"
    $<TARGET_FILE_DIR:wegui_sim>
)

# 外挂 flash 镜像：拷到 exe 旁，运行时由 sdl_port 存储端口直接 fopen 读取
# （硬件对应"烧录 W25Qxx"；改资产后重跑 tool/3.bin2c 再重启模拟器即可，无需重编译）
add_custom_command(TARGET wegui_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../tool/3.bin2c/output/merged_bin.bin"
    "$<TARGET_FILE_DIR:wegui_sim>")

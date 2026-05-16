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
    "../Demo/img_ex_perf"
    "../Demo/ex_flash_res"
    "../tool/bin2c/output"
    "../tool/font2c/output"
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
    "../tool/font2c/output/arial_16_4bbp.c"
    "../tool/font2c/output/msyh_16_4bbp_ascii_menu_mix.c"
    "../tool/font2c/output/msyh_24_2bbp_ascii_menu_mix.c"
    "../tool/bin2c/output/merged_bin.c"
)
file(GLOB DEMO_EXTRA_SOURCES CONFIGURE_DEPENDS "../Demo/img_ex_perf/*.c")
list(APPEND DEMO_SOURCES ${DEMO_EXTRA_SOURCES})
file(GLOB SIMULATOR_SOURCES CONFIGURE_DEPENDS "*.c")

add_executable(wegui_sim
    ${CORE_SOURCES}
    ${DEMO_SOURCES}
    ${SIMULATOR_SOURCES}
)

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

cmake_minimum_required(VERSION 3.20)
project(tessara_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PANEL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FW "${PANEL_ROOT}/firmware")
set(LVGL_DIR "${PANEL_ROOT}/.pio/libdeps/jc3248w535/lvgl")
set(ARDUINOJSON_DIR "${PANEL_ROOT}/.pio/libdeps/jc3248w535/ArduinoJson")

if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
  message(FATAL_ERROR "LVGL not found at ${LVGL_DIR}. Run: cd .. && .venv/bin/pio pkg install -e jc3248w535")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(FREETYPE REQUIRED freetype2)

add_compile_definitions(LV_CONF_INCLUDE_SIMPLE SIM_HOST_FONTS)
include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${FW}/components"
  "${FW}/components/core_services"
  "${FW}/components/ui_kit"
  "${FW}/components/dashboard_shell"
  "${FW}/components/widget_sdk"
  "${FW}/components/widget_runtime"
  "${FW}/widgets/todoist"
  "${FW}/widgets/weather"
  "${FW}/widgets/aitokens"
  "${FW}/widgets/clock"
  "${FW}/components/widget_sdk"
  "${LVGL_DIR}"
  "${LVGL_DIR}/src"
  "${ARDUINOJSON_DIR}/src"
  ${SDL2_INCLUDE_DIRS}
  ${FREETYPE_INCLUDE_DIRS}
)

# LVGL sources (core + SDL drivers + software draw)
file(GLOB_RECURSE LVGL_SOURCES
  "${LVGL_DIR}/src/*.c"
)
# Drop demos / examples if any slipped in
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/demos/.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/examples/.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_draw_vglite.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_draw_pxp.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_draw_dave2d.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_draw_vg_lite.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_draw_g2d.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_draw_dma2d.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_opengles.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_windows.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_wayland.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_glfw.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_drm.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_fbdev.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_evdev.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_libinput.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_nuttx.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_x11.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_qnx.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/lv_uefi.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/thorvg/.*")
# Most optional image codecs stay out; bin_decoder is required by lv_init().
# FreeType sources are re-added for host Portuguese glyphs.
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/libs/.*")
list(APPEND LVGL_SOURCES
  "${LVGL_DIR}/src/libs/bin_decoder/lv_bin_decoder.c"
  "${LVGL_DIR}/src/libs/freetype/lv_freetype.c"
  "${LVGL_DIR}/src/libs/freetype/lv_freetype_glyph.c"
  "${LVGL_DIR}/src/libs/freetype/lv_freetype_image.c"
  "${LVGL_DIR}/src/libs/freetype/lv_freetype_outline.c"
  "${LVGL_DIR}/src/libs/freetype/lv_ftsystem.c"
)

add_executable(painel
  src/main.cpp
  src/scenario.cpp
  src/sim_detail.cpp
  src/sim_app.cpp
  src/sim_fonts.cpp
  host/daytime_host.cpp
  host/power_host.cpp
  host/settings_host.cpp
  host/log_host.cpp
  host/sd_host_stub.cpp
  "${FW}/components/widget_runtime/tessara/runtime.cpp"
  "${FW}/components/core_services/platform/device_resources.cpp"
  "${FW}/components/core_services/domain/dashboard_layout.cpp"
  "${FW}/components/ui_kit/ui/palette.cpp"
  "${FW}/components/ui_kit/ui/kit.cpp"
  "${FW}/components/ui_kit/ui/wifi_icons.cpp"
  "${FW}/components/ui_kit/ui/wifi_icons_data.cpp"
  "${FW}/components/ui_kit/ui/bitmaps.cpp"
  "${FW}/components/dashboard_shell/ui/brand_mark.cpp"
  "${FW}/components/dashboard_shell/ui/home.cpp"
  "${FW}/components/dashboard_shell/ui/settings_panel.cpp"
  "${FW}/components/dashboard_shell/ui/todoist_panel.cpp"
  "${FW}/components/dashboard_shell/ui/task_page.cpp"
  "${FW}/components/core_services/domain/task_store.cpp"
  "${FW}/components/core_services/services/text.cpp"
  "${FW}/widgets/todoist/todoist/snapshot.cpp"
  "${FW}/widgets/weather/weather/codes.cpp"
  "${FW}/widgets/weather/ui/weather_page.cpp"
  "${FW}/widgets/weather/ui/weather_icons.cpp"
  "${FW}/widgets/weather/ui/weather_icons_data.cpp"
  "${FW}/widgets/aitokens/ui/aitokens_page.cpp"
  "${FW}/widgets/aitokens/aitokens/snapshot.cpp"
  "${FW}/widgets/aitokens/ui/aitokens_logos.cpp"
  "${FW}/widgets/aitokens/ui/aitokens_logos_data.cpp"
  "${FW}/widgets/clock/ui/clock_page.cpp"
  "${FW}/widgets/clock/clock_summary.cpp"
  "${LVGL_DIR}/src/libs/qrcode/qrcodegen.c"
  ${LVGL_SOURCES}
)

target_compile_options(painel PRIVATE ${SDL2_CFLAGS_OTHER} ${FREETYPE_CFLAGS_OTHER} -Wall -Wno-unused-parameter)
target_link_libraries(painel PRIVATE ${SDL2_LIBRARIES} ${FREETYPE_LIBRARIES})
target_link_directories(painel PRIVATE ${SDL2_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS})

# Copy fixtures next to binary for default --fixtures resolution
add_custom_command(TARGET painel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
          "$<TARGET_FILE_DIR:painel>/fixtures"
)

cmake_minimum_required(VERSION 3.20)
project(wilmot_warehouse_zh VERSION 0.4.0 LANGUAGES C CXX)
option(WILMZH_LEGACY_LOADER "Build wilmzh.dll for an existing v0.2.0 installation" OFF)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This mod targets 64-bit Windows only.")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/runtime_table.h")
add_custom_command(OUTPUT "${GENERATED}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_table.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/translations/zh-CN.json" "${GENERATED}"
  DEPENDS tools/generate_table.py translations/zh-CN.json VERBATIM)
add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)
add_library(wilmzh SHARED src/runtime_mod.cpp src/version_proxy.cpp src/font_overlay.cpp src/help_overlay.cpp
  src/version.def third_party/lodepng/lodepng.cpp "${GENERATED}")
target_include_directories(wilmzh PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" third_party/lodepng)
target_compile_features(wilmzh PRIVATE cxx_std_17)
target_link_libraries(wilmzh PRIVATE minhook user32 opengl32)
set_target_properties(wilmzh PROPERTIES PREFIX "" OUTPUT_NAME "version")
if(WILMZH_LEGACY_LOADER)
  target_compile_definitions(wilmzh PRIVATE WILMZH_LEGACY_LOADER=1)
  set_target_properties(wilmzh PROPERTIES OUTPUT_NAME "wilmzh")
endif()
if(MINGW)
  target_link_options(wilmzh PRIVATE -static -static-libgcc -static-libstdc++)
elseif(MSVC)
  set_property(TARGET minhook wilmzh PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(wilmzh PRIVATE /utf-8)
endif()
install(TARGETS wilmzh RUNTIME DESTINATION WilmotPC LIBRARY DESTINATION WilmotPC)
install(FILES assets/wilmzh-font.wzh assets/wilmzh-help.wzh DESTINATION WilmotPC)

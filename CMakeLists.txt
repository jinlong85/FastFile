cmake_minimum_required(VERSION 3.16)
project(FastFile VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Unicode Win32 (required by DuiLib)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

# ---- Vendored DuiLib (static, offline-friendly) ----
set(DUILIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/duilib/DuiLib")
if(NOT EXISTS "${DUILIB_ROOT}/UIlib.h")
  message(FATAL_ERROR "DuiLib sources missing under third_party/duilib/DuiLib — re-run vendor step.")
endif()

file(GLOB DUILIB_ROOT_SRC    "${DUILIB_ROOT}/*.cpp")
file(GLOB DUILIB_CONTROL_SRC "${DUILIB_ROOT}/Control/*.cpp")
list(FILTER DUILIB_CONTROL_SRC EXCLUDE REGEX "UI(Flash|WebBrowser)[.]cpp$")
file(GLOB DUILIB_CORE_SRC    "${DUILIB_ROOT}/Core/*.cpp")
file(GLOB DUILIB_LAYOUT_SRC  "${DUILIB_ROOT}/Layout/*.cpp")
file(GLOB DUILIB_UTILS_SRC   "${DUILIB_ROOT}/Utils/*.cpp")

add_library(duilib STATIC
  ${DUILIB_ROOT_SRC}
  ${DUILIB_CONTROL_SRC}
  ${DUILIB_CORE_SRC}
  ${DUILIB_LAYOUT_SRC}
  ${DUILIB_UTILS_SRC}
)
target_include_directories(duilib PUBLIC
  "${DUILIB_ROOT}"
  "${DUILIB_ROOT}/Control"
  "${DUILIB_ROOT}/Core"
  "${DUILIB_ROOT}/Layout"
  "${DUILIB_ROOT}/Utils"
)
target_compile_definitions(duilib PUBLIC UILIB_STATIC)
target_link_libraries(duilib PUBLIC comctl32 gdiplus imm32 ole32 uuid)

if(MSVC)
  target_compile_options(duilib PRIVATE /W3 /MP /source-charset:.936)
  # DuiLib historically uses older CRT patterns; keep defaults lenient for P0
endif()

# ---- FastFile app ----
add_executable(FastFile WIN32
  src/main.cpp
  src/MainWnd.cpp
  src/MainWnd.Dpi.cpp
  src/MainWnd.Theme.cpp
  src/MainWnd.Util.cpp
  src/MainWnd.Nav.cpp
  src/MainWnd.Tree.cpp
  src/MainWnd.Tabs.cpp
  src/MainWnd.Favorites.cpp
  src/MainWnd.FileOps.cpp
  src/MainWnd.Menus.cpp
  src/MainWnd.Integration.cpp
  src/MainWnd.Views.cpp
  src/MainWnd.Icons.cpp
  src/TabStripUI.cpp
  src/TabStripUI.h
  src/MainWnd.Preview.cpp
  src/MainWnd.DragDrop.cpp
  src/MainWnd.h
  src/MainWndInternal.h
  src/UiTokens.h
  cmake/FastFile.manifest
)

# App icon (res\FastFile.rc embeds res\FastFile.ico as resource id 1)
if(WIN32)
  target_sources(FastFile PRIVATE res/FastFile.rc)
  # rc.exe resolves the .ico at compile time, but MSBuild only watches the .rc file —
  # without this, swapping the icon would silently keep the old one in the exe.
  set_property(SOURCE res/FastFile.rc APPEND PROPERTY
               OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/FastFile.ico")
endif()
target_link_libraries(FastFile PRIVATE duilib shell32 shlwapi propsys)
if(MSVC)
  target_compile_options(FastFile PRIVATE /W3 /utf-8)
  # Emit FastFile.map: a crash report gives an RVA, the map turns it into a function name.
  target_link_options(FastFile PRIVATE /MAP)
  set_target_properties(FastFile PROPERTIES
    VS_DPI_AWARE "PerMonitor"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:FastFile>"
  )
endif()

# Copy skin XML next to the exe so CPaintManagerUI can load it at runtime
add_custom_command(TARGET FastFile POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/skin"
          "$<TARGET_FILE_DIR:FastFile>/skin"
  COMMENT "Copying skin resources next to FastFile.exe"
)

message(STATUS "FastFile P0 scaffold — DuiLib static + Win32 GUI")

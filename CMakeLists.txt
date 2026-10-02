cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FASTFILE_VERSION LIMIT_COUNT 1)
string(STRIP "${FASTFILE_VERSION}" FASTFILE_VERSION)
if(NOT FASTFILE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain a semantic version such as 1.0.9")
endif()
project(FastFile VERSION "${FASTFILE_VERSION}" LANGUAGES CXX)

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
  src/MainWnd.Settings.cpp
  src/MainWnd.Integration.cpp
  src/MainWnd.Views.cpp
  src/MainWnd.Icons.cpp
  src/FluentScrollBarUI.cpp
  src/FluentScrollBarUI.h
  src/TabStripUI.cpp
  src/TabStripUI.h
  src/MainWnd.Preview.cpp
  src/MainWnd.DragDrop.cpp
  src/ShellBrowserHost.cpp
  src/ShellBrowserHost.h
  src/ShellFileOperation.cpp
  src/ShellFileOperation.h
  src/ShellMenuUtil.h
  src/MainWnd.h
  src/MainWndInternal.h
  src/FastFileCore.h
  src/UiTokens.h
  src/FavoriteStarUI.h
  src/FavoritesJson.h
  src/ShellPresentation.h
  src/TabLayout.h
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
target_link_libraries(FastFile PRIVATE duilib shell32 shlwapi propsys msimg32)
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

include(CTest)
if(BUILD_TESTING)
  get_target_property(FASTFILE_REGRESSION_SOURCES FastFile SOURCES)
  list(FILTER FASTFILE_REGRESSION_SOURCES EXCLUDE REGEX "(^src/main[.]cpp$|^res/|^cmake/)")
  add_executable(FastFileMainWndRegressionTests tests/MainWndRegressionTests.cpp ${FASTFILE_REGRESSION_SOURCES})
  target_include_directories(FastFileMainWndRegressionTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(FastFileMainWndRegressionTests PRIVATE duilib shell32 shlwapi propsys msimg32)
  if(MSVC)
    target_compile_options(FastFileMainWndRegressionTests PRIVATE /utf-8 /wd4828 /MP)
  endif()
  add_dependencies(FastFileMainWndRegressionTests FastFile)
  add_test(NAME FastFileMainWndRegressionTests COMMAND FastFileMainWndRegressionTests)
  set_tests_properties(FastFileMainWndRegressionTests PROPERTIES TIMEOUT 180)
  add_test(NAME FastFileUiPolishTests COMMAND FastFileMainWndRegressionTests --ui-polish-only)
  set_tests_properties(FastFileUiPolishTests PROPERTIES TIMEOUT 30)
  add_test(NAME FastFileShellActivationTests COMMAND FastFileMainWndRegressionTests --shell-activation-only)
  set_tests_properties(FastFileShellActivationTests PROPERTIES TIMEOUT 60)
  add_executable(FastFileFavoritesTests tests/FavoritesTests.cpp)
  target_include_directories(FastFileFavoritesTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(FastFileFavoritesTests PRIVATE duilib gdiplus)
  if(MSVC)
    target_compile_options(FastFileFavoritesTests PRIVATE /utf-8 /wd4828)
  endif()
  add_test(NAME FastFileFavoritesTests COMMAND FastFileFavoritesTests)
  add_executable(FastFileCoreTests tests/FastFileCoreTests.cpp)
  target_include_directories(FastFileCoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME FastFileCoreTests COMMAND FastFileCoreTests)
  add_executable(FastFileShellBrowserTests
    tests/ShellBrowserHostTests.cpp
    src/ShellBrowserHost.cpp
  )
  target_include_directories(FastFileShellBrowserTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(FastFileShellBrowserTests PRIVATE shell32 shlwapi ole32 uuid propsys comctl32 msimg32)
  add_test(NAME FastFileShellBrowserTests COMMAND FastFileShellBrowserTests)
  add_test(NAME FastFileVersionSource
    COMMAND "${CMAKE_COMMAND}"
      "-DPROJECT_VERSION=${PROJECT_VERSION}"
      "-DVERSION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
      "-DINSTALLER_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/installer/build_installer.ps1"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckVersion.cmake")
endif()

message(STATUS "FastFile ${PROJECT_VERSION} — DuiLib static + Win32 GUI")

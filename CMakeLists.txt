cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FASTFILE_VERSION LIMIT_COUNT 1)
string(STRIP "${FASTFILE_VERSION}" FASTFILE_VERSION)
if(NOT FASTFILE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain a semantic version such as 1.0.9")
endif()
project(FastFile VERSION "${FASTFILE_VERSION}" LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
string(REPLACE "." "," FASTFILE_VERSION_COMMAS "${FASTFILE_VERSION}")
configure_file(res/FastFileBuildInfo.h.in generated/FastFileBuildInfo.h @ONLY)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}/res")


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
  src/MainWnd.ExplorerIntegration.cpp
  src/MainWnd.Views.cpp
  src/MainWnd.Icons.cpp
  src/FluentScrollBarUI.cpp
  src/FluentScrollBarUI.h
  src/TabStripUI.cpp
  src/TabStripUI.h
  src/MainWnd.Preview.cpp
  src/MainWnd.DragDrop.cpp
  src/ShellBrowserHost.cpp
  src/ShellWindowRegistration.cpp
  src/ShellBrowserHost.h
  src/ShellFileOperation.cpp
  src/ShellFileOperation.h
  src/ShellMenuUtil.h
  src/FileTagManager.cpp
  src/FileTagManager.h
  src/MainWnd.h
  src/MainWndInternal.h
  src/ExplorerAgentProtocol.h
  src/FastFileAbout.h
  src/FastFileCore.h
  src/UiTokens.h
  src/FavoriteStarUI.h
  src/FavoritesJson.h
  src/ShellPresentation.h
  src/TabLayout.h
  src/AutoUpdater.cpp
  src/AutoUpdater.h
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
target_link_libraries(FastFile PRIVATE duilib shell32 shlwapi propsys msimg32 version winhttp)
if(MSVC)
  target_compile_options(FastFile PRIVATE /W3 /utf-8)
  # Emit FastFile.map: a crash report gives an RVA, the map turns it into a function name.
  target_link_options(FastFile PRIVATE /MAP)
  set_target_properties(FastFile PROPERTIES
    VS_DPI_AWARE "PerMonitor"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:FastFile>"
  )
endif()

# Headless per-user Shell handoff process; no DuiLib initialization at runtime.
get_target_property(FASTFILE_AGENT_SOURCES FastFile SOURCES)
add_executable(FastFileAgent WIN32 ${FASTFILE_AGENT_SOURCES})
target_compile_definitions(FastFileAgent PRIVATE FASTFILE_AGENT_EXECUTABLE)
target_link_libraries(FastFileAgent PRIVATE duilib shell32 shlwapi propsys msimg32 version winhttp)
if(MSVC)
  target_compile_options(FastFileAgent PRIVATE /W3 /utf-8 /Gy)
  target_link_options(FastFileAgent PRIVATE /OPT:REF)
endif()
add_dependencies(FastFile FastFileAgent)

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
  target_link_libraries(FastFileMainWndRegressionTests PRIVATE duilib shell32 shlwapi propsys msimg32 version)
  if(MSVC)
    target_compile_options(FastFileMainWndRegressionTests PRIVATE /utf-8 /wd4828 /MP)
    target_link_options(FastFileMainWndRegressionTests PRIVATE /MAP)
  endif()
  add_dependencies(FastFileMainWndRegressionTests FastFile)
  add_test(NAME FastFileMainWndRegressionTests COMMAND FastFileMainWndRegressionTests)
  add_test(NAME FastFileQuickAccessTests COMMAND FastFileMainWndRegressionTests --quick-access-only)
  set_tests_properties(FastFileQuickAccessTests PROPERTIES TIMEOUT 60)
  set_tests_properties(FastFileMainWndRegressionTests PROPERTIES TIMEOUT 180)
  # View-switch and async-thumbnail checks run as their own suites (keeps the main suite short).
  add_test(NAME FastFileViewSwitchTests COMMAND FastFileMainWndRegressionTests --view-switch-only)
  set_tests_properties(FastFileViewSwitchTests PROPERTIES TIMEOUT 90)
  add_test(NAME FastFileAsyncThumbTests COMMAND FastFileMainWndRegressionTests --thumbs-only)
  set_tests_properties(FastFileAsyncThumbTests PROPERTIES TIMEOUT 90)
  add_test(NAME FastFileUiPolishTests COMMAND FastFileMainWndRegressionTests --ui-polish-only)
  set_tests_properties(FastFileUiPolishTests PROPERTIES TIMEOUT 30)
  add_test(NAME FastFileShellActivationTests COMMAND FastFileMainWndRegressionTests --shell-activation-only)
  set_tests_properties(FastFileShellActivationTests PROPERTIES TIMEOUT 90)
  add_test(NAME FastFileIntegrationTests COMMAND FastFileMainWndRegressionTests --integration-only)
  set_tests_properties(FastFileIntegrationTests PROPERTIES TIMEOUT 90)
  # Live tests launch real explorer.exe. On modern Windows builds with tabs (such as Windows 11 26H2),
  # /n,/separate requests are merged as tabs into existing Explorer windows rather than opening independent
  # windows. This requires manual invocation (--explorer-live-only / --agent-live-only) when no user Explorer
  # window is active, so they are disabled from default ctest runs to avoid disturbing developer sessions.
  add_test(NAME FastFileExplorerTakeoverLiveTests COMMAND FastFileMainWndRegressionTests --explorer-live-only)
  set_tests_properties(FastFileExplorerTakeoverLiveTests PROPERTIES TIMEOUT 90 DISABLED TRUE)
  add_test(NAME FastFileExplorerAgentLiveTests COMMAND FastFileMainWndRegressionTests --agent-live-only)
  set_tests_properties(FastFileExplorerAgentLiveTests PROPERTIES TIMEOUT 100 DISABLED TRUE)
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
    src/FileTagManager.cpp
  )
  target_include_directories(FastFileShellBrowserTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(FastFileShellBrowserTests PRIVATE shell32 shlwapi ole32 uuid propsys comctl32 msimg32)
  if(MSVC)
    target_compile_options(FastFileShellBrowserTests PRIVATE /utf-8 /wd4828)
  endif()
  add_test(NAME FastFileShellBrowserTests COMMAND FastFileShellBrowserTests)
  add_executable(FastFileShellWindowTests
    tests/ShellWindowRegistrationTests.cpp
    src/ShellWindowRegistration.cpp
  )
  target_include_directories(FastFileShellWindowTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(FastFileShellWindowTests PRIVATE shell32 shlwapi ole32 oleaut32 uuid propsys)
  if(MSVC)
    target_compile_options(FastFileShellWindowTests PRIVATE /utf-8)
  endif()
  add_test(NAME FastFileShellWindowTests COMMAND FastFileShellWindowTests)
  set_tests_properties(FastFileShellWindowTests PROPERTIES TIMEOUT 120)
  add_executable(FastFileAutoUpdaterTests
    tests/AutoUpdaterTests.cpp
    src/AutoUpdater.cpp
  )
  target_include_directories(FastFileAutoUpdaterTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(FastFileAutoUpdaterTests PRIVATE winhttp version shlwapi shell32)
  if(MSVC)
    target_compile_options(FastFileAutoUpdaterTests PRIVATE /utf-8 /wd4828)
  endif()
  add_test(NAME FastFileAutoUpdaterTests COMMAND FastFileAutoUpdaterTests)
  set_tests_properties(FastFileAutoUpdaterTests PROPERTIES TIMEOUT 30)
  add_test(NAME FastFileBuildSourceParity
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckBuildSources.cmake")
  add_test(NAME FastFileVersionSource
    COMMAND "${CMAKE_COMMAND}"
      "-DPROJECT_VERSION=${PROJECT_VERSION}"
      "-DVERSION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
      "-DINSTALLER_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/installer/build_installer.ps1"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckVersion.cmake")
endif()

message(STATUS "FastFile ${PROJECT_VERSION} — DuiLib static + Win32 GUI")

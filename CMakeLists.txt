cmake_minimum_required(VERSION 3.21)
project(HyperBrowser VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
set(BROWSER_ENGINE "WEBVIEW2" CACHE STRING "Browser engine backend: WEBVIEW2 (CEF is a documented future option)")
set_property(CACHE BROWSER_ENGINE PROPERTY STRINGS WEBVIEW2 CEF)
option(HB_PORTABLE "Create portable.flag so settings/profile live next to the exe" OFF)
option(HB_BUILD_TESTS "Build host-side unit tests for the platform-independent core" OFF)
set(WEBVIEW2_VERSION "1.0.2535.41" CACHE STRING "Microsoft.Web.WebView2 NuGet package version")
set(WEBVIEW2_SDK_DIR "" CACHE PATH "Pre-extracted Microsoft.Web.WebView2 package (offline builds). Empty = download.")

# ---------------------------------------------------------------------------
# Environment / compiler guard (clear errors, never a half-working build)
# ---------------------------------------------------------------------------
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  if(HB_BUILD_TESTS)
    # Host-side tests of platform-independent code are allowed on any OS.
    add_executable(core_tests tests/core_tests.cpp src/core/Util.cpp src/core/Url.cpp src/core/Settings.cpp src/perf/PerfLogic.cpp)
    target_include_directories(core_tests PRIVATE src)
    return()
  endif()
  message(FATAL_ERROR "HyperBrowser targets Windows 10/11 (x64). Use MinGW-w64 or MSVC on Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "A 64-bit (x64) toolchain is required. Use the MinGW-w64 x86_64 / UCRT64 shell or an x64 MSVC prompt.")
endif()

if(MINGW)
  set(HB_COMPILER_NAME "MinGW-w64 GCC ${CMAKE_CXX_COMPILER_VERSION}")
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "MinGW detected but compiler is ${CMAKE_CXX_COMPILER_ID}. Only GCC is supported on MinGW.")
  endif()
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    message(FATAL_ERROR "GCC 9 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION}).")
  endif()
elseif(MSVC)
  set(HB_COMPILER_NAME "MSVC ${CMAKE_CXX_COMPILER_VERSION}")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.20)
    message(FATAL_ERROR "Visual Studio 2019 (MSVC 19.20) or newer is required.")
  endif()
else()
  message(FATAL_ERROR "Unsupported compiler '${CMAKE_CXX_COMPILER_ID}'. Use MinGW-w64 GCC or MSVC.")
endif()

if(BROWSER_ENGINE STREQUAL "WEBVIEW2")
  set(HB_ENGINE_STATUS "Supported (MinGW-w64 and MSVC)")
elseif(BROWSER_ENGINE STREQUAL "CEF")
  if(MINGW)
    message(FATAL_ERROR
      "MinGW-w64 detected, but the selected browser engine (CEF) requires MSVC.\n"
      "Install Visual Studio 2022 Build Tools, or configure with -DBROWSER_ENGINE=WEBVIEW2.")
  endif()
  message(FATAL_ERROR
    "The CEF backend is not implemented yet (src/engine/cef is a placeholder).\n"
    "Configure with -DBROWSER_ENGINE=WEBVIEW2.")
else()
  message(FATAL_ERROR "Unknown BROWSER_ENGINE '${BROWSER_ENGINE}'. Use WEBVIEW2.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

message(STATUS "")
message(STATUS "Compiler:       ${HB_COMPILER_NAME}")
message(STATUS "Architecture:   x64")
message(STATUS "Build Type:     ${CMAKE_BUILD_TYPE}${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "Browser Engine: ${BROWSER_ENGINE}")
message(STATUS "Status:         ${HB_ENGINE_STATUS}")
message(STATUS "")

# ---------------------------------------------------------------------------
# WebView2 SDK (headers + loader DLL), fetched at configure time
# ---------------------------------------------------------------------------
include(cmake/WebView2.cmake)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(HB_CORE_SOURCES
  src/core/Util.cpp
  src/core/Url.cpp
  src/core/Settings.cpp
  src/core/AppPaths.cpp
  src/perf/PerfLogic.cpp
  src/perf/PerformanceMonitor.cpp
  src/perf/GamingMode.cpp)

set(HB_UI_SOURCES src/ui/MainWindow.cpp src/ui/PerfPanel.cpp)

set(HB_ENGINE_SOURCES
  src/engine/EngineFactory.cpp
  src/engine/webview2/WebView2Engine.cpp)

add_executable(HyperBrowser WIN32 src/main.cpp ${HB_CORE_SOURCES} ${HB_UI_SOURCES} ${HB_ENGINE_SOURCES})
target_include_directories(HyperBrowser PRIVATE src)
target_include_directories(HyperBrowser SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_include_directories(HyperBrowser PRIVATE "${WEBVIEW2_GENERATED_DIR}")
target_compile_definitions(HyperBrowser PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  WINVER=0x0A00 _WIN32_WINNT=0x0A00
  HB_ENGINE_WEBVIEW2
  HB_VERSION="${PROJECT_VERSION}")
target_link_libraries(HyperBrowser PRIVATE user32 gdi32 shell32 ole32 uuid psapi pdh iphlpapi ws2_32)

# ---------------------------------------------------------------------------
# Compiler / linker flags
# ---------------------------------------------------------------------------
if(MINGW)
  target_compile_options(HyperBrowser PRIVATE -Wall -Wextra)
  # Release: small + fast startup. -O2 keeps hot paths fast; section GC removes dead code.
  target_compile_options(HyperBrowser PRIVATE
    $<$<CONFIG:Release>:-O2;-ffunction-sections;-fdata-sections;-fno-ident>)
  target_link_options(HyperBrowser PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--dynamicbase -Wl,--nxcompat
    $<$<CONFIG:Release>:-s;-Wl,--gc-sections>)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT HB_IPO OUTPUT HB_IPO_MSG LANGUAGES CXX)
  if(HB_IPO)
    set_property(TARGET HyperBrowser PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()
else() # MSVC
  target_compile_options(HyperBrowser PRIVATE /W4 /utf-8 /permissive-
    $<$<CONFIG:Release>:/O1;/Gy;/GL;/guard:cf>)
  target_link_options(HyperBrowser PRIVATE
    /DYNAMICBASE /NXCOMPAT
    $<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF;/guard:cf>)
  set_property(TARGET HyperBrowser PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# Stage runtime files next to the exe (also what a portable copy needs)
# ---------------------------------------------------------------------------
add_custom_command(TARGET HyperBrowser POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEBVIEW2_LOADER_DLL}" "$<TARGET_FILE_DIR:HyperBrowser>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:HyperBrowser>/resources"
  VERBATIM)
if(HB_PORTABLE)
  add_custom_command(TARGET HyperBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch "$<TARGET_FILE_DIR:HyperBrowser>/portable.flag" VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Install / portable ZIP:  cmake --install build --prefix dist   |  cpack --config build/CPackConfig.cmake
# ---------------------------------------------------------------------------
install(TARGETS HyperBrowser RUNTIME DESTINATION .)
install(FILES "${WEBVIEW2_LOADER_DLL}" DESTINATION .)
install(DIRECTORY resources DESTINATION .)
if(HB_PORTABLE)
  file(WRITE "${CMAKE_BINARY_DIR}/portable.flag" "")
  install(FILES "${CMAKE_BINARY_DIR}/portable.flag" DESTINATION .)
endif()
set(CPACK_GENERATOR ZIP)
if(HB_PORTABLE)
  set(CPACK_PACKAGE_FILE_NAME "HyperBrowser-${PROJECT_VERSION}-win64-portable")
else()
  set(CPACK_PACKAGE_FILE_NAME "HyperBrowser-${PROJECT_VERSION}-win64")
endif()
include(CPack)

if(HB_BUILD_TESTS)
  add_executable(core_tests tests/core_tests.cpp src/core/Util.cpp src/core/Url.cpp src/core/Settings.cpp src/perf/PerfLogic.cpp)
  target_include_directories(core_tests PRIVATE src)
endif()

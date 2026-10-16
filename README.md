# HyperBrowser (Stage 2)

A lightweight, gaming-style Windows browser. Stage 1: window, tabs, address bar, navigation, new-tab page,
settings file, MinGW-w64/GCC **and** MSVC support. Stage 2: performance HUD + detail panel, tab sleeping,
Gaming Mode.

> **Not yet compiled on Windows.** This was written in a Linux sandbox without a MinGW compiler or the
> WebView2 SDK. The platform-independent core (URL handling, settings, text utils) is unit-tested and
> passes (URL/settings/sleep policy/formatting, `-DHB_BUILD_TESTS=ON`); the Win32/WebView2 code has been reviewed but never built. Expect to fix a few compile errors
> on the first build and send them back.

## Why WebView2 and not CEF

| | WebView2 | CEF |
|---|---|---|
| MinGW-w64 / GCC | **Works** (COM headers only; loader DLL loaded at runtime) | **No.** Official builds are MSVC-only (`/MT`/`/MD`, `cef_sandbox.lib`); GCC means no sandbox |
| Size | exe is small; Chromium runtime is shared with Edge/Windows | ~200 MB bundled |
| RAM | shared runtime, built-in tab suspend/memory-target APIs | same multi-process Chromium as Chrome |
| Extensions | limited | full (future option) |

`BROWSER_ENGINE=CEF` is rejected by CMake with an explanation (MinGW) or a "not implemented" error (MSVC).
All app code talks to `core/IBrowserEngine.h` only, so a CEF backend can be added in `src/engine/cef/`.

## Build (MinGW-w64, recommended)

1. Install [MSYS2](https://www.msys2.org/), open the **UCRT64** shell, then:
   `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`
2. Configure and build (the WebView2 SDK is downloaded from nuget.org automatically):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# or: cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

3. Run `build/HyperBrowser.exe`. The post-build step copies `WebView2Loader.dll` and `resources/` next to it.

Offline? Download the `Microsoft.Web.WebView2` .nupkg, rename to .zip, extract, and add
`-DWEBVIEW2_SDK_DIR=<folder>`.

## Build (MSVC)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Runtime requirement

The Microsoft Edge **WebView2 Runtime** (preinstalled on Windows 11 and current Windows 10). If missing,
the app shows a message with the download link instead of crashing.

## Portable build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DHB_PORTABLE=ON
cmake --build build
cpack --config build/CPackConfig.cmake        # -> HyperBrowser-0.1.0-win64-portable.zip
```

The zip contains the exe, `WebView2Loader.dll`, `resources/` and `portable.flag`. Copy/unzip anywhere:
settings and the browser profile are then stored in `data\` next to the exe (the folder must be writable).
Without the flag they live in `%LOCALAPPDATA%\HyperBrowser`. The target PC still needs the WebView2 Runtime.

## Release optimizations

MinGW: `-O2`, function/data sections + `--gc-sections`, LTO when supported, stripped symbols, static
libgcc/libstdc++, ASLR + DEP. MSVC: `/O1 /GL /LTCG /OPT:REF,ICF /guard:cf`, static CRT.
Startup/RAM: no scripts or external resources on the new-tab page, no background threads, timers only while needed,
GDI chrome with no animation, one engine process tree shared with the system runtime. *Not yet profiled.*

## Layout

```
src/core/            IBrowserEngine (abstraction), Settings, Url, Util, AppPaths  (Url/Settings/Util are OS-independent)
src/engine/webview2/ WebView2 backend (MinGW-safe: no WRL, no import lib)
src/perf/            PerformanceMonitor (Toolhelp/PDH/IP Helper), GamingMode, PerfLogic (OS-independent, tested)
src/ui/              MainWindow (tabs, toolbar, address bar, HUD strip), PerfPanel (GDI, double-buffered)
resources/           newtab.html
tests/               host-side unit tests:  cmake -S . -B bt -DHB_BUILD_TESTS=ON && cmake --build bt && bt/core_tests
```

## What works in Stage 1

Tabs (open, close, switch, reopen closed, middle-click close), address bar with search fallback (Google by
default, `javascript:`/`data:` never navigated from the bar), back/forward/reload/stop/home, Chrome-style
shortcuts (Ctrl+T/W/Shift+T/L/R/Tab/1-9, Alt+Left/Right/Home, F5, F6), new-tab page, `settings.ini`
(corruption-tolerant, atomic save), friendly errors for missing runtime/loader/profile problems, tab and
engine crash notices, Edge DevTools (F12) and built-in error pages from WebView2.

## Stage 2: performance features

**HUD** - slim status strip at the bottom-right of the window: `CPU 3% | RAM 420 MB | GPU 1%`. Click it for the
detail panel (CPU, RAM, GPU, tabs, browser processes, network, last page load, Performance Mode, Gaming Mode, plus
buttons for Gaming Mode and the mode). Toggle the HUD from the menu or `show_hud` in `settings.ini`.

**What is measured** (the browser's own process tree: our exe plus every WebView2 child process):
CPU = share of the whole machine (like Task Manager); RAM = sum of private bytes (an approximation: shared memory
is not double counted but private commit may exceed physical use); GPU = busiest GPU engine type of those
processes via the Windows "GPU Engine" counters (Windows 10 1709+, shows `n/a` otherwise); Network = **whole-PC**
throughput, not per browser (per-process network needs ETW/admin, TODO).

**No polling when idle:** there are no threads. A 1 Hz timer runs only while the HUD is visible, the panel is open
or Gaming Mode is on; the tab-sleep check is a separate 15 s timer (5 s in Gaming Mode) that only exists when sleeping is enabled.

**Performance modes** (`performance_mode`, menu, or the panel button):
- `balanced` (default): hidden tabs ask the engine for a low memory target; tabs idle for `tab_sleep_minutes` (default 10) are suspended.
- `memory_saver`: same, with a third of the idle time (minimum 1 minute).
- `off`: no tab optimisation at all.

**Tab sleeping is deliberately conservative.** A tab is never put to sleep if it is the active tab, is playing audio,
is still loading, or the engine refuses the suspend (the tab then simply stays awake). Sleeping tabs are drawn dimmed;
a status line shows how many are asleep; they wake instantly when selected. Page timers/scripts of a sleeping tab are paused, so
sites that must stay live in the background (chat, dashboards without audio) can be affected: use `off`, or keep them
playing audio. TODO: per-site exceptions and pinned-tab protection (arrives with pinning).

**Gaming Mode** (toolbar pill, menu, panel, or `Ctrl+Shift+G`; the UI shows `Gaming Mode: ON`):
- lowers the CPU priority of the browser's own processes to *Below Normal* and turns on Windows efficiency mode (EcoQoS) for them,
  re-applied every second to engine processes spawned later;
- sleeps idle tabs after 30 s (same safety rules as above);
- every priority/EcoQoS setting is recorded per process and restored exactly when the mode is switched off or the browser closes
  (a process is only restored if it is still the same process, not a recycled PID).
- It does **not** touch any other process, Windows service, driver, registry key or Windows Defender, and it does not promise any FPS gain: it only
  makes the browser a lighter neighbour. It cannot lower GPU use by itself beyond what sleeping/hidden tabs already save;
  "reduce background network" and "reduce animations" are not implemented (the UI has no animations; no safe network lever exists). TODO: auto-enable when a fullscreen game is detected.

New `settings.ini` keys: `show_hud`, `performance_mode`, `tab_sleep_minutes`.

## Known limitations / TODO (nothing below is faked in the UI)

- **Popups** open as a new tab without `window.opener`; popup-based logins (some "Sign in with Google" flows) may fail. TODO: linked popup windows.
- Settings are edited in `settings.ini` (menu → Edit settings file); changes apply on restart. TODO: settings page.
- Not implemented yet: task manager (the monitor already collects per-process stats for it), AI Hub, bookmarks, history, download manager UI (WebView2's default download UI is used), find, zoom UI, print, private windows, multiple windows, tab pinning/muting/reordering/tab search, extensions, default-browser registration, per-monitor DPI, session restore.
- Cookie/permission/privacy controls beyond WebView2 defaults: TODO.
- Tab strip does not scroll; with very many tabs they shrink to a minimum width.

## Security notes

Engine runs with the WebView2 sandbox; the loader DLL is loaded by full path; settings values are validated
(http/https only); address-bar script/data URLs are blocked; the new-tab page has a no-script CSP; no
registry or system changes are made.

## Roadmap

Stage 2: done (needs a first Windows build).
Stage 3: AI Hub (`ai_services.json`), bookmarks, history, download manager, settings page.
Stage 4: task manager, find/zoom/print, private windows, tab features, session restore.

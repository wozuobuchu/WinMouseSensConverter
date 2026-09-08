# WinMouseSensConverter Agent Guide

## Project overview

WinMouseSensConverter is a native Windows desktop application written in C++20. It reads low-latency keyboard and mouse data through Raw Input and supports two UI modes: signed X/Y distance measurement using a reference DPI, and effective-DPI calibration from a known ruler distance and the final two-dimensional movement vector.

Key behavior:

- Pressing the configured recording key or left-clicking the status bar starts or stops recording through `app_func::toggle_recording`.
- Left-clicking a value grid copies its displayed values to the clipboard (X then Y in Measurement, the DPI text in Calibration).
- Starting a recording clears the previous measurement.
- Measurement mode displays signed X/Y movement: right/down are positive and left/up are negative.
- Calibration mode uses `hypot(dx, dy)` and `calibrated_dpi = counts / (calibration_distance_cm / 2.54)`. Unit controls the `CALDIS` ruler-distance display; Reference DPI supplies its raw-count equivalent. Neither affects calibrated DPI.
- Switching modes preserves the active recording and accumulated X/Y values.
- Defaults, presets, input ranges, and the configuration format are documented in `README.md`; keep both language sections consistent with `config.hpp` and `ui.cpp`.
- Keyboard events, mouse-button events, and mouse movement are collected by one dedicated Raw Input message thread. The main UI thread consumes the SPSC key-event queue and packed movement snapshots, then renders with Direct2D and DirectWrite.
- Input startup is explicit and owned by the entry-point lifetime guard. Registration failure exits with code `1` before configuration I/O or window creation; do not introduce input-thread startup as an include side effect.
- The executable manifest requires administrator privileges at startup.

## Build policy

Only build and test the `x64` platform. Do not build or troubleshoot the `x86`/`Win32` configurations unless a user explicitly requests it.

Run builds from the repository root:

```powershell
.\build_windows.ps1 -Configuration Debug -Platform x64 -NoRestore
.\build_windows.ps1 -Configuration Release -Platform x64 -NoRestore
```

Run the automatic tests from the repository root. The runner builds by default; use `-NoBuild` only after the selected configuration has already been built:

```powershell
.\WinMouseSensConverterAutomaticTest\run_tests.ps1 -Configuration Debug
.\WinMouseSensConverterAutomaticTest\run_tests.ps1 -Configuration Release
```

The test executable must remain a console application that runs as the current user. Do not add or inherit the main application's administrator manifest or start the main executable from the test runner.

Validate code and build changes with both Debug and Release x64 builds and relevant automatic tests. Documentation-only changes require checking the described behavior, commands, and links against the repository; no rebuild is needed.

The normal release artifact is:

```text
x64\Release\WinMouseSensConverter.exe
```

Use `-Clean` only when a clean rebuild is needed. Keep `-NoRestore` for normal local validation after dependencies are installed.

## Build dependencies

- Windows 10 or Windows 11.
- Visual Studio with MSBuild, the MSVC `v145` C++ toolset, and the Windows 10 SDK.
- The Visual Studio "Desktop development with C++" workload supplies the required compiler, resource compiler, linker, Windows headers, Direct2D, DirectWrite, and WRL support.
- PowerShell and `vswhere.exe`; `build_windows.ps1` uses them to locate and initialize the latest suitable Visual Studio installation.
- Boost.Lockfree headers for `boost/lockfree/spsc_queue.hpp`, installed for the `x64-windows` vcpkg triplet.

This repository currently uses classic vcpkg integration and does not contain a `vcpkg.json` manifest. Install and integrate Boost before building if it is missing:

```powershell
vcpkg install boost-lockfree:x64-windows
vcpkg integrate install
```

`d2d1.lib` and `dwrite.lib` are linked from the Windows SDK. The application has no additional third-party runtime library requirement.

## Implementation constraints

- Preserve the single combined Raw Input message thread, its 1 ms buffered-input interval, and its single-producer/single-consumer key-event queue design.
- Keep keyboard and mouse-button transitions deduplicated through the shared `key_down_` state table. Map the five physical mouse buttons to their standard VK values; do not enqueue vertical or horizontal wheel movement as key events.
- Keep cross-mode runtime data in `sync.hpp`: the recording flag, current mode, and accumulated X/Y values must not be replaced by a second application-state container.
- Keep the common, Measurement, and Calibration `D2duiSystemRender` queues isolated in the application view layer. Open one frame, draw the common queue and exactly one mode queue, then end the frame; keep reusable rendering components in `D2DUILIB`.
- Keep local include dependencies acyclic. Component implementations must include their interface and direct dependencies instead of an application umbrella header that includes the component interface; avoid reciprocal includes and back-edges between layers.
- Never perform blocking dialogs, waits, file/network operations, or thread joins in menu handlers or paint paths.
- Event handlers may update application/component state, but must schedule redraws only through `UiState::redraw_dirty = true`. Keep actual rendering in `ui::finish_main_loop_iteration`, gated by the main UI timer. Continue input consumption and mouse-analyser ticks independently of whether a frame is drawn.
- Keep About, Instruction, and custom-setting windows modeless and route their messages through `ui::preprocess_modeless_dialog_message`.
- Persist new user settings in `config.hpp`, including defaults, parsing, validation, loading, and saving, and document them in both language sections of `README.md`. Recording state, results, hover state, and the current run's Custom menu selection are transient.
- Treat every documented configuration field as required. Missing, duplicated, or invalid fields invalidate the complete configuration and restore all defaults; unknown fields remain ignored.
- Reuse configuration parsers for custom input. A successful custom DPI, calibration-distance, or recording-key submission selects Custom; startup maps saved preset values back to preset commands.
- Keep resource scripts UTF-8 and retain `#pragma code_page(65001)`.

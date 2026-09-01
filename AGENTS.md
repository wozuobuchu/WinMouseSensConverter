# WinMouseSensConverter Agent Guide

## Project overview

WinMouseSensConverter is a native Windows desktop application written in C++20. It reads low-latency keyboard and mouse data through Raw Input and supports two UI modes: signed X/Y distance measurement using a reference DPI, and effective-DPI calibration from a known ruler distance and the final two-dimensional movement vector.

Key behavior:

- Pressing F1 starts or stops recording.
- Starting a recording clears the previous measurement.
- Measurement mode displays signed X/Y movement: right/down are positive and left/up are negative.
- Calibration mode uses `hypot(dx, dy)` and `calibrated_dpi = counts / (calibration_distance_cm / 2.54)`. Reference DPI and output unit affect the displayed comparison distances, not the calibrated-DPI result.
- Switching modes preserves the active recording and accumulated X/Y values.
- Default settings are Measurement mode, `800` Reference DPI, `cm`, a `10 cm` calibration distance, and `F1`.
- Supported output units are raw counts, inches, millimeters, centimeters, decimeters, and meters.
- Keyboard and mouse Raw Input are collected by dedicated message threads. The main UI thread consumes their queues and renders with Direct2D and DirectWrite.
- About, Instruction, custom DPI, and custom calibration-distance windows are modeless. UI work must not block the main UI loop or either Raw Input thread.
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

- Preserve the dedicated Raw Input message threads and their single-producer/single-consumer queue design.
- Keep cross-mode runtime data in `sync.hpp`: the recording flag, current mode, and accumulated X/Y values must not be replaced by a second application-state container.
- Keep mode renderers isolated under `ui::modes::measurement` and `ui::modes::calibration`; put reusable drawing and formatting in the common UI layer, and dispatch exactly one mode renderer per frame.
- Never perform blocking dialogs, waits, file/network operations, or thread joins in menu handlers or paint paths.
- Do not perform or request redraws independently from UI event handlers. Events that change visible UI state, including system paint events, must only set `UiState::redraw_dirty = true`; keep actual rendering centralized in the timer-gated end-of-main-loop path.
- Keep help windows modeless and route their messages through `ui::preprocess_modeless_dialog_message`.
- Persist every new user-selectable option in the configuration file. Add its default, parsing, validation, loading, and saving behavior to `config.hpp`, and document the option and configuration format in both language sections of `README.md`.
- Treat every documented configuration field as required. Missing, duplicated, or invalid fields invalidate the complete configuration and restore all defaults; unknown fields remain ignored.
- Calibration Distance presets are `10`, `20`, and `50 cm`; custom input accepts integer centimeters from `10` through `1000`. A successful Custom submission keeps Custom checked for that run, while startup maps saved preset values back to their preset commands.
- Keep resource scripts UTF-8 and retain `#pragma code_page(65001)`.
- Validate relevant changes with both Debug and Release x64 builds; do not require x86 validation.
